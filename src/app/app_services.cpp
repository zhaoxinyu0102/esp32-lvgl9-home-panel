/**
 * @file app_services.cpp
 * @brief 后台服务任务实现。
 */

#include "app_services.h"

#include <Arduino.h>

#include "app_events.h"
#include "app_log.h"
#include "device_model.h"
#include "serial_cli.h"
#include "time_service.h"
#include "weather_service.h"
#include "web_panel.h"
#include "wifi_manager.h"

#include "lvgl_v9_port.h"
#include "ui.h"

#define APP_TASK_TICK_MS   250
#define APP_TASK_STACK     16384   /* 建一个屏要跑很深的调用链，6KB 会爆栈崩溃 */
#define APP_TASK_PRIO      2

static void app_task(void * arg)
{
    (void)arg;

    uint32_t tick = 0;
    APP_LOG("应用服务任务已启动");

    while (1) {
        /* ---- 每拍都做的事（不碰 UI，不需要锁） ---- */
        serial_cli_tick();
        app_events_tick();          /* 250ms 一拍：状态变了就往串口发事件行（见 app_events.h） */
        wifi_manager_tick();
        dm_tick(APP_TASK_TICK_MS);
        web_panel_loop();           /* 处理网页请求 / SSE 保活 */

        /* 把应用层排队的提示弹到屏幕上（手机点一下，屏幕立刻有反馈） */
        if (ui_toast_pending() && lvgl_port_lock(50)) {
            ui_toast_flush();
            lvgl_port_unlock();
        }

        /* ---- 每秒刷一次界面 + 推一次网页状态 ---- */
        if (++tick % (1000 / APP_TASK_TICK_MS) == 0) {
            weather_service_tick();

            bool locked = lvgl_port_lock(200);
            if (locked) {
                ui_refresh();
                lvgl_port_unlock();
            }

            web_panel_push_state();   /* 状态变了才真推（内部去重） */
        }

        vTaskDelay(pdMS_TO_TICKS(APP_TASK_TICK_MS));
    }
}

void app_services_start(void)
{
    /* 时间服务要在 WiFi 起来后才有意义，但启动只是注册，立即调用没问题 */
    time_service_start();
    weather_service_init(APP_WEATHER_API_KEY, APP_WEATHER_CITY);

    serial_cli_init();
    app_events_init();          /* 上电握手：READY + 全量状态，方便 STM32 对齐初始状态 */

    xTaskCreatePinnedToCore(app_task, "app_services", APP_TASK_STACK, NULL, APP_TASK_PRIO, NULL,
                            ARDUINO_RUNNING_CORE);
}
