/**
 * @file ui_comp_statusbar.c
 * @brief 顶部状态栏实现（句柄式，多个屏幕各自一份）。
 */

#include "ui_comp_statusbar.h"

#include <stdlib.h>

#include "ui_theme.h"
#include "images/ui_images.h"

typedef struct {
    lv_obj_t * time;
    lv_obj_t * date;
    lv_obj_t * wifi_icon;
    lv_obj_t * wifi_text;
    lv_obj_t * w_icon;
    lv_obj_t * w_temp;
    lv_obj_t * w_desc;
} statusbar_ctx_t;

static statusbar_ctx_t * ctx_of(lv_obj_t * bar)
{
    return (bar != NULL) ? (statusbar_ctx_t *)lv_obj_get_user_data(bar) : NULL;
}

lv_obj_t * ui_comp_statusbar_create(lv_obj_t * parent)
{
    statusbar_ctx_t * ctx = (statusbar_ctx_t *)calloc(1, sizeof(statusbar_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    lv_obj_t * bar = lv_obj_create(parent);
    ui_transparent(bar);
    lv_obj_set_user_data(bar, ctx);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, UI_STATUS_H);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bar, UI_PAD, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(UI_C_BORDER), 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_70, 0);
    /* 头部做成"悬浮玻璃"：上沿稍亮、往下融进背景 */
    ui_gradient(bar, UI_C_CARD, UI_C_BG);
    lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);

    /* 左：时间 + 日期 */
    lv_obj_t * left = ui_row(bar);
    lv_obj_set_width(left, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(left, 10, 0);
    ctx->time = ui_label(left, "--:--", UI_FONT_NUM_L, UI_C_TEXT);
    ctx->date = ui_label(left, "--月--日", UI_FONT_CN, UI_C_TEXT_DIM);

    /* 右：WiFi + 天气 */
    lv_obj_t * right = ui_row(bar);
    lv_obj_set_width(right, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(right, 14, 0);

    lv_obj_t * wifi_box = ui_row(right);
    lv_obj_set_width(wifi_box, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(wifi_box, 4, 0);
    ctx->wifi_icon = ui_icon(wifi_box, LV_SYMBOL_WIFI, UI_FONT_BODY, UI_C_TEXT_MUTE);
    ctx->wifi_text = ui_label(wifi_box, "未连接", UI_FONT_CN, UI_C_TEXT_DIM);

    lv_obj_t * sep = lv_obj_create(right);
    ui_transparent(sep);
    lv_obj_set_size(sep, 1, 24);
    lv_obj_set_style_bg_color(sep, lv_color_hex(UI_C_BORDER), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    lv_obj_t * weather_box = ui_row(right);
    lv_obj_set_width(weather_box, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(weather_box, 6, 0);
    ctx->w_icon = lv_image_create(weather_box);
    lv_image_set_src(ctx->w_icon, &ui_img_w_sunny);
    lv_image_set_scale(ctx->w_icon, 128);          /* 96 -> 48 */
    ctx->w_temp = ui_label(weather_box, "--°", UI_FONT_NUM_M, UI_C_CYAN);
    ctx->w_desc = ui_label(weather_box, "", UI_FONT_CN, UI_C_TEXT_DIM);

    return bar;
}

void ui_comp_statusbar_set_time(lv_obj_t * bar, const char * time_text, const char * date_text, const char * week_text)
{
    statusbar_ctx_t * ctx = ctx_of(bar);
    if (ctx == NULL) {
        return;
    }
    ui_label_set_text_if_changed(ctx->time, time_text);
    if (week_text != NULL && week_text[0] != '\0') {
        ui_label_set_fmt_if_changed(ctx->date, "%s %s", date_text, week_text);
    } else {
        ui_label_set_text_if_changed(ctx->date, date_text);
    }
}

void ui_comp_statusbar_set_wifi(lv_obj_t * bar, bool connected, int8_t rssi)
{
    statusbar_ctx_t * ctx = ctx_of(bar);
    if (ctx == NULL) {
        return;
    }

    if (connected) {
        uint32_t color = (rssi > -67) ? UI_C_OK : UI_C_WARN;
        lv_obj_set_style_text_color(ctx->wifi_icon, lv_color_hex(color), 0);
        ui_label_set_text_if_changed(ctx->wifi_text, "已连接");
        lv_obj_set_style_text_color(ctx->wifi_text, lv_color_hex(UI_C_TEXT_DIM), 0);
    } else {
        lv_obj_set_style_text_color(ctx->wifi_icon, lv_color_hex(UI_C_TEXT_MUTE), 0);
        ui_label_set_text_if_changed(ctx->wifi_text, "未连接");
        lv_obj_set_style_text_color(ctx->wifi_text, lv_color_hex(UI_C_TEXT_MUTE), 0);
    }
}

void ui_comp_statusbar_set_weather(lv_obj_t * bar, const void * icon_src, const char * temp_text, const char * desc)
{
    statusbar_ctx_t * ctx = ctx_of(bar);
    if (ctx == NULL) {
        return;
    }
    if (icon_src != NULL) {
        lv_image_set_src(ctx->w_icon, icon_src);
    }
    ui_label_set_text_if_changed(ctx->w_temp, temp_text);
    ui_label_set_text_if_changed(ctx->w_desc, desc != NULL ? desc : "");
}
