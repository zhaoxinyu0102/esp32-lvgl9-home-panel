/**
 * @file app_events.h
 * @brief 操作动作串口上报：你在界面上按了什么 / 拖了什么，就从串口发一行，给外接 STM32 用。
 *
 * ============================== 上报内容 ==============================
 * 只报"操作动作"，不报 WiFi 状态、不报心跳、不报天气轮询。所有动作都是**动作发生的那一刻**
 * 发出的，一条一行，行尾 \r\n。
 *
 * 两种风格，用 app_config.h 里的 APP_EVENTS_STYLE 选：
 *
 *   APP_EVENTS_STYLE 0（默认，中文可读，插上串口监视器就能看懂）
 *     ------------------------------------------------------------------
 *     灯光-全部打开
 *     灯光-全部关闭
 *     灯光-客厅主灯-打开
 *     灯光-客厅主灯-关闭
 *     灯光-客厅主灯-亮度 80%
 *     灯光-客厅氛围灯-颜色 FF8A3D
 *     空调-电源-开
 *     空调-目标温度 26
 *     空调-模式-制冷
 *     空调-风速-中风
 *     空调-摆风-开
 *     窗帘-客厅窗帘-打开
 *     窗帘-客厅窗帘-关闭
 *     窗帘-客厅窗帘-停止
 *     窗帘-客厅窗帘-位置 45%
 *     插座-热水器-打开
 *     场景-回家模式
 *     安防-布防
 *     传感器-门磁-触发
 *     界面-灯光
 *     背光-80%
 *     城市-hefei
 *
 *   APP_EVENTS_STYLE 1（纯 ASCII，给 STM32 解析用）
 *     ------------------------------------------------------------------
 *     LIGHT_ALL ON / LIGHT_ALL OFF
 *     LIGHT 0 ON / LIGHT 0 OFF / LIGHT 0 BRI 80 / LIGHT 0 RGB FF8A3D
 *     AC POWER ON / AC TARGET 26 / AC MODE COOL / AC FAN MID / AC SWING ON
 *     CURTAIN 0 OPEN / CURTAIN 0 CLOSE / CURTAIN 0 STOP / CURTAIN 0 POS 45
 *     SWITCH 0 ON / SWITCH 0 OFF
 *     SCENE 0
 *     COFFEE POWER 1
 *     COFFEE TYPE 2
 *     COFFEE START 2
 *     SCREEN 1
 *     BACKLIGHT 80
 *     CITY hefei
 *
 * 滑条（亮度/窗帘位置/目标温度/背光）：拖动时**最多每 150ms 发一条**，松手后一定会补发最终值，
 * 所以既跟手又不会刷屏。
 *
 * 串口命令：sync 重新发一遍当前全部状态；events 0/1 关/开上报。
 * 日志过滤：上报行以 `灯光/空调/窗帘/插座/场景/安防/传感器/界面/背光/城市`（风格 0）
 *           或 `LIGHT/AC/CURTAIN/SWITCH/SCENE/COFFEE/SCREEN/BACKLIGHT/CITY`（风格 1）开头，
 *           日志行都以 `[` 开头，STM32 忽略 `[` 开头的行即可。
 * ====================================================================
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_events_init(void);          /* 上电：一行就绪提示 */
void app_events_sync(void);          /* 重新发一遍全部状态（sync 命令用） */
void app_events_tick(void);          /* 后台任务里周期调用：补发滑条最终值 */

/* ---- 动作上报（由设备模型的 setter 调用，任何来源都会经过这里：触摸、场景、串口） ---- */
void ev_lights_all(bool on);
void ev_light(int i, bool on);
void ev_light_bright(int i, int pct);
void ev_light_color(int i, uint32_t rgb);
void ev_ac_power(bool on);
void ev_ac_target(int target);
void ev_ac_mode(int mode);
void ev_ac_fan(int fan);
void ev_ac_swing(bool on);
void ev_curtain_cmd(int i, int cmd);      /* 0 打开 / 1 停止 / 2 关闭 */
void ev_curtain_pos(int i, int pct);
void ev_switch(int i, bool on);
void ev_scene(int i);
/* ---- 咖啡机 ---- */
void ev_coffee_power(bool on);
void ev_coffee_type(int t);
void ev_coffee_param(int what, int v);   /* 0浓度 1咖啡量 2牛奶量 3水温 4杯数 */
void ev_coffee_start(void);
void ev_coffee_stage(int stage);
void ev_coffee_done(int cups);
void ev_coffee_stop(void);
void ev_coffee_alert(int what);          /* 0 缺水 / 1 缺豆 */
void ev_screen(int id);
void ev_backlight(int pct);
void ev_city(const char * city);

void app_events_set_enabled(bool on);
bool app_events_enabled(void);

/**
 * 临时静音：应用场景时会连带改一堆设备，那些"连带动作"不需要逐条上报，
 * 只需要在最后发一行"场景-回家模式"。device_model 里包住 switch 用。
 */
void app_events_set_silent(bool on);

/** 天气刷新按钮 */
void ev_weather_refresh(void);

#ifdef __cplusplus
}
#endif
