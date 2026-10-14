/**
 * @file ui_Screen_Coffee.h
 * @brief 咖啡机界面（替换原「安防」屏）。
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t * ui_Screen_Coffee_create(lv_obj_t * parent);
void       ui_Screen_Coffee_refresh(void);

#ifdef __cplusplus
}
#endif
