/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include "sdkconfig.h"
#ifdef CONFIG_ARDUINO_RUNNING_CORE
#include <Arduino.h>
#endif
#include "esp_display_panel.hpp"
#include "lvgl.h"

// *INDENT-OFF*

/**
 * LVGL related parameters, can be adjusted by users
 */
#define LVGL_PORT_TICK_PERIOD_MS                (2) // The period of the LVGL tick task, in milliseconds

/**
 * LVGL buffer related parameters, can be adjusted by users:
 *
 *  - Memory type for buffer allocation:
 *      - MALLOC_CAP_SPIRAM: Allocate LVGL buffer in PSRAM
 *      - MALLOC_CAP_INTERNAL: Allocate LVGL buffer in SRAM
 *
 *  - The size (in bytes) and number of buffers:
 *      - Lager buffer size can improve FPS, but it will occupy more memory. Maximum buffer size is `width * height`.
 *      - The number of buffers should be 1 or 2.
 *
 *  NOTE: 800x480 partial buffer, 20 lines -> 800*20*2 = 32KB per buffer, 2 buffers = 64KB SRAM.
 *        If the UI needs more FPS, raise `LVGL_PORT_BUFFER_SIZE_HEIGHT` (e.g. 40) and watch free SRAM.
 */
#define LVGL_PORT_BUFFER_MALLOC_CAPS            (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)       // Allocate LVGL buffer in SRAM
// #define LVGL_PORT_BUFFER_MALLOC_CAPS            (MALLOC_CAP_SPIRAM)      // Allocate LVGL buffer in PSRAM
#define LVGL_PORT_BUFFER_SIZE_HEIGHT            (40)
#define LVGL_PORT_BUFFER_NUM                    (2)

/**
 * LVGL timer handle task related parameters, can be adjusted by users
 */
#define LVGL_PORT_TASK_MAX_DELAY_MS             (500)       // The maximum delay of the LVGL timer task, in milliseconds
#define LVGL_PORT_TASK_MIN_DELAY_MS             (2)         // The minimum delay of the LVGL timer task, in milliseconds

/**
 * NOTE: SquareLine 鍜栧暋 UI 鍏?12 涓睆 + 13 涓粍浠?+ 澶ч噺鍥剧墖瀵硅薄锛屽眰绾?甯冨眬閫掑綊杈冩繁銆? *       ESP-IDF 閭ｄ唤渚嬬▼閲屽洜涓洪粯璁?8KB 鏍堜細宕╋紙瑙?squareline_coffee_5inch/main/main.c 娉ㄩ噴锛夛紝
 *       鎶?LVGL 浠诲姟鏍堟彁鍒颁簡 18KB锛岃繖閲屽悓鏍风粰鍒?20KB銆? */
#define LVGL_PORT_TASK_STACK_SIZE               (32 * 1024) // The stack size of the LVGL timer task, in bytes
#define LVGL_PORT_TASK_PRIORITY                 (2)         // The priority of the LVGL timer task
#ifdef ARDUINO_RUNNING_CORE
#define LVGL_PORT_TASK_CORE                     (ARDUINO_RUNNING_CORE)  // Valid if using Arduino
#else
#define LVGL_PORT_TASK_CORE                     (0)                     // Valid if using ESP-IDF
#endif

/**
 * Display rotation, can be adjusted by users.
 *
 *  - 0 / 90 / 180 / 270
 *
 *  NOTE: LVGL v9 rotates in software (`lv_display_set_rotation()`), the horizontal/vertical resolution is swapped
 *        internally. The touch transformation is updated accordingly in `lvgl_port_init()`.
 */
#ifdef CONFIG_LVGL_PORT_ROTATION_DEGREE
#define LVGL_PORT_ROTATION_DEGREE               (CONFIG_LVGL_PORT_ROTATION_DEGREE)
#else
#define LVGL_PORT_ROTATION_DEGREE               (0)     // Valid if using Arduino
#endif

#if (LVGL_PORT_ROTATION_DEGREE != 0) && (LVGL_PORT_ROTATION_DEGREE != 90) && \
    (LVGL_PORT_ROTATION_DEGREE != 180) && (LVGL_PORT_ROTATION_DEGREE != 270)
    #error "Invalid rotation degree, please set macro `LVGL_PORT_ROTATION_DEGREE` to 0, 90, 180 or 270"
#endif

