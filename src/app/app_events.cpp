/**
 * @file app_events.cpp
 * @brief 操作动作串口上报（协议见 app_events.h）。
 *
 * 上报挂在**设备模型的 setter** 上，所以不管是点触摸屏、拖滑条、按场景联动、还是串口命令改的，
 * 都会发出同一套动作行；界面代码一行都不用改。
 */

#include "app_events.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_display.h"
#include "device_model.h"
#include "ui.h"
#include "weather_service.h"

#define STYLE_CN   (APP_EVENTS_STYLE == 0)

static bool s_enabled = (APP_EVENTS_ENABLE != 0);
static bool s_silent = false;

/* ---------------------------------------------------------------- 基础输出 */
static void out(const char * fmt, ...)
{
    if (!s_enabled || s_silent) {
        return;
    }
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.print(buf);
    Serial.print("\r\n");
}

/* 动作同时也在屏幕上弹一条中文提示（手机操作时屏幕立刻有反馈）。
 * 尊重 s_silent：应用场景时那一串连带动作不逐条弹，只弹"场景-xxx"。 */
static void toast(const char * fmt, ...)
{
    if (!s_enabled || s_silent) {
        return;
    }
    char b[72];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    ui_toast_async(b);
}

/* ---------------------------------------------------------------- 名字表 */
static const char * AC_MODE_CN[4] = { "制冷", "制热", "送风", "除湿" };
static const char * AC_MODE_EN[4] = { "COOL", "HEAT", "FAN", "DRY" };
static const char * AC_FAN_CN[4]  = { "自动", "低风", "中风", "高风" };
static const char * AC_FAN_EN[4]  = { "AUTO", "LOW", "MID", "HIGH" };
static const char * SCREEN_CN[UI_SCREEN_COUNT] = {
    "首页", "灯光", "空调", "窗帘", "能耗", "安防", "天气", "设置"
};
static const char * SCREEN_EN[UI_SCREEN_COUNT] = {
    "HOME", "LIGHT", "AC", "CURTAIN", "ENERGY", "SECURITY", "WEATHER", "SETTINGS"
};

/* ---------------------------------------------------------------- 滑条节流 */
/* 拖动时最多每 150ms 发一条；松手后的最终值由 app_events_tick() 补发 */
#define VAL_MIN_GAP_MS  150
#define VAL_SLOTS       12

typedef struct {
    uint8_t  kind;          /* 1 灯光亮度 / 2 窗帘位置 / 3 空调目标 / 4 背光（0 = 空闲） */
    uint8_t  id;
    int      last_val;
    int      pend_val;
    bool     has_pend;
    uint32_t last_ms;
} val_slot_t;

static val_slot_t s_vals[VAL_SLOTS];

static void val_send(int kind, int id, int val);

static val_slot_t * val_slot(int kind, int id)
{
    for (int i = 0; i < VAL_SLOTS; i++) {
        if (s_vals[i].kind == (uint8_t)(kind + 1) && s_vals[i].id == (uint8_t)id) {
            return &s_vals[i];
        }
    }
    for (int i = 0; i < VAL_SLOTS; i++) {
        if (s_vals[i].kind == 0) {          /* 空闲槽 */
            s_vals[i].kind = (uint8_t)(kind + 1);
            s_vals[i].id = (uint8_t)id;
            s_vals[i].last_val = -1;
            return &s_vals[i];
        }
    }
    return &s_vals[0];
}

static void val_emit(int kind, int id, int val)
{
    val_slot_t * s = val_slot(kind, id);
    uint32_t now = millis();

    if (s->last_val < 0 || (now - s->last_ms) >= VAL_MIN_GAP_MS) {
        if (val != s->last_val) {
            val_send(kind, id, val);
            s->last_val = val;
            s->last_ms = now;
        }
        s->has_pend = false;
    } else {
        s->pend_val = val;          /* 太频繁，先记下最终值 */
        s->has_pend = true;
    }
}

