/**
 * @file ui_Screen_Dashboard.h
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t * ui_Screen_Dashboard_create(lv_obj_t * parent);
void       ui_Screen_Dashboard_refresh(void);

#ifdef __cplusplus
}
#endif
