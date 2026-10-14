/**
 * @file device_model.c
 * @brief 设备模型实现（模拟 + 联动）。
 *
 * 换成真硬件：把各个 dm_*_set / dm_*_toggle 里的"改结构体 + dm_touch()"换成你的
 * GPIO / 继电器 / 空调 IR / 窗帘电机调用即可，其余（UI、场景联动、能耗统计）都不用动。
 */

#include "device_model.h"

#include <string.h>

#include "lvgl.h"          /* 只为 LV_SYMBOL_* 常量 */
#include "app_config.h"
#include "app_events.h"     /* 每次动作上报串口（协议见 app_events.h） */

static dm_light_t   s_lights[DM_LIGHT_COUNT];
static dm_ac_t      s_ac;
static dm_curtain_t s_curtains[DM_CURTAIN_COUNT];
static dm_switch_t  s_switches[DM_SWITCH_COUNT];
static dm_coffee_t   s_coffee;
static dm_energy_t  s_energy;
static const char * s_scene_names[DM_SCENE_COUNT] = { "回家模式", "离家模式", "睡眠模式", "影院模式" };

static uint32_t s_version = 1;

void dm_touch(void)
{
    s_version++;
}

uint32_t dm_version(void)
{
    return s_version;
}

dm_light_t *    dm_lights(void)    { return s_lights; }
dm_ac_t *       dm_ac(void)        { return &s_ac; }
dm_curtain_t *  dm_curtains(void)  { return s_curtains; }
dm_switch_t *   dm_switches(void)  { return s_switches; }
dm_coffee_t *   dm_coffee(void)    { return &s_coffee; }
dm_energy_t *   dm_energy(void)    { return &s_energy; }
static int      s_last_scene = -1;

const char *    dm_scene_name(int i) { return (i >= 0 && i < DM_SCENE_COUNT) ? s_scene_names[i] : ""; }
int             dm_last_scene(void) { return s_last_scene; }

/* -------------------------------------------------------------------------
 * 初始化（给一份合理的初始状态，开箱就有内容看）
 * ---------------------------------------------------------------------- */
void dm_init(void)
{
    struct { const char * name; const char * room; bool on; uint8_t bri; uint8_t mode; uint16_t ct; uint32_t rgb; } lights[DM_LIGHT_COUNT] = {
        { "客厅主灯",   "客厅", true,  80, 0, 4000, 0xFFF3E0 },
        { "餐厅吊灯",   "餐厅", false, 60, 0, 3500, 0xFFE7C4 },
        { "卧室床头",   "主卧", false, 35, 0, 3000, 0xFFD9A0 },
        { "书房台灯",   "书房", true,  70, 0, 5000, 0xFFFFFF },
        { "玄关射灯",   "玄关", false, 50, 0, 4000, 0xFFF3E0 },
        { "客厅氛围灯", "客厅", true,  65, 1, 4000, 0x4F8CFF },
    };
    for (int i = 0; i < DM_LIGHT_COUNT; i++) {
        s_lights[i].name = lights[i].name;
        s_lights[i].room = lights[i].room;
        s_lights[i].on = lights[i].on;
        s_lights[i].brightness = lights[i].bri;
        s_lights[i].mode = lights[i].mode;
        s_lights[i].ct = lights[i].ct;
        s_lights[i].rgb = lights[i].rgb;
    }

    s_ac.on = false;
    s_ac.mode = DM_AC_COOL;
    s_ac.fan = DM_FAN_AUTO;
    s_ac.target = 25;
    s_ac.room = APP_DEMO_ROOM_TEMP;
    s_ac.swing = false;

    const char * curtain_names[DM_CURTAIN_COUNT] = { "客厅窗帘", "主卧窗帘", "书房窗帘" };
    const uint8_t curtain_pos[DM_CURTAIN_COUNT] = { 100, 0, 40 };
    for (int i = 0; i < DM_CURTAIN_COUNT; i++) {
        s_curtains[i].name = curtain_names[i];
        s_curtains[i].position = curtain_pos[i];
        s_curtains[i].dir = 0;
    }

    struct { const char * name; const char * sym; bool on; uint16_t w; } switches[DM_SWITCH_COUNT] = {
        { "空气净化器", LV_SYMBOL_REFRESH, true,  45  },
        { "加湿器",     LV_SYMBOL_TINT,    false, 30  },
        { "热水器",     LV_SYMBOL_CHARGE,  true,  1500 },
        { "智能插座",   LV_SYMBOL_POWER,   false, 120 },
    };
    for (int i = 0; i < DM_SWITCH_COUNT; i++) {
        s_switches[i].name = switches[i].name;
        s_switches[i].symbol = switches[i].sym;
        s_switches[i].on = switches[i].on;
        s_switches[i].power_w = switches[i].w;
    }

    /* 咖啡机初始状态 */
    s_coffee.power = true;
    s_coffee.type = DM_COFFEE_LATTE;
    s_coffee.strength = 3;
    s_coffee.coffee_ml = 60;
    s_coffee.milk_ml = 150;
    s_coffee.temp = 92;
    s_coffee.cups = 1;
    s_coffee.brewing = false;
    s_coffee.stage = DM_BREW_IDLE;
    s_coffee.progress = 0;
    s_coffee.water_pct = 82;
    s_coffee.beans_pct = 64;
    s_coffee.descalc_pct = 25;
    s_coffee.cups_today = 0;

    /* 能耗：给 7 天历史 + 今天一个真实感的数据 */
    static const float hist[DM_ENERGY_DAYS] = { 5.8f, 6.4f, 4.9f, 7.1f, 5.2f, 6.8f, 4.2f };
    memcpy(s_energy.days, hist, sizeof(hist));
    s_energy.today_kwh = s_energy.days[DM_ENERGY_DAYS - 1];
    s_energy.tariff = 0.558f;
    s_energy.power_w = 0;

    s_version++;
}

