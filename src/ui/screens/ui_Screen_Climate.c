/**
 * @file ui_Screen_Climate.c
 * @brief 空调温控：大圆弧目标温度 + 模式/风速/摆风。
 */

#include "ui_Screen_Climate.h"

#include "ui.h"
#include "ui_theme.h"

#include "device_model.h"

static lv_obj_t * s_power;
static lv_obj_t * s_arc;
static lv_obj_t * s_target;
static lv_obj_t * s_room;
static lv_obj_t * s_mode_btn[4];
static lv_obj_t * s_fan_btn[4];
static lv_obj_t * s_swing;
static lv_obj_t * s_power_chip;
static bool       s_updating = false;

static const char * MODE_CN[4] = { "制冷", "制热", "送风", "除湿" };
static const char * MODE_ICON[4] = { LV_SYMBOL_MINUS, LV_SYMBOL_PLUS, LV_SYMBOL_REFRESH, LV_SYMBOL_TINT };
static const char * FAN_CN[4] = { "自动", "低风", "中风", "高风" };

static void power_cb(lv_event_t * e)
{
    dm_ac_set_on(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    ui_Screen_Climate_refresh();
}

static void arc_cb(lv_event_t * e)
{
    if (s_updating) {
        return;
    }
    dm_ac_set_target((int)lv_arc_get_value(lv_event_get_target(e)));
    ui_label_set_fmt_if_changed(s_target, "%d", (int)lv_arc_get_value(lv_event_get_target(e)));
}

static void mode_cb(lv_event_t * e)
{
    dm_ac_set_mode((dm_ac_mode_t)(int)(intptr_t)lv_event_get_user_data(e));
    ui_Screen_Climate_refresh();
}

static void fan_cb(lv_event_t * e)
{
    dm_ac_set_fan((dm_fan_t)(int)(intptr_t)lv_event_get_user_data(e));
    ui_Screen_Climate_refresh();
}

static void swing_cb(lv_event_t * e)
{
    dm_ac_set_swing(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void temp_step_cb(lv_event_t * e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    dm_ac_set_target(dm_ac()->target + delta);
    ui_Screen_Climate_refresh();
}

lv_obj_t * ui_Screen_Climate_create(lv_obj_t * parent)
{
    lv_obj_t * wrap = ui_row(parent);
    lv_obj_set_width(wrap, LV_PCT(100));
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* ---------------- 左：圆弧温控 ---------------- */
    lv_obj_t * dial_card = ui_card(wrap, 0x1B2547, UI_C_CARD_2);
    lv_obj_set_width(dial_card, 360);
    lv_obj_set_height(dial_card, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(dial_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * dial = lv_obj_create(dial_card);
    ui_transparent(dial);
    lv_obj_set_size(dial, 240, 240);

    s_arc = lv_arc_create(dial);
    lv_obj_set_size(s_arc, 232, 232);
    lv_obj_center(s_arc);
    lv_arc_set_rotation(s_arc, 135);
    lv_arc_set_bg_angles(s_arc, 0, 270);
    lv_arc_set_range(s_arc, 16, 30);
    lv_arc_set_value(s_arc, dm_ac()->target);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(s_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(UI_C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_arc, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(UI_C_ACCENT), LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_arc, arc_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * center = ui_col(dial);
    lv_obj_set_width(center, LV_PCT(100));
    lv_obj_align(center, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_target = ui_label(center, "--", UI_FONT_NUM_XL, UI_C_TEXT);
    ui_label(center, "目标温度 °C", UI_FONT_CN, UI_C_TEXT_DIM);
    s_room = ui_label(center, "室温 --.-°C", UI_FONT_CN, UI_C_TEXT_DIM);

    lv_obj_t * step_row = ui_row(dial_card);
    lv_obj_set_style_pad_column(step_row, UI_GAP, 0);
    lv_obj_t * minus = ui_button(step_row, "- 降低", UI_C_BORDER, temp_step_cb, (void *)(intptr_t)-1);
    lv_obj_t * plus = ui_button(step_row, "升高  +", UI_C_ACCENT, temp_step_cb, (void *)(intptr_t)1);
    lv_obj_set_width(minus, 150);
    lv_obj_set_width(plus, 150);

    /* ---------------- 右：开关/模式/风速 ---------------- */
    lv_obj_t * right = ui_col(wrap);
    lv_obj_set_width(right, 372);
    lv_obj_set_style_pad_row(right, UI_GAP, 0);

    lv_obj_t * power_card = ui_card(right, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_width(power_card, LV_PCT(100));
    lv_obj_set_height(power_card, 88);
    lv_obj_set_flex_flow(power_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(power_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * pc = ui_col(power_card);
    ui_label(pc, "空调电源", UI_FONT_CN, UI_C_TEXT);
    s_power_chip = ui_label(pc, "已关闭", UI_FONT_SMALL, UI_C_TEXT_MUTE);
    s_power = ui_toggle(power_card, dm_ac()->on, power_cb, NULL);

    lv_obj_t * mode_card = ui_card(right, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_width(mode_card, LV_PCT(100));
    lv_obj_set_height(mode_card, 148);
    ui_label(mode_card, "运行模式", UI_FONT_CN, UI_C_TEXT_DIM);

    lv_obj_t * modes = ui_row(mode_card);
    lv_obj_set_style_pad_column(modes, 8, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t * b = lv_obj_create(modes);
        ui_transparent(b);
        lv_obj_set_size(b, 78, 62);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_10, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_C_TEXT), 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_icon(b, MODE_ICON[i], UI_FONT_BODY, UI_C_TEXT_DIM);
        ui_label(b, MODE_CN[i], UI_FONT_CN, UI_C_TEXT_DIM);
        s_mode_btn[i] = b;
    }

    lv_obj_t * fan_card = ui_card(right, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_width(fan_card, LV_PCT(100));
    lv_obj_set_height(fan_card, 120);
    ui_label(fan_card, "风速", UI_FONT_CN, UI_C_TEXT_DIM);

    lv_obj_t * fans = ui_row(fan_card);
    lv_obj_set_style_pad_column(fans, 8, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t * b = lv_obj_create(fans);
        ui_transparent(b);
        lv_obj_set_size(b, 78, 40);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_10, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_C_TEXT), 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, fan_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_label(b, FAN_CN[i], UI_FONT_CN, UI_C_TEXT_DIM);
        s_fan_btn[i] = b;
    }

    lv_obj_t * swing_row = ui_row(fan_card);
    lv_obj_set_flex_align(swing_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(swing_row, "上下摆风", UI_FONT_CN, UI_C_TEXT_DIM);
    s_swing = ui_toggle(swing_row, dm_ac()->swing, swing_cb, NULL);

    ui_Screen_Climate_refresh();
    return parent;
}

void ui_Screen_Climate_refresh(void)
{
    if (s_target == NULL) {
        return;
    }

    dm_ac_t * ac = dm_ac();

    s_updating = true;
    if (s_power != NULL) {
        if (ac->on) {
            lv_obj_add_state(s_power, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_power, LV_STATE_CHECKED);
        }
    }
    if (s_arc != NULL && !lv_obj_has_state(s_arc, LV_STATE_PRESSED)) {
        lv_arc_set_value(s_arc, ac->target);
    }
    if (s_swing != NULL) {
        if (ac->swing) {
            lv_obj_add_state(s_swing, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_swing, LV_STATE_CHECKED);
        }
    }
    s_updating = false;

    ui_label_set_fmt_if_changed(s_target, "%d", ac->target);
    ui_label_set_fmt_if_changed(s_room, "室温 %.1f°C", ac->room);
    ui_label_set_text_if_changed(s_power_chip, ac->on ? "运行中" : "已关闭");

    /* 样式写入只在状态真正变化时做：lv_obj_set_style_* 每次都会触发局部重绘，
     * 每秒无脑刷一遍会让界面一直重画（明显影响流畅度） */
    static int8_t last_on = -1;
    static int8_t last_mode = -1;
    static int8_t last_fan = -1;

    if (last_on != (int8_t)ac->on || last_mode != (int8_t)ac->mode) {
        last_on = (int8_t)ac->on;
        last_mode = (int8_t)ac->mode;
        lv_obj_set_style_text_color(s_power_chip, lv_color_hex(ac->on ? UI_C_OK : UI_C_TEXT_MUTE), 0);

        for (int i = 0; i < 4; i++) {
            bool on = ac->on && (int)ac->mode == i;
            lv_obj_set_style_bg_opa(s_mode_btn[i], on ? LV_OPA_30 : LV_OPA_10, 0);
            lv_obj_set_style_bg_color(s_mode_btn[i], lv_color_hex(on ? UI_C_ACCENT : UI_C_TEXT), 0);
            lv_obj_set_style_border_width(s_mode_btn[i], on ? 1 : 0, 0);
            lv_obj_set_style_border_color(s_mode_btn[i], lv_color_hex(UI_C_ACCENT), 0);
        }
    }

    if (last_fan != (int8_t)ac->fan) {
        last_fan = (int8_t)ac->fan;
        for (int i = 0; i < 4; i++) {
            bool fon = (int)ac->fan == i;
            lv_obj_set_style_bg_opa(s_fan_btn[i], fon ? LV_OPA_30 : LV_OPA_10, 0);
            lv_obj_set_style_bg_color(s_fan_btn[i], lv_color_hex(fon ? UI_C_ACCENT : UI_C_TEXT), 0);
        }
    }
}
