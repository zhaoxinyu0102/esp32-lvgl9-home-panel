/**
 * @file ui_Screen_Weather.h
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t * ui_Screen_Weather_create(lv_obj_t * parent);
void       ui_Screen_Weather_refresh(void);

#ifdef __cplusplus
}
#endif
