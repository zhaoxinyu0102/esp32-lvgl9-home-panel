/**
 * @file app_log.h
 * @brief 统一日志宏（app 层的 .cpp 文件用，走 Arduino 的 Serial）。
 */

#pragma once

#include <Arduino.h>

#include "app_config.h"

#if APP_ENABLE_SERIAL_LOG
#define APP_LOG(fmt, ...)   Serial.printf("[app] " fmt "\r\n", ##__VA_ARGS__)
#else
#define APP_LOG(fmt, ...)   do { } while (0)
#endif