/* -------------------------------------------------------------------------
 * 每秒推进：能耗累加、室温漂移、窗帘运动
 * ---------------------------------------------------------------------- */
static uint16_t calc_power(void)
{
    uint32_t w = 0;

    for (int i = 0; i < DM_LIGHT_COUNT; i++) {
        if (s_lights[i].on) {
            w += 3 + (uint32_t)s_lights[i].brightness * 9 / 100;   /* 3..12W */
        }
    }
    for (int i = 0; i < DM_SWITCH_COUNT; i++) {
        if (s_switches[i].on) {
            w += s_switches[i].power_w;
        }
    }
    if (s_ac.on) {
        w += (s_ac.mode == DM_AC_FAN) ? 60 : 900;
    }
    for (int i = 0; i < DM_CURTAIN_COUNT; i++) {
        if (s_curtains[i].dir != 0) {
            w += 40;
        }
    }
    return (uint16_t)w;
}

void dm_tick(uint32_t dt_ms)
{
    bool changed = false;

    /* 窗帘运动 */
    for (int i = 0; i < DM_CURTAIN_COUNT; i++) {
        if (s_curtains[i].dir == 0) {
            continue;
        }
        int step = (int)(s_curtains[i].dir) * (int)(dt_ms / 40);   /* 约 4 秒走完全程 */
        int pos = (int)s_curtains[i].position + step;
        if (pos >= 100) {
            pos = 100;
            s_curtains[i].dir = 0;
        } else if (pos <= 0) {
            pos = 0;
            s_curtains[i].dir = 0;
        }
        s_curtains[i].position = (uint8_t)pos;
        changed = true;
    }

    /* 室温向目标漂移（空调开）/ 向室外温度漂移（关） */
    float before = s_ac.room;
    if (s_ac.on && s_ac.mode != DM_AC_FAN) {
        float target = (float)s_ac.target;
        s_ac.room += (target - s_ac.room) * (float)dt_ms / 45000.0f;
    } else {
        s_ac.room += (26.5f - s_ac.room) * (float)dt_ms / 240000.0f;
    }
    if (s_ac.room - before > 0.03f || before - s_ac.room > 0.03f) {
        changed = true;
    }

    /* 能耗累加 */
    s_energy.power_w = calc_power();
    s_energy.today_kwh += (float)s_energy.power_w * ((float)dt_ms / 1000.0f) / 3600000.0f;
    s_energy.days[DM_ENERGY_DAYS - 1] = s_energy.today_kwh;

    /* 咖啡机萃取流程：研磨 -> 冲泡咖啡 -> 打奶 -> 奶泡 -> 完成（约 8 秒） */
    if (s_coffee.brewing) {
        uint32_t step = dt_ms / 25;
        uint32_t np = (uint32_t)s_coffee.progress + step;
        s_coffee.progress = (uint8_t)(np > 100 ? 100 : np);

        uint8_t p = s_coffee.progress;
        bool milk = (s_coffee.type == DM_COFFEE_LATTE || s_coffee.type == DM_COFFEE_CAPPUCCINO);
        uint8_t stage;
        if (p < 12)                    stage = DM_BREW_GRIND;
        else if (p < 70)               stage = DM_BREW_COFFEE;
        else if (milk && p < 88)       stage = DM_BREW_MILK;
        else if (milk && p < 100)      stage = DM_BREW_FOAM;
        else                           stage = DM_BREW_DONE;

        if (stage != s_coffee.stage) {
            s_coffee.stage = stage;
            ev_coffee_stage((int)stage);
        }

        if (p >= 100) {
            uint16_t use_w = (uint16_t)((s_coffee.coffee_ml + s_coffee.milk_ml) * s_coffee.cups / 20);
            uint8_t  use_b = (uint8_t)(s_coffee.cups * 2);
            s_coffee.brewing = false;
            s_coffee.cups_today = (uint16_t)(s_coffee.cups_today + s_coffee.cups);
            s_coffee.water_pct = (uint8_t)((s_coffee.water_pct > use_w) ? (s_coffee.water_pct - use_w) : 0);
            s_coffee.beans_pct = (uint8_t)((s_coffee.beans_pct > use_b) ? (s_coffee.beans_pct - use_b) : 0);
            ev_coffee_done((int)s_coffee.cups);
        }
        changed = true;
    }

    if (changed) {
        dm_touch();
    }
}

