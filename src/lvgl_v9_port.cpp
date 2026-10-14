/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file lvgl_v9_port.cpp
 * @brief LVGL **v9** porting layer for `ESP32_Display_Panel` (Arduino / PlatformIO).
 *
 * 鐢?ESP32_Display_Panel 鐨?`examples/platformio/lvgl_v8_port` 绉绘鑰屾潵锛屽彧鏇挎崲 LVGL 鑳舵按灞傦紝
 * 鏉跨骇鍒濆鍖栵紙Board / LCD / Touch 椹卞姩锛変繚鎸佷笉鍙樸€倂8 -> v9 鐨勫搴斿叧绯伙細
 *
 *   lv_disp_draw_buf_init + lv_disp_drv_t + lv_disp_drv_register
 *          -> lv_display_create + lv_display_set_buffers + lv_display_set_flush_cb
 *   lv_disp_flush_ready(drv)              -> lv_display_flush_ready(disp)
 *   disp_drv.user_data                    -> lv_display_set_user_data / lv_display_get_user_data
 *   disp_drv.full_refresh / direct_mode   -> lv_display_set_buffers(..., LV_DISPLAY_RENDER_MODE_FULL/DIRECT)
 *   disp_drv.rounder_cb                   -> LV_EVENT_INVALIDATE_AREA 鏄剧ず浜嬩欢鍥炶皟
 *   lv_indev_drv_t + lv_indev_drv_register-> lv_indev_create + lv_indev_set_type + lv_indev_set_read_cb
 *   lv_disp_set_rotation(LV_DISP_ROT_*)   -> lv_display_set_rotation(LV_DISPLAY_ROTATION_*)
 *   LV_COLOR_16_SWAP 瀹?                  -> lv_display_set_color_format(RGB565 / RGB565_SWAPPED)
 *   鎵嬪啓 rotate_copy_pixel 纭欢鏃嬭浆        -> LVGL v9 鍐呯疆杞欢鏃嬭浆锛坙v_display_set_rotation锛? */

#include <assert.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#undef ESP_UTILS_LOG_TAG
#define ESP_UTILS_LOG_TAG "LvPort"
#include "esp_lib_utils.h"
#include "lvgl_v9_port.h"

using namespace esp_panel::drivers;

#define LVGL_PORT_BUFFER_NUM_MAX    (3)

static SemaphoreHandle_t lvgl_mux = nullptr;                  // LVGL mutex
static TaskHandle_t lvgl_task_handle = nullptr;               // LVGL task handle
static esp_timer_handle_t lvgl_tick_timer = NULL;             // LVGL tick timer
static void *lvgl_buf[LVGL_PORT_BUFFER_NUM_MAX] = {};         // LVGL draw buffers
static LCD *lvgl_lcd = nullptr;                               // LCD device used by the callbacks

/**
 * @brief Rounder callback (v8 `disp_drv.rounder_cb`).
 *
 * In LVGL v9 the invalidated area is exposed as a display event, the callback can round it for panels which require
 * aligned coordinates (mostly SPI/QSPI panels).
 */
static void rounder_event_cb(lv_event_t *e)
{
    LCD *lcd = (LCD *)lv_event_get_user_data(e);
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);

    if ((lcd == nullptr) || (area == nullptr)) {
        return;
    }

    uint8_t x_align = lcd->getBasicAttributes().basic_bus_spec.x_coord_align;
    uint8_t y_align = lcd->getBasicAttributes().basic_bus_spec.y_coord_align;

    if (x_align > 1) {
        // round the start of coordinate down to the nearest aligned value
        area->x1 &= ~(x_align - 1);
        // round the end of coordinate up to the nearest aligned value
        area->x2 = (area->x2 & ~(x_align - 1)) + x_align - 1;
    }

    if (y_align > 1) {
        area->y1 &= ~(y_align - 1);
        area->y2 = (area->y2 & ~(y_align - 1)) + y_align - 1;
    }
}

#if LVGL_PORT_AVOID_TEAR && LVGL_PORT_FULL_REFRESH

