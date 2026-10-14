/**
 * @file device_model.h
 * @brief 设备状态模型：灯 / 空调 / 窗帘 / 开关设备 / 安防 / 场景 / 能耗。
 *
 * 目前是**模拟实现**（APP_SIMULATE_DEVICES=1）：能耗会累加、室温会向目标漂移、窗帘会运动，
 * 界面上的每个操作都会真实改变状态并互相联动（例如"离家模式"会关灯关空调开窗帘）。
 * 接真硬件时把 device_model.c 里的 dm_* setter 换成你的驱动调用即可，UI 一行都不用改。
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define DM_LIGHT_COUNT   6
#define DM_CURTAIN_COUNT 3
#define DM_SWITCH_COUNT  4
#define DM_SCENE_COUNT   4
#define DM_COFFEE_TYPES  4      /* 浓缩 / 瑞斯崔朵 / 拿铁 / 卡布奇诺 */
#define DM_ENERGY_DAYS   7

/* 灯光：mode 0 = 白光（用色温 ct），1 = 彩光（用 rgb） */
typedef struct {
    const char * name;
    const char * room;
    bool         on;
    uint8_t      brightness;    /* 1..100 */
    uint8_t      mode;          /* 0 白光 / 1 彩光 */
    uint16_t     ct;            /* 2700..6500 K */
    uint32_t     rgb;           /* 0xRRGGBB */
} dm_light_t;

typedef enum { DM_AC_COOL = 0, DM_AC_HEAT, DM_AC_FAN, DM_AC_DRY } dm_ac_mode_t;
typedef enum { DM_FAN_AUTO = 0, DM_FAN_LOW, DM_FAN_MID, DM_FAN_HIGH } dm_fan_t;

typedef struct {
    bool        on;
    dm_ac_mode_t mode;
    dm_fan_t     fan;
    uint8_t      target;        /* 16..30 °C */
    float        room;          /* 室内温度（模拟） */
    bool         swing;
} dm_ac_t;

/* dir: 0 停止，+1 正在打开，-1 正在关闭 */
typedef struct {
    const char * name;
    uint8_t      position;      /* 0 = 全关，100 = 全开 */
    int8_t       dir;
} dm_curtain_t;

typedef struct {
    const char * name;
    const char * symbol;        /* LVGL 内置符号 */
    bool         on;
    uint16_t     power_w;       /* 额定功率 */
} dm_switch_t;

/* ---- 咖啡机 ---- */
typedef enum {
    DM_COFFEE_ESPRESSO = 0,     /* 浓缩 */
    DM_COFFEE_RISTRETTO,        /* 瑞斯崔朵 */
    DM_COFFEE_LATTE,            /* 拿铁 */
    DM_COFFEE_CAPPUCCINO,       /* 卡布奇诺 */
} dm_coffee_type_t;

/* 萃取阶段 */
typedef enum {
    DM_BREW_IDLE = 0,           /* 待机 */
    DM_BREW_GRIND,              /* 研磨 */
    DM_BREW_COFFEE,             /* 萃取咖啡 */
    DM_BREW_MILK,               /* 打奶 */
    DM_BREW_FOAM,               /* 打奶泡 */
    DM_BREW_DONE,               /* 完成 */
} dm_brew_stage_t;

typedef struct {
    bool     power;             /* 电源 */
    uint8_t  type;              /* 咖啡类型（dm_coffee_type_t） */
    uint8_t  strength;          /* 浓度 1..5 */
    uint16_t coffee_ml;         /* 咖啡量 10..250 ml */
    uint16_t milk_ml;           /* 牛奶量 0..250 ml */
    uint8_t  temp;              /* 水温 80..96 C */
    uint8_t  cups;              /* 杯数 1..2 */
    bool     brewing;           /* 正在萃取 */
    uint8_t  stage;             /* dm_brew_stage_t */
    uint8_t  progress;          /* 0..100 */
    uint8_t  water_pct;         /* 水箱 0..100 */
    uint8_t  beans_pct;         /* 豆仓 0..100 */
    uint8_t  descalc_pct;       /* 除垢计时 0..100 */
    uint16_t cups_today;        /* 今日已做杯数 */
} dm_coffee_t;

typedef struct {
    float    today_kwh;
    float    days[DM_ENERGY_DAYS];  /* 前 6 天历史 + 最后一个是今天 */
    uint16_t power_w;               /* 当前总功率 */
    float    tariff;                /* 电价 元/kWh */
} dm_energy_t;

void dm_init(void);
void dm_tick(uint32_t dt_ms);
uint32_t dm_version(void);          /* 状态变化计数，UI 用它判断要不要重绘 */

dm_light_t *    dm_lights(void);
dm_ac_t *       dm_ac(void);
dm_curtain_t *  dm_curtains(void);
dm_switch_t *   dm_switches(void);
dm_coffee_t *   dm_coffee(void);

const char * dm_coffee_type_name(int i);
const char * dm_coffee_stage_name(int stage);
dm_energy_t *   dm_energy(void);

void dm_light_set(int i, bool on, int brightness, int mode, int ct, uint32_t rgb);
void dm_light_toggle(int i);
void dm_light_all(bool on);

void dm_ac_set_on(bool on);
void dm_ac_set_target(int target);
void dm_ac_set_mode(dm_ac_mode_t mode);
void dm_ac_set_fan(dm_fan_t fan);
void dm_ac_set_swing(bool swing);

void dm_curtain_open(int i);
void dm_curtain_close(int i);
void dm_curtain_stop(int i);
void dm_curtain_set(int i, int position);

void dm_switch_set(int i, bool on);

/* 咖啡机控制 */
void dm_coffee_set_power(bool on);
void dm_coffee_set_type(int type);
void dm_coffee_set_strength(int v);      /* 1..5 */
void dm_coffee_set_coffee_ml(int ml);    /* 10..250 */
void dm_coffee_set_milk_ml(int ml);      /* 0..250 */
void dm_coffee_set_temp(int c);          /* 80..96 */
void dm_coffee_set_cups(int n);          /* 1..2 */
void dm_coffee_start(void);              /* 开始萃取（缺水/缺豆会拒绝并提醒） */
void dm_coffee_stop(void);               /* 中止萃取 */

const char * dm_scene_name(int i);

/** 最近一次应用的场景编号（-1 表示还没应用过）；串口事件上报用 */
int dm_last_scene(void);
void dm_scene_apply(int i);

/** 外部改了状态后手动戳一下（触发 UI 刷新） */
void dm_touch(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif
