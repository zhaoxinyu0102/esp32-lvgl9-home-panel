/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file main.cpp
 * @brief 智慧家居面板：板级初始化 -> LVGL 移植层 -> 界面 -> 应用服务（WiFi/时间/天气/设备）。
 */

#include <Arduino.h>
#include <assert.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>

#include "app_config.h"
#include "app_display.h"
#include "app_events.h"
#include "app_services.h"
#include "device_model.h"
#include "lvgl_v9_port.h"
#include "time_service.h"
#include "ui.h"
#include "web_panel.h"
#include "wifi_manager.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static Backlight * s_backlight = nullptr;
static uint8_t     s_brightness = APP_DEFAULT_BRIGHTNESS;

/* -------------------------------------------------------------------------
 * 背光（设置页调用）
 * ---------------------------------------------------------------------- */
void app_set_backlight(uint8_t percent)
{
    if (percent < 1) {
        percent = 1;
    }
    if (percent > 100) {
        percent = 100;
    }
    s_brightness = percent;
    if (s_backlight != nullptr) {
        s_backlight->setBrightness(percent);
    }
    ev_backlight(percent);          /* 背光滑条：上报百分比 */
}

uint8_t app_get_backlight(void)
{
    return s_brightness;
}

/* -------------------------------------------------------------------------
 * setup / loop
 * ---------------------------------------------------------------------- */
void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("==== 智慧家居面板启动 ====");

    Serial.println("初始化板级硬件");
    Board * board = new Board();
    board->init();
#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif
    assert(board->begin());

    /* 背光 */
    s_backlight = board->getBacklight();
    app_set_backlight(APP_DEFAULT_BRIGHTNESS);

    Serial.println("初始化 LVGL 移植层");
    if (!lvgl_port_init(board->getLCD(), board->getTouch())) {
        Serial.println("LVGL 初始化失败");
        return;
    }
    Serial.printf("[sys] 剩余内存 heap=%u PSRAM=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());

#if APP_DIAG_PLAIN_SCREEN
    /* 诊断模式分两段：
     *   第 1 段：让面板驱动直接画彩条（完全绕过 LVGL）-> 验证 面板/总线/时序 是否正常
     *   第 2 段：用 LVGL 画一个红屏              -> 验证 LVGL 移植层 + 渲染/切换帧缓冲 是否正常
     */
    auto lcd_test = board->getLCD();
    Serial.printf("[diag] 面板: %dx%d, 帧缓冲数=%d\n",
                  (int)lcd_test->getFrameWidth(), (int)lcd_test->getFrameHeight(),
                  (int)lcd_test->getFrameBufferCount());
    Serial.println("[diag] 第 1 段：面板直接画彩条（绕过 LVGL），请观察 3 秒...");
    lcd_test->colorBarTest(200, 100);
    delay(3000);

    Serial.println("[diag] 第 2 段：LVGL 红屏");
    if (lvgl_port_lock(-1)) {
        lv_obj_t * scr = lv_obj_create(NULL);
        lv_obj_remove_style_all(scr);
        lv_obj_set_style_bg_color(scr, lv_color_hex(0xE53935), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_t * lbl = lv_label_create(scr);
        lv_label_set_text(lbl, "DISPLAY OK");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
        lv_obj_center(lbl);
        lv_screen_load(scr);
        lvgl_port_unlock();
    }
    Serial.println("[diag] 结论对照：彩条有+红屏无 => LVGL 层；都没有 => 面板/总线层");
    return;
#endif

    /* 设备模型（模拟数据） */
    dm_init();
    /* WiFi：读 NVS 里的凭据并开始连接 */
    wifi_manager_init();

    Serial.println("创建界面");
    if (lvgl_port_lock(-1)) {
        ui_init();
        lvgl_port_unlock();
    }
    Serial.printf("[sys] 界面创建后 heap=%u PSRAM=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());

    Serial.println("启动后台服务（时间 / 天气 / 界面刷新）");
    app_services_start();

    /* 网页面板：手机浏览器直接访问 http://<IP> 或 http://smart-home.local */
    web_panel_init();

    Serial.println("就绪");
}

void loop()
{
    delay(1000);
}