/**
 * @brief Flush callback used with the full-refresh modes (1, 2).
 *
 * The draw buffers ARE the LCD frame buffers (`LV_DISPLAY_RENDER_MODE_FULL`), so a flush is always a whole frame:
 * hand the buffer over to the panel, wait until the panel has sent it, then let LVGL render the next frame.
 */
static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;

    /* Switch the current LCD frame buffer to the buffer LVGL just filled */
    lvgl_lcd->switchFrameBufferTo(px_map);

    /* Waiting for that frame buffer to complete transmission (`onLcdVsyncCallback` notifies us) */
    ulTaskNotifyValueClear(NULL, ULONG_MAX);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    lv_display_flush_ready(disp);
}

#elif LVGL_PORT_AVOID_TEAR && LVGL_PORT_DIRECT_MODE

/**
 * @brief Flush callback used with the LVGL direct modes (3, 4).
 *
 * In direct mode `px_map` points into the LCD frame buffer LVGL has just rendered the invalidated areas
 * into, so there is nothing to copy: just hand that buffer to the panel, which switches to it on the next
 * VSYNC. LVGL rotates the (two or three) frame buffers itself.
 */
static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;

    if (lv_display_flush_is_last(disp)) {
        lvgl_lcd->switchFrameBufferTo(px_map);
#if LVGL_PORT_DIRECT_WAIT_VSYNC
        /* With only two buffers we must wait until the switch really happened, otherwise LVGL would start
         * rendering into the buffer which is still being scanned out */
        ulTaskNotifyValueClear(NULL, ULONG_MAX);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#endif
    }

    lv_display_flush_ready(disp);
}

#else

/**
 * @brief Flush callback used when the avoid tearing function is disabled (partial rendering).
 */
static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    LCD *lcd = lvgl_lcd;
    const int offsetx1 = area->x1;
    const int offsety1 = area->y1;
    const int width = area->x2 - area->x1 + 1;
    const int height = area->y2 - area->y1 + 1;

    lcd->drawBitmap(offsetx1, offsety1, width, height, (const uint8_t *)px_map);

    /* For RGB LCD the data is moved by DMA, LVGL can start the next area right away.
     * For the other buses `onDrawBitmapFinishCallback` notifies LVGL when the transfer is done. */
    if (lcd->getBus()->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        lv_display_flush_ready(disp);
    }
}

#endif /* LVGL_PORT_AVOID_TEAR */

/**
 * @brief Notify LVGL that one bitmap transfer has finished (non-RGB buses only).
 */
IRAM_ATTR bool onDrawBitmapFinishCallback(void *user_data)
{
    lv_display_t *disp = (lv_display_t *)user_data;

    lv_display_flush_ready(disp);

    return false;
}

#if LVGL_PORT_AVOID_TEAR && LVGL_PORT_NEED_VSYNC
/**
 * @brief Notify the LVGL task that the LCD has finished sending a frame (called in ISR).
 */
IRAM_ATTR bool onLcdVsyncCallback(void *user_data)
{
    BaseType_t need_yield = pdFALSE;
    TaskHandle_t task_handle = (TaskHandle_t)user_data;

    xTaskNotifyFromISR(task_handle, ULONG_MAX, eNoAction, &need_yield);

    return (need_yield == pdTRUE);
}
#endif

/**
 * @brief Map `LVGL_PORT_ROTATION_DEGREE` to the LVGL v9 rotation enum.
 */
static lv_display_rotation_t get_rotation(void)
{
    switch (LVGL_PORT_ROTATION_DEGREE) {
    case 90:
        return LV_DISPLAY_ROTATION_90;
    case 180:
        return LV_DISPLAY_ROTATION_180;
    case 270:
        return LV_DISPLAY_ROTATION_270;
    default:
        return LV_DISPLAY_ROTATION_0;
    }
}

