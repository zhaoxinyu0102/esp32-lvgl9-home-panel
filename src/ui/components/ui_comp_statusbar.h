/**
 * @file ui_comp_statusbar.h
 * @brief 顶部状态栏（时间 / 日期 / WiFi / 天气）。
 *
 * 每个屏幕各有一份实例，所以 API 都是"传句柄"的；句柄就是 create() 的返回值。
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t * ui_comp_statusbar_create(lv_obj_t * parent);

/** "14:32" / "10月12日" / "周日"（week 可为 NULL） */
void ui_comp_statusbar_set_time(lv_obj_t * bar, const char * time_text, const char * date_text, const char * week_text);

void ui_comp_statusbar_set_wifi(lv_obj_t * bar, bool connected, int8_t rssi);

/** icon_src 传 &ui_img_w_sunny 之类，可为 NULL */
void ui_comp_statusbar_set_weather(lv_obj_t * bar, const void * icon_src, const char * temp_text, const char * desc);

#ifdef __cplusplus
}
#endif