static void val_send(int kind, int id, int val)
{
    if (kind == 0) {
        const char * name = (id < DM_LIGHT_COUNT) ? dm_lights()[id].name : "?";
        if (STYLE_CN) {
            out("灯光-%s-亮度 %d%%", name, val);
        } else {
            out("LIGHT %d BRI %d", id, val);
        }
    } else if (kind == 1) {
        const char * name = (id < DM_CURTAIN_COUNT) ? dm_curtains()[id].name : "?";
        if (STYLE_CN) {
            out("窗帘-%s-位置 %d%%", name, val);
        } else {
            out("CURTAIN %d POS %d", id, val);
        }
    } else if (kind == 2) {
        if (STYLE_CN) {
            out("空调-目标温度 %d", val);
        } else {
            out("AC TARGET %d", val);
        }
    } else {
        if (STYLE_CN) {
            out("背光-%d%%", val);
        } else {
            out("BACKLIGHT %d", val);
        }
    }
}

/* ================================================================= 动作上报 */

void ev_lights_all(bool on)
{
    if (STYLE_CN) {
        out("灯光-全部%s", on ? "打开" : "关闭");
    } else {
        out("LIGHT_ALL %s", on ? "ON" : "OFF");
    }
    toast("灯光 全部%s", on ? "打开" : "关闭");
}

void ev_light(int i, bool on)
{
    if (i < 0 || i >= DM_LIGHT_COUNT) {
        return;
    }
    if (STYLE_CN) {
        out("灯光-%s-%s", dm_lights()[i].name, on ? "打开" : "关闭");
    } else {
        out("LIGHT %d %s", i, on ? "ON" : "OFF");
    }
    toast("%s 已%s", dm_lights()[i].name, on ? "开启" : "关闭");
}

void ev_light_bright(int i, int pct)
{
    val_emit(0, i, pct);
}

void ev_light_color(int i, uint32_t rgb)
{
    if (i < 0 || i >= DM_LIGHT_COUNT) {
        return;
    }
    if (STYLE_CN) {
        out("灯光-%s-颜色 %06lX", dm_lights()[i].name, (unsigned long)rgb);
    } else {
        out("LIGHT %d RGB %06lX", i, (unsigned long)rgb);
    }
    toast("%s 颜色已更新", dm_lights()[i].name);
}

void ev_ac_power(bool on)
{
    if (STYLE_CN) {
        out("空调-电源-%s", on ? "开" : "关");
    } else {
        out("AC POWER %s", on ? "ON" : "OFF");
    }
    toast("空调 已%s", on ? "开启" : "关闭");
}

void ev_ac_target(int target)
{
    val_emit(2, 0, target);
}

void ev_ac_mode(int mode)
{
    if (mode < 0 || mode > 3) {
        return;
    }
    if (STYLE_CN) {
        out("空调-模式-%s", AC_MODE_CN[mode]);
    } else {
        out("AC MODE %s", AC_MODE_EN[mode]);
    }
    toast("空调 %s模式 已开启", AC_MODE_CN[mode]);
}

void ev_ac_fan(int fan)
{
    if (fan < 0 || fan > 3) {
        return;
    }
    if (STYLE_CN) {
        out("空调-风速-%s", AC_FAN_CN[fan]);
    } else {
        out("AC FAN %s", AC_FAN_EN[fan]);
    }
    toast("空调 风速 %s", AC_FAN_CN[fan]);
}

void ev_ac_swing(bool on)
{
    if (STYLE_CN) {
        out("空调-摆风-%s", on ? "开" : "关");
    } else {
        out("AC SWING %s", on ? "ON" : "OFF");
    }
    toast("空调 摆风 已%s", on ? "开启" : "关闭");
}