/* -------------------------------------------------------------------------
 * 灯光
 * ---------------------------------------------------------------------- */
void dm_light_set(int i, bool on, int brightness, int mode, int ct, uint32_t rgb)
{
    if (i < 0 || i >= DM_LIGHT_COUNT) {
        return;
    }
    dm_light_t * l = &s_lights[i];

    bool on_changed = (l->on != on);
    l->on = on;

    if (brightness >= 1) {
        uint8_t b = (uint8_t)((brightness > 100) ? 100 : brightness);
        if (b != l->brightness) {
            l->brightness = b;
            ev_light_bright(i, b);          /* 滑条：发百分比 */
        }
    }
    l->mode = (uint8_t)(mode ? 1 : 0);
    if (ct > 0) {
        l->ct = (uint16_t)ct;
    }
    if (rgb != 0 && rgb != l->rgb) {
        l->rgb = rgb;
        ev_light_color(i, rgb);
    }
    if (on_changed) {
        ev_light(i, on);
    }
    dm_touch();
}

void dm_light_toggle(int i)
{
    if (i < 0 || i >= DM_LIGHT_COUNT) {
        return;
    }
    s_lights[i].on = !s_lights[i].on;
    ev_light(i, s_lights[i].on);
    dm_touch();
}

void dm_light_all(bool on)
{
    ev_lights_all(on);                      /* "全部打开/全部关闭"整条上报 */
    for (int i = 0; i < DM_LIGHT_COUNT; i++) {
        s_lights[i].on = on;
    }
    dm_touch();
}

/* -------------------------------------------------------------------------
 * 空调
 * ---------------------------------------------------------------------- */
void dm_ac_set_on(bool on)          { s_ac.on = on; ev_ac_power(on); dm_touch(); }
void dm_ac_set_target(int target)
{
    if (target < 16) target = 16;
    if (target > 30) target = 30;
    s_ac.target = (uint8_t)target;
    ev_ac_target(target);
    dm_touch();
}
void dm_ac_set_mode(dm_ac_mode_t m) { s_ac.mode = m; ev_ac_mode((int)m); dm_touch(); }
void dm_ac_set_fan(dm_fan_t f)      { s_ac.fan = f; ev_ac_fan((int)f); dm_touch(); }
void dm_ac_set_swing(bool s)        { s_ac.swing = s; ev_ac_swing(s); dm_touch(); }

/* -------------------------------------------------------------------------
 * 窗帘
 * ---------------------------------------------------------------------- */
void dm_curtain_open(int i)
{
    if (i < 0 || i >= DM_CURTAIN_COUNT) return;
    s_curtains[i].dir = 1;
    ev_curtain_cmd(i, 0);
    dm_touch();
}

void dm_curtain_close(int i)
{
    if (i < 0 || i >= DM_CURTAIN_COUNT) return;
    s_curtains[i].dir = -1;
    ev_curtain_cmd(i, 2);
    dm_touch();
}

