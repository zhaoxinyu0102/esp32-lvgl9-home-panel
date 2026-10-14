/**
 * @file time_service.h
 * @brief SNTP 时间服务：同步网络时间并按本地时区格式化。
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 启动 SNTP（不阻塞；同步成功后 time_service_synced() 变 true） */
void time_service_start(void);

bool time_service_synced(void);

/** 取本地时间（未同步时返回开机以来的近似时间，界面不会显示乱码） */
void time_service_now(struct tm * out);

/** 分别格式化：time "14:32" / date "10月12日" / week "周日" */
void time_service_format(char * time_buf, size_t time_len,
                         char * date_buf, size_t date_len,
                         char * week_buf, size_t week_len);

#ifdef __cplusplus
}
#endif