void ev_curtain_cmd(int i, int cmd)
{
    if (i < 0 || i >= DM_CURTAIN_COUNT) {
        return;
    }
    if (STYLE_CN) {
        out("窗帘-%s-%s", dm_curtains()[i].name,
            (cmd == 0) ? "打开" : ((cmd == 1) ? "停止" : "关闭"));
    } else {
        out("CURTAIN %d %s", i, (cmd == 0) ? "OPEN" : ((cmd == 1) ? "STOP" : "CLOSE"));
    }
    toast("%s 已%s", dm_curtains()[i].name, (cmd == 0) ? "打开" : ((cmd == 1) ? "停止" : "关闭"));
}

void ev_curtain_pos(int i, int pct)
{
    val_emit(1, i, pct);
}

void ev_switch(int i, bool on)
{
    if (i < 0 || i >= DM_SWITCH_COUNT) {
        return;
    }
    if (STYLE_CN) {
        out("插座-%s-%s", dm_switches()[i].name, on ? "打开" : "关闭");
    } else {
        out("SWITCH %d %s", i, on ? "ON" : "OFF");
    }
    toast("%s 已%s", dm_switches()[i].name, on ? "开启" : "关闭");
}

void ev_scene(int i)
{
    if (i < 0 || i >= DM_SCENE_COUNT) {
        return;
    }
    if (STYLE_CN) {
        out("场景-%s", dm_scene_name(i));
    } else {
        out("SCENE %d", i);
    }
    toast("已应用 %s", dm_scene_name(i));
}

/* ---- 咖啡机 ---- */
void ev_coffee_power(bool on)
{
    if (STYLE_CN) {
        out("咖啡机-电源-%s", on ? "开" : "关");
    } else {
        out("COFFEE POWER %d", on ? 1 : 0);
    }
    toast("咖啡机 已%s", on ? "开机" : "待机");
}

void ev_coffee_type(int t)
{
    if (STYLE_CN) {
        out("咖啡机-类型-%s", dm_coffee_type_name(t));
    } else {
        out("COFFEE TYPE %d", t);
    }
    toast("已选择 %s", dm_coffee_type_name(t));
}

void ev_coffee_param(int what, int v)
{
    const char * cn = (what == 0) ? "浓度" : (what == 1) ? "咖啡量"
                    : (what == 2) ? "牛奶量" : (what == 3) ? "水温" : "杯数";
    const char * en = (what == 0) ? "STRENGTH" : (what == 1) ? "COFFEE_ML"
                    : (what == 2) ? "MILK_ML" : (what == 3) ? "TEMP" : "CUPS";
    if (STYLE_CN) {
        if (what == 1 || what == 2) {
            out("咖啡机-%s-%dml", cn, v);
        } else {
            out("咖啡机-%s-%d", cn, v);
        }
    } else {
        out("COFFEE %s %d", en, v);
    }
    /* 只在关键参数上弹提示，避免拖动滑条刷屏 */
    if (what == 0)      toast("浓度 %d", v);
    else if (what == 3) toast("水温 %d 度", v);
    else if (what == 4) toast("杯数 x%d", v);
}

void ev_coffee_start(void)
{
    if (STYLE_CN) {
        out("咖啡机-开始-%s", dm_coffee_type_name((int)dm_coffee()->type));
    } else {
        out("COFFEE START %d", (int)dm_coffee()->type);
    }
    toast("开始制作 %s", dm_coffee_type_name((int)dm_coffee()->type));
}

void ev_coffee_stage(int stage)
{
    if (STYLE_CN) {
        out("咖啡机-阶段-%s", dm_coffee_stage_name(stage));
    } else {
        out("COFFEE STAGE %d", stage);
    }
}

void ev_coffee_done(int cups)
{
    if (STYLE_CN) {
        out("咖啡机-完成-%d杯", cups);
    } else {
        out("COFFEE DONE %d", cups);
    }
    toast("%s 好了，请取杯", dm_coffee_type_name((int)dm_coffee()->type));
}