void dm_curtain_stop(int i)
{
    if (i < 0 || i >= DM_CURTAIN_COUNT) return;
    s_curtains[i].dir = 0;
    ev_curtain_cmd(i, 1);
    dm_touch();
}

void dm_curtain_set(int i, int position)
{
    if (i < 0 || i >= DM_CURTAIN_COUNT) return;
    if (position < 0) position = 0;
    if (position > 100) position = 100;
    s_curtains[i].position = (uint8_t)position;
    s_curtains[i].dir = 0;
    ev_curtain_pos(i, position);        /* 滑条：发百分比 */
    dm_touch();
}

/* -------------------------------------------------------------------------
 * 开关设备 / 安防
 * ---------------------------------------------------------------------- */
void dm_switch_set(int i, bool on)
{
    if (i < 0 || i >= DM_SWITCH_COUNT) return;
    s_switches[i].on = on;
    ev_switch(i, on);
    dm_touch();
}

/* -------------------------------------------------------------------------
 * 咖啡机
 * ---------------------------------------------------------------------- */
static const char * COFFEE_TYPE_NAME[DM_COFFEE_TYPES] = { "浓缩", "瑞斯崔朵", "拿铁", "卡布奇诺" };

const char * dm_coffee_type_name(int i)
{
    return (i >= 0 && i < DM_COFFEE_TYPES) ? COFFEE_TYPE_NAME[i] : "咖啡";
}

const char * dm_coffee_stage_name(int stage)
{
    switch (stage) {
    case DM_BREW_GRIND:  return "研磨中";
    case DM_BREW_COFFEE: return "冲泡咖啡";
    case DM_BREW_MILK:   return "打奶中";
    case DM_BREW_FOAM:   return "打奶泡";
    case DM_BREW_DONE:   return "完成";
    default:             return "待机";
    }
}

void dm_coffee_set_power(bool on)
{
    s_coffee.power = on;
    if (!on && s_coffee.brewing) {
        s_coffee.brewing = false;
        s_coffee.stage = DM_BREW_IDLE;
        s_coffee.progress = 0;
    }
    ev_coffee_power(on);
    dm_touch();
}

void dm_coffee_set_type(int t)
{
    if (t < 0 || t >= DM_COFFEE_TYPES) {
        return;
    }
    s_coffee.type = (uint8_t)t;
    /* 每种咖啡给一组顺手的水量默认值 */
    if (t == DM_COFFEE_ESPRESSO)        { s_coffee.coffee_ml = 40; s_coffee.milk_ml = 0;   }
    else if (t == DM_COFFEE_RISTRETTO)  { s_coffee.coffee_ml = 25; s_coffee.milk_ml = 0;   }
    else if (t == DM_COFFEE_LATTE)      { s_coffee.coffee_ml = 60; s_coffee.milk_ml = 150; }
    else                                { s_coffee.coffee_ml = 50; s_coffee.milk_ml = 120; }
    ev_coffee_type(t);
    ev_coffee_param(1, (int)s_coffee.coffee_ml);
    ev_coffee_param(2, (int)s_coffee.milk_ml);
    dm_touch();
}

void dm_coffee_set_strength(int v)
{
    if (v < 1) v = 1;
    if (v > 5) v = 5;
    s_coffee.strength = (uint8_t)v;
    ev_coffee_param(0, v);
    dm_touch();
}

void dm_coffee_set_coffee_ml(int ml)
{
    if (ml < 10)  ml = 10;
    if (ml > 250) ml = 250;
    s_coffee.coffee_ml = (uint16_t)ml;
    ev_coffee_param(1, ml);
    dm_touch();
}

void dm_coffee_set_milk_ml(int ml)
{
    if (ml < 0)   ml = 0;
    if (ml > 250) ml = 250;
    s_coffee.milk_ml = (uint16_t)ml;
    ev_coffee_param(2, ml);
    dm_touch();
}

void dm_coffee_set_temp(int c)
{
    if (c < 80) c = 80;
    if (c > 96) c = 96;
    s_coffee.temp = (uint8_t)c;
    ev_coffee_param(3, c);
    dm_touch();
}

void dm_coffee_set_cups(int n)
{
    if (n < 1) n = 1;
    if (n > 2) n = 2;
    s_coffee.cups = (uint8_t)n;
    ev_coffee_param(4, n);
    dm_touch();
}