static lv_display_t *display_init(LCD *lcd)
{
    ESP_UTILS_CHECK_FALSE_RETURN(lcd != nullptr, nullptr, "Invalid LCD device");
    ESP_UTILS_CHECK_FALSE_RETURN(lcd->getRefreshPanelHandle() != nullptr, nullptr, "LCD device is not initialized");

    auto lcd_width = lcd->getFrameWidth();
    auto lcd_height = lcd->getFrameHeight();
    uint32_t buffer_size_bytes = 0;

    ESP_UTILS_LOGD("Malloc memory for LVGL buffer");
#if !LVGL_PORT_AVOID_TEAR
    // Avoid tearing function is disabled: partial buffers in SRAM.
    // Start with LVGL_PORT_BUFFER_SIZE_HEIGHT lines, halve it if the internal SRAM is tight.
    uint16_t buffer_height = LVGL_PORT_BUFFER_SIZE_HEIGHT;
    while (1) {
        buffer_size_bytes = (uint32_t)lcd_width * buffer_height * sizeof(lv_color_t);
        bool allocated = true;
        for (int i = 0; (i < LVGL_PORT_BUFFER_NUM) && (i < LVGL_PORT_BUFFER_NUM_MAX); i++) {
            lvgl_buf[i] = heap_caps_malloc(buffer_size_bytes, LVGL_PORT_BUFFER_MALLOC_CAPS);
            if (lvgl_buf[i] == nullptr) {
                allocated = false;
                break;
            }
        }
        if (allocated) {
            break;
        }
        for (int i = 0; i < LVGL_PORT_BUFFER_NUM_MAX; i++) {
            if (lvgl_buf[i] != nullptr) {
                free(lvgl_buf[i]);
                lvgl_buf[i] = nullptr;
            }
        }
        ESP_UTILS_CHECK_FALSE_RETURN(buffer_height > 8, nullptr, "Malloc LVGL buffer failed");
        ESP_UTILS_LOGW("Only %d lines fit in SRAM, retry with %d lines", buffer_height, buffer_height / 2);
        buffer_height /= 2;
    }
    for (int i = 0; i < LVGL_PORT_BUFFER_NUM; i++) {
        ESP_UTILS_LOGD("Buffer[%d] address: %p, size: %d", i, lvgl_buf[i], (int)buffer_size_bytes);
    }
#else
    // Avoid tearing function is enabled: use the LCD frame buffers, one whole screen each
    buffer_size_bytes = lcd_width * lcd_height * sizeof(lv_color_t);
    for (int i = 0; (i < LVGL_PORT_DISP_BUFFER_NUM) && (i < LVGL_PORT_BUFFER_NUM_MAX); i++) {
        lvgl_buf[i] = lcd->getFrameBufferByIndex(i);
        ESP_UTILS_CHECK_NULL_RETURN(lvgl_buf[i], nullptr, "Invalid LCD frame buffer");
    }
#endif
    ESP_UTILS_LOGD("LVGL buffer size: %d bytes", (int)buffer_size_bytes);

    ESP_UTILS_LOGD("Create LVGL display");
    lv_display_t *disp = lv_display_create(lcd_width, lcd_height);
    ESP_UTILS_CHECK_NULL_RETURN(disp, nullptr, "Create LVGL display failed");

    lv_display_set_flush_cb(disp, flush_callback);

    /* LVGL v9 expresses the byte order with the color format.
     * `-DLV_COLOR_16_SWAP=1` (SPI/QSPI panels) -> RGB565 with swapped bytes. */
#if defined(LV_COLOR_16_SWAP) && LV_COLOR_16_SWAP
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
#else
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
#endif

#if LVGL_PORT_AVOID_TEAR && LVGL_PORT_DIRECT_MODE
    /* Direct mode: LVGL renders the invalidated areas straight into the LCD frame buffers */
    lv_display_set_buffers(disp, lvgl_buf[0], lvgl_buf[1], buffer_size_bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
#if LVGL_PORT_DISP_BUFFER_NUM >= 3
    /* Three buffers: LVGL rotates them and synchronizes the areas which were not redrawn
     * (`refr_sync_areas()`), so it never renders into the buffer currently on screen ->
     * tear free and no VSYNC stall. */
    static lv_draw_buf_t third_buf;
    lv_color_format_t cf = lv_display_get_color_format(disp);
    uint32_t stride = lv_draw_buf_width_to_stride(lcd_width, cf);
    lv_draw_buf_init(&third_buf, lcd_width, lcd_height, cf, stride, lvgl_buf[2], buffer_size_bytes);
    lv_display_set_3rd_draw_buffer(disp, &third_buf);
#endif
#elif LVGL_PORT_AVOID_TEAR
    /* Full refresh: the draw buffers are the LCD frame buffers, a flush covers the whole screen */
    lv_display_set_buffers(disp, lvgl_buf[0], lvgl_buf[1], buffer_size_bytes, LV_DISPLAY_RENDER_MODE_FULL);
#else
    lv_display_set_buffers(disp, lvgl_buf[0], lvgl_buf[1], buffer_size_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif
    lv_display_set_user_data(disp, (void *)lcd);

    // Only available when the coordinate alignment is enabled
    if ((lcd->getBasicAttributes().basic_bus_spec.x_coord_align > 1) ||
            (lcd->getBasicAttributes().basic_bus_spec.y_coord_align > 1)) {
        lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, (void *)lcd);
    }

    // LVGL v9 rotates in software and swaps the resolution internally
    lv_display_set_rotation(disp, get_rotation());

    return disp;
}

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    Touch *tp = (Touch *)lv_indev_get_user_data(indev);
    TouchPoint point;

    /* Read data from touch controller */
    int read_touch_result = tp->readPoints(&point, 1, 0);
    if (read_touch_result > 0) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static lv_indev_t *indev_init(Touch *tp)
{
    ESP_UTILS_CHECK_FALSE_RETURN(tp != nullptr, nullptr, "Invalid touch device");
    ESP_UTILS_CHECK_FALSE_RETURN(tp->getPanelHandle() != nullptr, nullptr, "Touch device is not initialized");

    ESP_UTILS_LOGD("Create LVGL input device");
    lv_indev_t *indev = lv_indev_create();
    ESP_UTILS_CHECK_NULL_RETURN(indev, nullptr, "Create LVGL input device failed");

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
    lv_indev_set_user_data(indev, (void *)tp);

    return indev;
}

static void tick_increment(void *arg)
{
    LV_UNUSED(arg);
    /* Tell LVGL how many milliseconds have elapsed */
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

static bool tick_init(void)
{
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &tick_increment,
        .name = "LVGL tick"
    };
    ESP_UTILS_CHECK_ERROR_RETURN(
        esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer), false, "Create LVGL tick timer failed"
    );
    ESP_UTILS_CHECK_ERROR_RETURN(
        esp_timer_start_periodic(lvgl_tick_timer, LVGL_PORT_TICK_PERIOD_MS * 1000), false,
        "Start LVGL tick timer failed"
    );

    return true;
}

