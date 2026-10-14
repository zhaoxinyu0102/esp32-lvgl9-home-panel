/**
 * @file app_config.h
 * @brief 智能家居应用的编译期配置：WiFi、时间、天气、调试开关。
 *
 * 这里的值都可以改成你自己的；WiFi 也可以在屏幕上（设置页）输入并保存到 NVS，
 * 保存过之后会优先用 NVS 里的凭据。
 */

#pragma once

/* -------------------------------------------------------------------------
 * WiFi
 *
 * 留空则开机进设置页让你在屏幕上输入（会自动弹出 WiFi 配网界面）。
 * 填了就自动连（连不上也还是可以进配网界面）。
 * ---------------------------------------------------------------------- */
#define APP_WIFI_SSID               ""
#define APP_WIFI_PASSWORD           ""
#define APP_WIFI_HOSTNAME           "smart-home-panel"
#define APP_WIFI_CONNECT_TIMEOUT_MS (15000)

/* -------------------------------------------------------------------------
 * 板子自己的热点（手机直接连它就能控制，不需要家里路由器）
 *
 * AP + STA 同时工作：手机连热点访问控制面板；STA 照常连路由器取天气/时间。
 * 热点地址固定是 http://192.168.4.1
 * ---------------------------------------------------------------------- */
#define APP_AP_ENABLE               1
#define APP_AP_SSID                 "SmartHome-Panel"
#define APP_AP_PASSWORD             "12345678"   /* 至少 8 位；填空 = 开放热点 */
#define APP_AP_CHANNEL              0            /* 0 = 自动跟随 STA 信道 */
#define APP_AP_MAX_CONN             4
#define APP_AP_HIDDEN               0

/* -------------------------------------------------------------------------
 * 时间
 *
 * TZ 字符串用 POSIX 格式：中国是 "CST-8"（注意符号是反的）。
 * NTP 服务器优先用国内源，失败自动试 pool.ntp.org。
 * ---------------------------------------------------------------------- */
#define APP_TZ                      "CST-8"
#define APP_NTP_SERVER_1            "ntp.aliyun.com"
#define APP_NTP_SERVER_2            "ntp1.aliyun.com"
#define APP_NTP_SERVER_3            "pool.ntp.org"

/* -------------------------------------------------------------------------
 * 天气
 *
 * 天气数据源：APP_WEATHER_PROVIDER
 *   0 = Open-Meteo（默认，推荐）-- 完全免费、**不需要 API key**，连上 WiFi 就有真实天气
 *   1 = 心知天气 Seniverse v3（国内快，需要 key）
 *   2 = OpenWeatherMap（需要 key，建议同时填经纬度）
 *
 *   provider 1/2 且 key 留空时 -> 演示模式（本地生成合理数据，界面照常有内容）。
 *   只要有网，provider 0 就是真实数据。
 *
 *   Seniverse: https://api.seniverse.com/v3/weather/now.json?key=<KEY>&location=<城市拼音>&language=zh-Hans&unit=c
 *   OWM:       https://api.openweathermap.org/data/2.5/weather?q=<城市>&appid=<KEY>&units=metric&lang=zh_cn
 *
 * 返回解析用 ArduinoJson（工程 lib/ArduinoJson 已内置，无需联网下载）。
 * ---------------------------------------------------------------------- */
#define APP_WEATHER_API_KEY         ""                     /* provider 0 不需要填 */
#define APP_WEATHER_PROVIDER        0                      /* 0 = Open-Meteo（免 key），1 = Seniverse，2 = OWM */
#define APP_WEATHER_CITY            "hefei"                /* 天气城市：英文/拼音即可（会转成中文城市名）。
                                                          * 例：hefei 合肥 / anhui 安徽 / shanghai / beijing
                                                          * 也能在设置页或串口命令 city <名称> 里随时改（存 NVS）*/
#define APP_WEATHER_LAT             "39.9042"              /* 仅 OWM 需要（可留空用 APP_WEATHER_CITY）*/
#define APP_WEATHER_LON             "116.4074"
#define APP_WEATHER_REFRESH_MIN     20                     /* 刷新间隔（分钟）*/

/* -------------------------------------------------------------------------
 * 应用
 * ---------------------------------------------------------------------- */
#define APP_ENABLE_SERIAL_LOG       1      /* 串口打日志 */
/* 状态变化事件上报（给外接 STM32/单片机接收指令用）：协议见 src/app/app_events.h。
 * 0 = 上电不发事件（也可用串口命令 events 0/1 运行时切换） */
#define APP_EVENTS_ENABLE           1
/* 上报风格：0 = 中文可读（"灯光-全部打开"、"灯光-客厅主灯-亮度 80%"）
 *           1 = 纯 ASCII（"LIGHT_ALL ON"、"LIGHT 0 BRI 80"），STM32 解析更方便 */
#define APP_EVENTS_STYLE            0
#define APP_SIMULATE_DEVICES        1      /* 设备是模拟的；接真硬件时把这里改成 0 并在 device_model.c 里接驱动 */
#define APP_DEMO_ROOM_TEMP          24.0f  /* 演示模式下的室内温度基准 */
#define APP_DEFAULT_BRIGHTNESS      80     /* 背光亮度百分比 */

/* ---------------- 品牌信息（显示在设置页/关于） ---------------- */
#define APP_TITLE                   "智慧家居"
#define APP_SUBTITLE                "Smart Home Panel"
#define APP_VERSION                 "1.0.0"

/* -------------------------------------------------------------------------
 * 诊断开关
 *
 * APP_DIAG_PLAIN_SCREEN = 1 时：不建任何界面、不启动后台服务，只在屏幕上画一个
 * 红色纯色屏 + "DISPLAY OK"。用来判断白屏到底是
 *     显示链路（LVGL 移植层 / 面板 / 防撕裂模式）的问题，
 * 还是 UI 与服务代码的问题：
 *     - 看到红色  => 显示链路正常，问题在 UI/服务代码（继续查 UI）
 *     - 仍然白屏  => 显示链路问题（查 lvgl_v9_port 的防撕裂模式 / 面板配置）
 * 排查完记得改回 0。
 * ---------------------------------------------------------------------- */
#define APP_DIAG_PLAIN_SCREEN       0