void dm_coffee_start(void)
{
    if (s_coffee.brewing) {
        return;
    }
    if (!s_coffee.power) {
        s_coffee.power = true;
        ev_coffee_power(true);
    }
    if (s_coffee.water_pct == 0) {
        ev_coffee_alert(0);
        return;
    }
    if (s_coffee.beans_pct == 0) {
        ev_coffee_alert(1);
        return;
    }
    s_coffee.brewing = true;
    s_coffee.progress = 0;
    s_coffee.stage = DM_BREW_GRIND;
    ev_coffee_start();
    dm_touch();
}

void dm_coffee_stop(void)
{
    if (!s_coffee.brewing) {
        return;
    }
    s_coffee.brewing = false;
    s_coffee.stage = DM_BREW_IDLE;
    s_coffee.progress = 0;
    ev_coffee_stop();
    dm_touch();
}

/* -------------------------------------------------------------------------
 * 场景联动
 * ---------------------------------------------------------------------- */
void dm_scene_apply(int i)
{
    if (i >= 0 && i < DM_SCENE_COUNT) {
        s_last_scene = i;       /* 串口事件上报"最近应用的场景" */
    }

    /* 场景会连带改一堆设备（灯/空调/窗帘/布防...），这些"连带动作"不逐条上报：
     * 静音包住整个 switch，最后只发一行"场景-回家模式"。 */
    app_events_set_silent(true);    switch (i) {
    case 0:     /* 回家模式：灯全开、空调开、窗帘开 */
        dm_light_all(true);
        s_ac.on = true;
        s_ac.mode = DM_AC_COOL;
        s_ac.target = 25;
        for (int k = 0; k < DM_CURTAIN_COUNT; k++) {
            s_curtains[k].dir = 1;
        }
        s_coffee.power = true;              /* 回家开机，随时来一杯 */
        break;

    case 1:     /* 离家模式：灯全关、空调关、窗帘关、咖啡机待机 */
        dm_light_all(false);
        s_ac.on = false;
        for (int k = 0; k < DM_CURTAIN_COUNT; k++) {
            s_curtains[k].dir = -1;
        }
        for (int k = 0; k < DM_SWITCH_COUNT; k++) {
            if (strcmp(s_switches[k].name, "热水器") != 0) {
                s_switches[k].on = false;
            }
        }
        s_coffee.power = false;
        s_coffee.brewing = false;
        s_coffee.stage = DM_BREW_IDLE;
        break;

    case 2:     /* 睡眠模式：只留卧室床头 15%、空调 26 制热关、窗帘关、布防 */
        for (int k = 0; k < DM_LIGHT_COUNT; k++) {
            s_lights[k].on = (strcmp(s_lights[k].room, "主卧") == 0);
            if (s_lights[k].on) {
                s_lights[k].brightness = 15;
                s_lights[k].ct = 2700;
                s_lights[k].mode = 0;
            }
        }
        s_ac.on = true;
        s_ac.mode = DM_AC_COOL;
        s_ac.target = 26;
        s_ac.fan = DM_FAN_LOW;
        for (int k = 0; k < DM_CURTAIN_COUNT; k++) {
            s_curtains[k].dir = -1;
        }
        s_coffee.power = false;
        s_coffee.brewing = false;
        s_coffee.stage = DM_BREW_IDLE;
        break;

    default:    /* 影院模式：氛围灯紫 40%、其他关、窗帘关、空调 24 送风 */
        for (int k = 0; k < DM_LIGHT_COUNT; k++) {
            s_lights[k].on = (strcmp(s_lights[k].name, "客厅氛围灯") == 0);
            if (s_lights[k].on) {
                s_lights[k].brightness = 40;
                s_lights[k].mode = 1;
                s_lights[k].rgb = 0x8B5CF6;
            }
        }
        s_ac.on = true;
        s_ac.mode = DM_AC_FAN;
        for (int k = 0; k < DM_CURTAIN_COUNT; k++) {
            s_curtains[k].dir = -1;
        }
        break;
    }

    /* 开关设备在"回家"时打开净化器 */
    if (i == 0) {
        for (int k = 0; k < DM_SWITCH_COUNT; k++) {
            if (strcmp(s_switches[k].name, "空气净化器") == 0) {
                s_switches[k].on = true;
            }
        }
    }

    app_events_set_silent(false);
    ev_scene(i);                /* 只发这一行：场景-回家模式（不拆成各设备动作） */

    dm_touch();
}
