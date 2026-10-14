/**
 * @file ui_theme.c
 * @brief 设计系统实现（配色、字体、通用控件构建器）。
 */

#include "ui_theme.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* 中文字库（src/lv_font_cjk_24.c）：SourceHanSans 24px，含 ASCII + 3755 常用汉字 */
LV_FONT_DECLARE(lv_font_cjk_24);

/* -------------------------------------------------------------------------
 * 文本更新：值没变就不动标签
 *
 * 每秒刷新时如果无脑调 lv_label_set_text()，即使内容一样也会让该标签（以及它所在卡片）
 * 重新绘制，页面元素一多就明显影响流畅度。这里统一做"变了才写"。
 * ---------------------------------------------------------------------- */
void ui_label_set_text_if_changed(lv_obj_t * label, const char * text)
{
    if (label == NULL || text == NULL) {
        return;
    }
    const char * cur = lv_label_get_text(label);
    if (cur != NULL && strcmp(cur, text) == 0) {
        return;
    }
    lv_label_set_text(label, text);
}

void ui_label_set_fmt_if_changed(lv_obj_t * label, const char * fmt, ...)
{
    if (label == NULL || fmt == NULL) {
        return;
    }
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ui_label_set_text_if_changed(label, buf);
}

const lv_font_t * UI_FONT_NUM_XL = &lv_font_montserrat_48;
const lv_font_t * UI_FONT_NUM_L = &lv_font_montserrat_32;
const lv_font_t * UI_FONT_NUM_M = &lv_font_montserrat_24;
const lv_font_t * UI_FONT_H1 = &lv_font_montserrat_20;
const lv_font_t * UI_FONT_BODY = &lv_font_montserrat_16;
/* 重要：Montserrat 不含汉字，中文标签一旦用它就会显示成方框（豆腐块）。
 * 所以"小字"也直接用中文字库（它同时含 ASCII，中英混排都没问题）。
 * UI_FONT_SMALL 目前和中文同字号（24px），等以后生成 18px 中文字库再换小。 */
const lv_font_t * UI_FONT_SMALL = &lv_font_cjk_24;
const lv_font_t * UI_FONT_CN = &lv_font_cjk_24;
const lv_font_t * UI_FONT_CN_SMALL = &lv_font_cjk_24;

void ui_theme_init(void)
{
    /* 字体全部是编译期常量，这里只做一次"把默认字体设成能显示中文的那个"，
     * 避免忘记给某个 label 设字体时出现空白。 */
    lv_obj_t *scr = lv_screen_active();     /* 此刻还没建屏，可能为 NULL，做保护 */
    if (scr != NULL) {
        lv_obj_set_style_text_font(scr, UI_FONT_CN, 0);
    }
}

/* -------------------------------------------------------------------------
 * 基础容器
 * ---------------------------------------------------------------------- */

