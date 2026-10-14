/**
 * @file ui_comp_navbar.h
 * @brief 底部导航栏（8 个入口，自绘 PNG 图标 + recolor 着色）。
 *
 * 句柄式：每个屏幕一份实例，set_active 时传入当前屏幕那一份。
 */

#pragma once

#include "lvgl.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t * ui_comp_navbar_create(lv_obj_t * parent);
void       ui_comp_navbar_set_active(lv_obj_t * navbar, ui_screen_id_t id);

#ifdef __cplusplus
}
#endif
