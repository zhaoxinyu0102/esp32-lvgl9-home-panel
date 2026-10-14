/**
 * @file time_service.cpp
 */

#include "time_service.h"

#include <Arduino.h>        /* configTzTime */

#include <stdio.h>
#include <sys/time.h>

#include "esp_sntp.h"
#include "app_config.h"

static bool s_synced = false;
static bool s_started = false;

static const char * WEEK_CN[7] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };

static void on_sync(struct timeval * tv)
{
    (void)tv;
    s_synced = true;
}

void time_service_start(void)
{
    if (s_started) {
        return;
    }
    s_started = true;

    /* 用 POSIX TZ 字符串设置时区 + 启动 SNTP；configTzTime 会顺带 setenv("TZ") */
    configTzTime(APP_TZ, APP_NTP_SERVER_1, APP_NTP_SERVER_2, APP_NTP_SERVER_3);

    esp_sntp_set_time_sync_notification_cb(on_sync);
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);

    /* 已同步过（例如刚重连）就直接标记 */
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        s_synced = true;
    }
}

bool time_service_synced(void)
{
    if (!s_synced && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        s_synced = true;
    }
    return s_synced;
}

void time_service_now(struct tm * out)
{
    time_t now = 0;
    time(&now);
    localtime_r(&now, out);
}

void time_service_format(char * time_buf, size_t time_len,
                         char * date_buf, size_t date_len,
                         char * week_buf, size_t week_len)
{
    struct tm t = { 0 };
    time_service_now(&t);

    snprintf(time_buf, time_len, "%02d:%02d", t.tm_hour, t.tm_min);
    snprintf(date_buf, date_len, "%d月%d日", t.tm_mon + 1, t.tm_mday);
    if (week_buf != NULL && week_len > 0) {
        if (t.tm_wday >= 0 && t.tm_wday < 7) {
            snprintf(week_buf, week_len, "%s", WEEK_CN[t.tm_wday]);
        } else {
            week_buf[0] = '\0';
        }
    }
}