/**
 * Avoid tearing related configurations, can be adjusted by users.
 *
 * Set the avoid tearing mode:
 *      - 0: Disable avoid tearing function (partial rendering, two draw buffers in SRAM)
 *      - 1: LCD double-buffer & LVGL full-refresh     (waits for VSYNC every frame)
 *      - 2: LCD triple-buffer & LVGL full-refresh     (waits for VSYNC every frame)
 *      - 3: LCD double-buffer & LVGL direct-mode      (waits for VSYNC every frame)
 *      - 4: LCD triple-buffer & LVGL direct-mode      <-- recommended, default
 *
 *  NOTE: This function only works with RGB/MIPI-DSI LCD, and rotation must stay 0 in these modes.
 *
 *  Mode 4 is the LVGL v9 equivalent of the ESP-IDF example's `TRIPLE_PARTIAL`: LVGL renders the
 *  invalidated areas straight into one of the three frame buffers and rotates them itself
 *  (`lv_display_set_3rd_draw_buffer()` + `refr_sync_areas()`), so the buffer being rendered into is
 *  never the one which is currently scanned out. Tear free, and no VSYNC stall (which is what makes
 *  modes 1-3 slower).
 */
#ifdef CONFIG_LVGL_PORT_AVOID_TEARING_MODE
#define LVGL_PORT_AVOID_TEARING_MODE            (CONFIG_LVGL_PORT_AVOID_TEARING_MODE)
#else
#define LVGL_PORT_AVOID_TEARING_MODE            (4)     // Valid if using Arduino
#endif

#if LVGL_PORT_AVOID_TEARING_MODE != 0
#define LVGL_PORT_AVOID_TEAR                    (1)

#if (LVGL_PORT_AVOID_TEARING_MODE < 1) || (LVGL_PORT_AVOID_TEARING_MODE > 4)
    #error "lvgl_v9_port: LVGL_PORT_AVOID_TEARING_MODE must be 0 (off), 1, 2, 3 or 4"
#endif
#if LVGL_PORT_ROTATION_DEGREE != 0
    #error "lvgl_v9_port: rotation cannot be combined with avoid tearing, please set LVGL_PORT_ROTATION_DEGREE to 0 or LVGL_PORT_AVOID_TEARING_MODE to 0"
#endif

#if LVGL_PORT_AVOID_TEARING_MODE == 1
    #define LVGL_PORT_DISP_BUFFER_NUM           (2)
    #define LVGL_PORT_FULL_REFRESH              (1)
    #define LVGL_PORT_NEED_VSYNC                (1)
#elif LVGL_PORT_AVOID_TEARING_MODE == 2
    #define LVGL_PORT_DISP_BUFFER_NUM           (3)
    #define LVGL_PORT_FULL_REFRESH              (1)
    #define LVGL_PORT_NEED_VSYNC                (1)
#elif LVGL_PORT_AVOID_TEARING_MODE == 3
    #define LVGL_PORT_DISP_BUFFER_NUM           (2)
    #define LVGL_PORT_DIRECT_MODE               (1)
    #define LVGL_PORT_DIRECT_WAIT_VSYNC         (1)     // only 2 buffers: must wait for the switch
    #define LVGL_PORT_NEED_VSYNC                (1)
#elif LVGL_PORT_AVOID_TEARING_MODE == 4
    #define LVGL_PORT_DISP_BUFFER_NUM           (3)
    #define LVGL_PORT_DIRECT_MODE               (1)
    #define LVGL_PORT_DIRECT_WAIT_VSYNC         (0)     // 3 buffers: no stall
    #define LVGL_PORT_NEED_VSYNC                (0)
#endif
#endif /* LVGL_PORT_AVOID_TEARING_MODE */

// *INDENT-ON*

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Porting LVGL with LCD and touch panel. This function should be called after the initialization of the LCD and
 *        touch panel.
 *
 * @param lcd The pointer to the LCD panel device, mustn't be nullptr
 * @param tp  The pointer to the touch panel device, set to nullptr if is not used
 *
 * @return true if success, otherwise false
 */
bool lvgl_port_init(esp_panel::drivers::LCD *lcd, esp_panel::drivers::Touch *tp);

/**
 * @brief Deinitialize the LVGL porting.
 *
 * @return true if success, otherwise false
 */
bool lvgl_port_deinit(void);

/**
 * @brief Lock the LVGL mutex. This function should be called before calling any LVGL APIs when not in LVGL task,
 *        and the `lvgl_port_unlock()` function should be called later.
 *
 * @param timeout_ms The timeout of the mutex lock, in milliseconds. If the timeout is set to `-1`, it will wait indefinitely.
 *
 * @return true if success, otherwise false
 */
bool lvgl_port_lock(int timeout_ms);

/**
 * @brief Unlock the LVGL mutex. This function should be called after using LVGL APIs when not in LVGL task, and the
 *        `lvgl_port_lock()` function should be called before.
 *
 * @return true if success, otherwise false
 */
bool lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
