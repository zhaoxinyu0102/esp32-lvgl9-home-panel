/**
 * @file weather_service.h
 * @brief 天气服务：定时 HTTP 拉取 + JSON 解析；没有 API key 时走演示数据。
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_FC_DAYS 3

/* 归一化的天气类型（两边 API 都映射到这套） */
typedef enum {
    WX_CLEAR = 0,   /* 晴 */
    WX_PARTLY,      /* 多云/晴间多云 */
    WX_CLOUDY,      /* 阴 */
    WX_RAIN,        /* 雨 */
    WX_SNOW,        /* 雪 */
    WX_THUNDER,     /* 雷 */
    WX_FOG,         /* 雾/霾 */
    WX_UNKNOWN,
} weather_kind_t;

typedef struct {
    char           day[12];      /* "今天" / "周二" */
    char           text[24];     /* 中文描述 */
    weather_kind_t kind;
    int            high;
    int            low;
} weather_forecast_t;

typedef struct {
    bool               valid;
    bool               demo;            /* true = 演示数据（没配 key 或拉取失败）*/
    char               city[24];
    int                temp;
    int                feels;
    int                humidity;
    float              wind_kmh;
    char               text[24];
    weather_kind_t     kind;
    int                high;
    int                low;
    weather_forecast_t fc[WEATHER_FC_DAYS];
    int                updated_min_ago;
} weather_t;

/** 启动：api_key 传空则演示模式 */
void weather_service_init(const char * api_key, const char * city);

/** 周期调用（内部按 APP_WEATHER_REFRESH_MIN 控制真正的 HTTP 频率） */
void weather_service_tick(void);

const weather_t * weather_service_get(void);

/** 手动触发一次刷新（设置页的"立即刷新"按钮） */
void weather_service_request_refresh(void);

/**
 * 设置天气城市（英文/拼音，如 hefei / anhui），写入 NVS 并立即重新获取。
 * 设置页和串口命令 city 都走它。
 */
void weather_service_set_city(const char * city);

/** 当前天气城市（设置页输入框回显用） */
const char * weather_service_city(void);

/** 天气类型 -> LVGL 图片指针（ui_img_w_*），可直接给 lv_image_set_src */
const void * weather_icon(weather_kind_t kind);

/** 天气类型 -> 中文 */
const char * weather_kind_text(weather_kind_t kind);

#ifdef __cplusplus
}
#endif
