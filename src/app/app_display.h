/**
 * @file app_display.h
 * @brief 屏幕背光控制（实现在 main.cpp，用的是 ESP32_Display_Panel 的 Backlight 驱动）。
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void    app_set_backlight(uint8_t percent);   /* 0..100 */
uint8_t app_get_backlight(void);

#ifdef __cplusplus
}
#endif
