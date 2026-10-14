/**
 * @file ui_Screen_Coffee.c
 * @brief 咖啡机界面（替换原「安防」屏）——屏内分两个子界面。
 *
 *   子界面 1「选咖啡」：左边大图（自适应等比填充），右边 2x2 类型 + 名称/阶段 + 进度条 + 开始/停止
 *   子界面 2「参数」  ：左右两列各两条滑条（咖啡量/牛奶量、浓度/水温）+ 底部杯数与耗材
 *
 * 之前一屏塞满导致：图被裁成一条、水温滑条被挤出可视区（卡片不滚动，超出即裁）。
 * 现在拆成两页，每页都有充足空间；图片用 LV_IMAGE_ALIGN_CONTAIN 自动等比缩放，不再手工算 zoom。
 *
 * 素材取自 lvgl_v9_coffee 工程（src/ui/images/coffee/），文案全中文。
 */

#include "ui_Screen_Coffee.h"

#include <stdarg.h>
#include <stdio.h>

#include "device_model.h"
#include "images/coffee/ui_coffee_images.h"
#include "ui.h"
#include "ui_theme.h"

/* 素材：4 种咖啡（卡布奇诺复用拿铁图） */
static const void * const TYPE_IMG[DM_COFFEE_TYPES] = {
    &ui_img_big_espresso_png,
    &ui_img_big_ristretto_png,
    &ui_img_big_latte_png,
    &ui_img_big_latte_png,
};
static const char * const TYPE_NAME[DM_COFFEE_TYPES] = { "浓缩", "瑞斯崔朵", "拿铁", "卡布奇诺" };

static lv_obj_t * s_power;
static lv_obj_t * s_tab[2];        /* 选咖啡 / 参数 */
static lv_obj_t * s_page[2];       /* 两个子界面 */
static lv_obj_t * s_type[DM_COFFEE_TYPES];
static lv_obj_t * s_hero;
static lv_obj_t * s_name;
static lv_obj_t * s_chip_stage;
static lv_obj_t * s_bar;
static lv_obj_t * s_start;
static lv_obj_t * s_cups[2];
static lv_obj_t * s_chip_water, * s_chip_beans, * s_chip_today;
static lv_obj_t * s_v_cm, * s_v_mm, * s_v_str, * s_v_temp;

static int s_last_power = -1, s_last_type = -1, s_last_stage = -1, s_last_brew = -1, s_last_cups = -1;
static int s_last_cm = -1, s_last_mm = -1, s_last_str = -1, s_last_temp = -1;