void ui_transparent(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void ui_gradient(lv_obj_t *obj, uint32_t top_color, uint32_t bottom_color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(top_color), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(bottom_color), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

lv_obj_t * ui_screen_root(lv_obj_t *screen)
{
    ui_transparent(screen);
    ui_gradient(screen, UI_C_BG, UI_C_BG_2);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_pad_row(screen, 0, 0);
    return screen;
}

lv_obj_t * ui_card(lv_obj_t *parent, uint32_t top_color, uint32_t bottom_color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    ui_gradient(card, top_color, bottom_color);
    lv_obj_set_style_radius(card, UI_RADIUS, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_C_BORDER), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_90, 0);
    /* 注意：不加阴影。LVGL 软件渲染里模糊阴影非常贵，卡片一多就明显掉帧，
     * 深色底上 1px 描边已经足够分层 */
    lv_obj_set_style_pad_all(card, UI_PAD, 0);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return card;
}

lv_obj_t * ui_row(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    ui_transparent(box);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(box, UI_GAP, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

lv_obj_t * ui_col(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    ui_transparent(box);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(box, 4, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

void ui_flex_fill_row(lv_obj_t *obj)
{
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, 0, 0);
}

/* -------------------------------------------------------------------------
 * 文本 / 图标
 * ---------------------------------------------------------------------- */

lv_obj_t * ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

lv_obj_t * ui_icon(lv_obj_t *parent, const char *symbol, const lv_font_t *font, uint32_t color)
{
    return ui_label(parent, symbol, font, color);
}

lv_obj_t * ui_chip(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *chip = lv_obj_create(parent);
    ui_transparent(chip);
    lv_obj_set_height(chip, LV_SIZE_CONTENT);
    lv_obj_set_width(chip, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_20, 0);
    lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(chip, 10, 0);
    lv_obj_set_style_pad_ver(chip, 4, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(chip, LV_OPA_40, 0);
    ui_label(chip, text, UI_FONT_CN, color);
    return chip;
}

lv_obj_t * ui_button(lv_obj_t *parent, const char *text, uint32_t color, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_obj_create(parent);
    ui_transparent(btn);
    lv_obj_set_height(btn, 46);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(btn, 22, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(UI_C_ACCENT_2), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_HOR, 0);
    /* 按压反馈：手指按下时亮一点，手感更跟手 */
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(btn, text, UI_FONT_CN, UI_C_TEXT);
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

lv_obj_t * ui_icon_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_obj_create(parent);
    ui_transparent(btn);
    lv_obj_set_size(btn, 40, 40);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_C_TEXT), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_10, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_icon(btn, symbol, UI_FONT_BODY, UI_C_TEXT_DIM);
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    return btn;
}

/* -------------------------------------------------------------------------
 * 开关 / 滑条
 * ---------------------------------------------------------------------- */

lv_obj_t * ui_toggle(lv_obj_t *parent, bool checked, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 52, 30);
    lv_obj_set_style_bg_color(sw, lv_color_hex(UI_C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(UI_C_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_color(sw, lv_color_hex(UI_C_ACCENT_2), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_grad_dir(sw, LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(UI_C_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, 3, LV_PART_KNOB);

    if (checked) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    if (cb != NULL) {
        lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user_data);
    }
    return sw;
}

lv_obj_t * ui_slider(lv_obj_t *parent, int32_t min, int32_t max, int32_t value,
                     lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_height(slider, 12);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(slider, lv_color_hex(UI_C_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(slider, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_C_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);

    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    if (cb != NULL) {
        lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, user_data);
    }
    return slider;
}

/* -------------------------------------------------------------------------
 * 列表行 / 卡片头
 * ---------------------------------------------------------------------- */

lv_obj_t * ui_list_row(lv_obj_t *parent, const char *symbol, const char *title, const char *value)
{
    lv_obj_t *row = ui_row(parent);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(row, 6, 0);

    if (symbol != NULL) {
        lv_obj_t *icon_box = lv_obj_create(row);
        ui_transparent(icon_box);
        lv_obj_set_size(icon_box, 40, 40);
        lv_obj_set_style_radius(icon_box, 12, 0);
        lv_obj_set_style_bg_color(icon_box, lv_color_hex(UI_C_ACCENT), 0);
        lv_obj_set_style_bg_opa(icon_box, LV_OPA_20, 0);
        lv_obj_set_flex_flow(icon_box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(icon_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_icon(icon_box, symbol, UI_FONT_BODY, UI_C_ACCENT);
    }

    lv_obj_t *text_col = ui_col(row);
    lv_obj_set_flex_grow(text_col, 1);
    ui_label(text_col, title, UI_FONT_CN, UI_C_TEXT);
    if (value != NULL) {
        /* 指标小卡的副标题也可能是中文，用中文字库 */
        lv_obj_t *val = ui_label(text_col, value, UI_FONT_CN, UI_C_TEXT_DIM);
        (void)val;
    }
    return row;
}

lv_obj_t * ui_card_header(lv_obj_t *card, const char *title, const char *right_text, lv_obj_t **out_right)
{
    lv_obj_t *row = ui_row(card);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title_label = ui_label(row, title, UI_FONT_CN, UI_C_TEXT);
    lv_obj_t *right_label = NULL;
    if (right_text != NULL) {
        /* 右侧小字可能是中文（如"单位: kWh"），必须用中文字库 */
        right_label = ui_label(row, right_text, UI_FONT_CN, UI_C_TEXT_DIM);
    }
    if (out_right != NULL) {
        *out_right = right_label;
    }
    return title_label;
}