static bool tick_deinit(void)
{
    ESP_UTILS_CHECK_ERROR_RETURN(
        esp_timer_stop(lvgl_tick_timer), false, "Stop LVGL tick timer failed"
    );
    ESP_UTILS_CHECK_ERROR_RETURN(
        esp_timer_delete(lvgl_tick_timer), false, "Delete LVGL tick timer failed"
    );
    return true;
}

static void lvgl_port_task(void *arg)
{
    ESP_UTILS_LOGD("Starting LVGL task");

    uint32_t task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_port_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        if (task_delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

bool lvgl_port_init(LCD *lcd, Touch *tp)
{
    ESP_UTILS_CHECK_FALSE_RETURN(lcd != nullptr, false, "Invalid LCD device");

    auto bus_type = lcd->getBus()->getBasicAttributes().type;
    lvgl_lcd = lcd;
#if LVGL_PORT_AVOID_TEAR
    ESP_UTILS_CHECK_FALSE_RETURN(
        (bus_type == ESP_PANEL_BUS_TYPE_RGB) || (bus_type == ESP_PANEL_BUS_TYPE_MIPI_DSI), false,
        "Avoid tearing function only works with RGB/MIPI-DSI LCD now"
    );
    ESP_UTILS_LOGI(
        "Avoid tearing is enabled, mode: %d, rotation: %d", LVGL_PORT_AVOID_TEARING_MODE, LVGL_PORT_ROTATION_DEGREE
    );
#else
    ESP_UTILS_LOGI("Avoid tearing is disabled, rotation: %d", LVGL_PORT_ROTATION_DEGREE);
#endif

    lv_display_t *disp = nullptr;
    lv_indev_t *indev = nullptr;

    lv_init();
    ESP_UTILS_CHECK_FALSE_RETURN(tick_init(), false, "Initialize LVGL tick failed");

    ESP_UTILS_LOGI("Initializing LVGL display driver");
    disp = display_init(lcd);
    ESP_UTILS_CHECK_NULL_RETURN(disp, false, "Initialize LVGL display driver failed");

    // For non-RGB LCD, need to notify LVGL that the buffer is ready when the refresh is finished
    if (bus_type != ESP_PANEL_BUS_TYPE_RGB) {
        ESP_UTILS_LOGD("Attach refresh finish callback to LCD");
        lcd->attachDrawBitmapFinishCallback(onDrawBitmapFinishCallback, (void *)disp);
    }

    if (tp != nullptr) {
        ESP_UTILS_LOGD("Initialize LVGL input driver");
        indev = indev_init(tp);
        ESP_UTILS_CHECK_NULL_RETURN(indev, false, "Initialize LVGL input driver failed");

#if LVGL_PORT_ROTATION_DEGREE != 0
        auto &transformation = tp->getTransformation();
#if LVGL_PORT_ROTATION_DEGREE == 90
        tp->swapXY(!transformation.swap_xy);
        tp->mirrorY(!transformation.mirror_y);
#elif LVGL_PORT_ROTATION_DEGREE == 180
        tp->mirrorX(!transformation.mirror_x);
        tp->mirrorY(!transformation.mirror_y);
#elif LVGL_PORT_ROTATION_DEGREE == 270
        tp->swapXY(!transformation.swap_xy);
        tp->mirrorX(!transformation.mirror_x);
#endif
#endif
    }

    ESP_UTILS_LOGD("Create mutex for LVGL");
    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    ESP_UTILS_CHECK_NULL_RETURN(lvgl_mux, false, "Create LVGL mutex failed");

    ESP_UTILS_LOGD("Create LVGL task");
    BaseType_t core_id = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE;
    BaseType_t ret = xTaskCreatePinnedToCore(lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, NULL,
                     LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core_id);
    ESP_UTILS_CHECK_FALSE_RETURN(ret == pdPASS, false, "Create LVGL task failed");

#if LVGL_PORT_AVOID_TEAR && LVGL_PORT_NEED_VSYNC
    lcd->attachRefreshFinishCallback(onLcdVsyncCallback, (void *)lvgl_task_handle);
#endif

    return true;
}

bool lvgl_port_lock(int timeout_ms)
{
    ESP_UTILS_CHECK_NULL_RETURN(lvgl_mux, false, "LVGL mutex is not initialized");

    const TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE);
}

bool lvgl_port_unlock(void)
{
    ESP_UTILS_CHECK_NULL_RETURN(lvgl_mux, false, "LVGL mutex is not initialized");

    xSemaphoreGiveRecursive(lvgl_mux);

    return true;
}

bool lvgl_port_deinit(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(tick_deinit(), false, "Deinitialize LVGL tick failed");

    ESP_UTILS_CHECK_FALSE_RETURN(lvgl_port_lock(-1), false, "Lock LVGL failed");
    if (lvgl_task_handle != nullptr) {
        vTaskDelete(lvgl_task_handle);
        lvgl_task_handle = nullptr;
    }
    ESP_UTILS_CHECK_FALSE_RETURN(lvgl_port_unlock(), false, "Unlock LVGL failed");

    lv_deinit();

#if !LVGL_PORT_AVOID_TEAR
    for (int i = 0; i < LVGL_PORT_BUFFER_NUM; i++) {
        if (lvgl_buf[i] != nullptr) {
            free(lvgl_buf[i]);
            lvgl_buf[i] = nullptr;
        }
    }
#endif
    if (lvgl_mux != nullptr) {
        vSemaphoreDelete(lvgl_mux);
        lvgl_mux = nullptr;
    }

    return true;
}