/* ------------------------------------------------------------------ 小工具 */
static void btn_highlight(lv_obj_t * btn, bool on)
{
    if (btn == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(btn, lv_color_hex(on ? UI_C_ACCENT : UI_C_CARD_2), 0);
    lv_obj_t * lbl = lv_obj_get_child(btn, 0);
    if (lbl != NULL) {
        lv_obj_set_style_text_color(lbl, lv_color_hex(on ? UI_C_TEXT : UI_C_TEXT_DIM), 0);
    }
}

static void chip_set(lv_obj_t * chip, const char * fmt, ...)
{
    if (chip == NULL) {
        return;
    }
    lv_obj_t * lbl = lv_obj_get_child(chip, 0);
    if (lbl == NULL) {
        return;
    }
    char b[48];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    ui_label_set_text_if_changed(lbl, b);
}

/* 子界面切换 */
static void sub_show(int idx)
{
    if (idx < 0 || idx > 1) {
        return;
    }
    for (int k = 0; k < 2; k++) {
        if (s_page[k] == NULL) {
            continue;
        }
        if (k == idx) {
            lv_obj_remove_flag(s_page[k], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_page[k], LV_OBJ_FLAG_HIDDEN);
        }
        btn_highlight(s_tab[k], k == idx);
    }
}

/* ------------------------------------------------------------------ 回调 */
static void tab_cb(lv_event_t * e)     { sub_show((int)(intptr_t)lv_event_get_user_data(e)); }
static void type_cb(lv_event_t * e)    { dm_coffee_set_type((int)(intptr_t)lv_event_get_user_data(e)); }
static void cups_cb(lv_event_t * e)    { dm_coffee_set_cups((int)(intptr_t)lv_event_get_user_data(e)); }

static void power_cb(lv_event_t * e)
{
    dm_coffee_set_power(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void start_cb(lv_event_t * e)
{
    (void)e;
    if (dm_coffee()->brewing) {
        dm_coffee_stop();
    } else {
        dm_coffee_start();
    }
}

static void param_cb(lv_event_t * e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    int v  = (int)lv_slider_get_value(lv_event_get_target(e));
    switch (id) {
    case 0:  dm_coffee_set_coffee_ml(v); break;
    case 1:  dm_coffee_set_milk_ml(v);   break;
    case 2:  dm_coffee_set_strength(v);  break;
    default: dm_coffee_set_temp(v);      break;
    }
}

/* 一行参数：左标题 + 右数值，下面整宽滑条 */
static void build_param(lv_obj_t * parent, const char * name,
                        int min, int max, int val, int id, lv_obj_t ** out_val)
{
    lv_obj_t * box = ui_col(parent);
    lv_obj_set_style_pad_row(box, 3, 0);

    lv_obj_t * row = ui_row(box);
    lv_obj_set_style_pad_column(row, 6, 0);
    ui_label(row, name, UI_FONT_CN, UI_C_TEXT_DIM);
    lv_obj_t * v = ui_label(row, "--", UI_FONT_CN, UI_C_TEXT);
    lv_obj_set_flex_grow(v, 1);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t * sl = ui_slider(box, min, max, val, param_cb, (void *)(intptr_t)id);
    lv_obj_set_height(sl, 18);              /* 加高一点，手指更好按 */
    if (out_val != NULL) {
        *out_val = v;
    }
}

/* ------------------------------------------------------------------ 建屏 */
lv_obj_t * ui_Screen_Coffee_create(lv_obj_t * parent)
{
    dm_coffee_t * c = dm_coffee();

    lv_obj_t * root = ui_col(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_row(root, 8, 0);

    /* ---------- 顶部：标题 + 子界面切换 + 电源 ---------- */
    lv_obj_t * head = ui_row(root);
    lv_obj_set_style_pad_column(head, 6, 0);
    ui_label(head, "咖啡机", UI_FONT_CN, UI_C_TEXT);

    lv_obj_t * spacer = lv_obj_create(head);
    ui_transparent(spacer);
    lv_obj_set_size(spacer, 0, 1);
    lv_obj_set_flex_grow(spacer, 1);

    s_tab[0] = ui_button(head, "选咖啡", UI_C_CARD_2, tab_cb, (void *)(intptr_t)0);
    s_tab[1] = ui_button(head, "参数",   UI_C_CARD_2, tab_cb, (void *)(intptr_t)1);
    lv_obj_set_height(s_tab[0], 34);
    lv_obj_set_height(s_tab[1], 34);
    lv_obj_set_style_pad_hor(s_tab[0], 10, 0);
    lv_obj_set_style_pad_hor(s_tab[1], 10, 0);

    ui_label(head, "电源", UI_FONT_CN, UI_C_TEXT_DIM);
    s_power = ui_toggle(head, c->power, power_cb, NULL);

    /* ================= 子界面 1：选咖啡 ================= */
    lv_obj_t * p0 = ui_row(root);
    lv_obj_set_flex_grow(p0, 1);
    lv_obj_set_style_pad_column(p0, UI_GAP, 0);
    lv_obj_set_flex_align(p0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    s_page[0] = p0;

    /* 左：大图（自适应填充整块，等比缩放不变形） */
    lv_obj_t * imgcard = ui_card(p0, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_flex_grow(imgcard, 5);
    lv_obj_set_height(imgcard, LV_PCT(100));
    lv_obj_set_style_pad_all(imgcard, 8, 0);

    s_hero = lv_image_create(imgcard);
    lv_obj_set_size(s_hero, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(s_hero, LV_IMAGE_ALIGN_CONTAIN);
    lv_image_set_src(s_hero, TYPE_IMG[c->type]);

    /* 右：类型 2x2 + 名称/阶段 + 进度 + 开始 */
    lv_obj_t * side = ui_card(p0, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_flex_grow(side, 4);
    lv_obj_set_height(side, LV_PCT(100));
    lv_obj_set_style_pad_row(side, 10, 0);

    lv_obj_t * grid = ui_col(side);
    lv_obj_set_style_pad_row(grid, 6, 0);
    for (int r = 0; r < 2; r++) {
        lv_obj_t * row = ui_row(grid);
        lv_obj_set_style_pad_column(row, 6, 0);
        for (int k = 0; k < 2; k++) {
            int i = r * 2 + k;
            s_type[i] = ui_button(row, TYPE_NAME[i], UI_C_CARD_2, type_cb, (void *)(intptr_t)i);
            lv_obj_set_height(s_type[i], 38);
            lv_obj_set_width(s_type[i], 0);
            lv_obj_set_flex_grow(s_type[i], 1);
            lv_obj_set_style_pad_hor(s_type[i], 4, 0);
        }
    }

    lv_obj_t * info = ui_row(side);
    lv_obj_set_style_pad_column(info, 8, 0);
    s_name = ui_label(info, "拿铁", UI_FONT_CN, UI_C_TEXT);
    s_chip_stage = ui_chip(info, "待机", UI_C_TEXT_DIM);

    s_bar = lv_bar_create(side);
    lv_obj_set_size(s_bar, LV_PCT(100), 10);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(UI_C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(UI_C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(s_bar, lv_color_hex(UI_C_ACCENT_2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(s_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

    s_start = ui_button(side, "开始冲泡", UI_C_ACCENT, start_cb, NULL);
    lv_obj_set_width(s_start, LV_PCT(100));
    lv_obj_set_flex_grow(s_start, 1);

    /* ================= 子界面 2：参数 ================= */
    lv_obj_t * p1 = ui_col(root);
    lv_obj_set_flex_grow(p1, 1);
    lv_obj_set_style_pad_row(p1, 8, 0);
    s_page[1] = p1;

    lv_obj_t * sliders = ui_row(p1);
    lv_obj_set_flex_grow(sliders, 1);
    lv_obj_set_style_pad_column(sliders, UI_GAP, 0);

    lv_obj_t * c1 = ui_card(sliders, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_flex_grow(c1, 1);
    lv_obj_set_height(c1, LV_PCT(100));
    lv_obj_set_style_pad_row(c1, 16, 0);
    build_param(c1, "咖啡量", 10, 250, c->coffee_ml, 0, &s_v_cm);
    build_param(c1, "牛奶量", 0, 250, c->milk_ml, 1, &s_v_mm);

    lv_obj_t * c2 = ui_card(sliders, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_flex_grow(c2, 1);
    lv_obj_set_height(c2, LV_PCT(100));
    lv_obj_set_style_pad_row(c2, 16, 0);
    build_param(c2, "浓度", 1, 5, c->strength, 2, &s_v_str);
    build_param(c2, "水温", 80, 96, c->temp, 3, &s_v_temp);

    /* 底部：杯数 + 耗材 */
    lv_obj_t * bottom = ui_row(p1);
    lv_obj_set_style_pad_column(bottom, 8, 0);
    ui_label(bottom, "杯数", UI_FONT_CN, UI_C_TEXT_DIM);
    for (int i = 0; i < 2; i++) {
        char t[8];
        snprintf(t, sizeof(t), "x%d", i + 1);
        s_cups[i] = ui_button(bottom, t, UI_C_CARD_2, cups_cb, (void *)(intptr_t)(i + 1));
        lv_obj_set_height(s_cups[i], 38);
        lv_obj_set_width(s_cups[i], 72);
        lv_obj_set_style_pad_hor(s_cups[i], 6, 0);
    }

    lv_obj_t * gap = lv_obj_create(bottom);
    ui_transparent(gap);
    lv_obj_set_size(gap, 0, 1);
    lv_obj_set_flex_grow(gap, 1);

    s_chip_water = ui_chip(bottom, "水--%", UI_C_CYAN);
    s_chip_beans = ui_chip(bottom, "豆--%", UI_C_WARM);
    s_chip_today = ui_chip(bottom, "今日0", UI_C_OK);

    /* 默认显示第一个子界面 */
    sub_show(0);
    ui_Screen_Coffee_refresh();
    return parent;
}

/* ------------------------------------------------------------------ 刷新 */
void ui_Screen_Coffee_refresh(void)
{
    dm_coffee_t * c = dm_coffee();
    if (s_power == NULL) {
        return;
    }

    if (s_last_power != (int)c->power) {
        s_last_power = (int)c->power;
        if (c->power) {
            lv_obj_add_state(s_power, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_power, LV_STATE_CHECKED);
        }
    }

    if (s_last_type != (int)c->type) {
        s_last_type = (int)c->type;
        lv_image_set_src(s_hero, TYPE_IMG[c->type]);
        for (int i = 0; i < DM_COFFEE_TYPES; i++) {
            btn_highlight(s_type[i], (i == (int)c->type));
        }
    }

    ui_label_set_fmt_if_changed(s_name, "%s %dml", dm_coffee_type_name((int)c->type), (int)c->coffee_ml);

    if (s_last_cm != (int)c->coffee_ml)   { s_last_cm = (int)c->coffee_ml;   ui_label_set_fmt_if_changed(s_v_cm, "%d ml", (int)c->coffee_ml); }
    if (s_last_mm != (int)c->milk_ml)     { s_last_mm = (int)c->milk_ml;     ui_label_set_fmt_if_changed(s_v_mm, "%d ml", (int)c->milk_ml); }
    if (s_last_str != (int)c->strength)   { s_last_str = (int)c->strength;   ui_label_set_fmt_if_changed(s_v_str, "%d 级", (int)c->strength); }
    if (s_last_temp != (int)c->temp)      { s_last_temp = (int)c->temp;      ui_label_set_fmt_if_changed(s_v_temp, "%d 度", (int)c->temp); }

    if (s_last_cups != (int)c->cups) {
        s_last_cups = (int)c->cups;
        for (int i = 0; i < 2; i++) {
            btn_highlight(s_cups[i], ((int)c->cups == i + 1));
        }
    }

    int brew = c->brewing ? 1 : 0;
    if (brew != s_last_brew) {
        s_last_brew = brew;
        lv_obj_t * lbl = lv_obj_get_child(s_start, 0);
        if (lbl != NULL) {
            ui_label_set_text_if_changed(lbl, brew ? "停止冲泡" : "开始冲泡");
        }
        lv_obj_set_style_bg_color(s_start, lv_color_hex(brew ? UI_C_DANGER : UI_C_ACCENT), 0);
    }

    if (s_last_stage != (int)c->stage) {
        s_last_stage = (int)c->stage;
        chip_set(s_chip_stage, "%s", dm_coffee_stage_name(c->stage));
        uint32_t col = c->brewing ? UI_C_ACCENT : UI_C_TEXT_DIM;
        lv_obj_set_style_border_color(s_chip_stage, lv_color_hex(col), 0);
        lv_obj_t * l = lv_obj_get_child(s_chip_stage, 0);
        if (l != NULL) {
            lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
        }
    }
    lv_bar_set_value(s_bar, c->progress, LV_ANIM_OFF);

    chip_set(s_chip_water, "水%d%%", (int)c->water_pct);
    chip_set(s_chip_beans, "豆%d%%", (int)c->beans_pct);
    chip_set(s_chip_today, "今日%d", (int)c->cups_today);
}