void ev_coffee_stop(void)
{
    if (STYLE_CN) {
        out("咖啡机-停止");
    } else {
        out("COFFEE STOP");
    }
    toast("已停止冲泡");
}

void ev_coffee_alert(int what)
{
    if (STYLE_CN) {
        out("咖啡机-提醒-%s", (what == 0) ? "请加水" : "请加咖啡豆");
    } else {
        out("COFFEE ALERT %d", what);
    }
    toast("%s", (what == 0) ? "水箱没水了，请加水" : "咖啡豆没了，请加豆");
}

void ev_screen(int id)
{
    if (id < 0 || id >= UI_SCREEN_COUNT) {
        return;
    }
    if (STYLE_CN) {
        out("界面-%s", SCREEN_CN[id]);
    } else {
        out("SCREEN %s", SCREEN_EN[id]);
    }
}

void ev_backlight(int pct)
{
    val_emit(3, 0, pct);
}

void ev_city(const char * city)
{
    if (city == NULL || city[0] == '\0') {
        return;
    }
    if (STYLE_CN) {
        out("城市-%s", city);
    } else {
        out("CITY %s", city);
    }
    toast("天气城市已设为 %s", city);
}

void ev_weather_refresh(void)
{
    if (STYLE_CN) {
        out("天气-刷新");
    } else {
        out("WEATHER REFRESH");
    }
    toast("正在刷新天气...");
}

/* ================================================================= 对外接口 */

void app_events_set_enabled(bool on) { s_enabled = on; }
bool app_events_enabled(void)        { return s_enabled; }
void app_events_set_silent(bool on)  { s_silent = on; }

void app_events_init(void)
{
    if (STYLE_CN) {
        out("系统就绪");
    } else {
        out("READY %s", APP_VERSION);
    }
}

void app_events_sync(void)
{
    if (!s_enabled) {
        return;
    }

    out(STYLE_CN ? "--当前状态--" : "SYNC_BEGIN");

    for (int i = 0; i < DM_LIGHT_COUNT; i++) {
        ev_light(i, dm_lights()[i].on);
        if (dm_lights()[i].on) {
            val_send(0, i, dm_lights()[i].brightness);
        }
    }
    ev_ac_power(dm_ac()->on);
    val_send(2, 0, dm_ac()->target);
    ev_ac_mode((int)dm_ac()->mode);
    ev_ac_fan((int)dm_ac()->fan);
    ev_ac_swing(dm_ac()->swing);

    for (int i = 0; i < DM_CURTAIN_COUNT; i++) {
        val_send(1, i, dm_curtains()[i].position);
    }
    for (int i = 0; i < DM_SWITCH_COUNT; i++) {
        ev_switch(i, dm_switches()[i].on);
    }
    ev_coffee_power(dm_coffee()->power);
    ev_coffee_type((int)dm_coffee()->type);
    ev_coffee_param(0, (int)dm_coffee()->strength);
    ev_coffee_param(1, (int)dm_coffee()->coffee_ml);
    ev_coffee_param(2, (int)dm_coffee()->milk_ml);
    ev_coffee_param(3, (int)dm_coffee()->temp);
    ev_coffee_param(4, (int)dm_coffee()->cups);
    if (dm_last_scene() >= 0) {
        ev_scene(dm_last_scene());
    }

    out(STYLE_CN ? "--状态结束--" : "SYNC_END");
}

void app_events_tick(void)
{
    if (!s_enabled) {
        return;
    }
    /* 补发拖动过程中被节流掉的最终值 */
    uint32_t now = millis();
    for (int i = 0; i < VAL_SLOTS; i++) {
        val_slot_t * s = &s_vals[i];
        if (s->kind != 0 && s->has_pend && (now - s->last_ms) >= VAL_MIN_GAP_MS) {
            val_send(s->kind - 1, s->id, s->pend_val);
            s->last_val = s->pend_val;
            s->last_ms = now;
            s->has_pend = false;
        }
    }
}
