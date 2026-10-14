/**
 * @file ui_Screen_Weather.c
 * @brief 天气：实时大卡 + 详情 + 3 天预报（可手动刷新）。
 */

#include "ui_Screen_Weather.h"

#include "ui.h"
#include "ui_theme.h"
#include "images/ui_images.h"

#include "app_events.h"
#include "weather_service.h"
#include "wifi_manager.h"

static lv_obj_t * s_icon;
static lv_obj_t * s_temp;
static lv_obj_t * s_text;
static lv_obj_t * s_city;
static lv_obj_t * s_range;
static lv_obj_t * s_source;
static lv_obj_t * s_feel;
static lv_obj_t * s_hum;
static lv_obj_t * s_wind;
static lv_obj_t * s_fc_icon[WEATHER_FC_DAYS];
static lv_obj_t * s_fc_text[WEATHER_FC_DAYS];
static lv_obj_t * s_fc_temp[WEATHER_FC_DAYS];

static void refresh_cb(lv_event_t * e)
{
    (void)e;
    weather_service_request_refresh();
    ev_weather_refresh();        /* 上报"刷新天气"这个动作 */
    ui_toast("正在刷新天气...");
}

lv_obj_t * ui_Screen_Weather_create(lv_obj_t * parent)
{
    /* 顶部：标题 + 刷新按钮 */
    lv_obj_t * head = ui_row(parent);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(head, "天气", UI_FONT_CN, UI_C_TEXT);
    ui_button(head, "刷新", UI_C_ACCENT, refresh_cb, NULL);

    /* 实时大卡 */
    lv_obj_t * hero = ui_card(parent, 0x1B2547, UI_C_CARD_2);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * left = ui_row(hero);
    lv_obj_set_width(left, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(left, 18, 0);

    s_icon = lv_image_create(left);
    lv_image_set_src(s_icon, &ui_img_w_sunny);
    lv_image_set_scale(s_icon, 200);          /* 96 -> 75 */

    lv_obj_t * temp_col = ui_col(left);
    lv_obj_set_width(temp_col, LV_SIZE_CONTENT);
    s_temp = ui_label(temp_col, "--°", UI_FONT_NUM_XL, UI_C_TEXT);
    s_text = ui_label(temp_col, "-", UI_FONT_CN, UI_C_TEXT_DIM);

    lv_obj_t * right = ui_col(hero);
    lv_obj_set_width(right, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    s_city = ui_label(right, "-", UI_FONT_CN, UI_C_TEXT);
    s_range = ui_label(right, "- / -", UI_FONT_CN, UI_C_TEXT_DIM);
    s_source = ui_label(right, "", UI_FONT_SMALL, UI_C_TEXT_MUTE);

    /* 详情三宫格 */
    lv_obj_t * details = ui_row(parent);
    lv_obj_set_width(details, LV_PCT(100));
    lv_obj_set_style_pad_column(details, UI_GAP, 0);

    const char * titles[3] = { "体感温度", "相对湿度", "风速" };
    lv_obj_t ** outs[3] = { &s_feel, &s_hum, &s_wind };
    for (int i = 0; i < 3; i++) {
        lv_obj_t * c = ui_card(details, UI_C_CARD, UI_C_CARD_2);
        lv_obj_set_flex_grow(c, 1);
        lv_obj_set_height(c, 96);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label(c, titles[i], UI_FONT_CN, UI_C_TEXT_DIM);
        *outs[i] = ui_label(c, "--", UI_FONT_NUM_M, UI_C_TEXT);
    }

    /* 预报 */
    lv_obj_t * fc_card = ui_card(parent, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_width(fc_card, LV_PCT(100));
    lv_obj_set_height(fc_card, LV_SIZE_CONTENT);
    ui_card_header(fc_card, "未来三天", NULL, NULL);

    lv_obj_t * fc_row = ui_row(fc_card);
    lv_obj_set_width(fc_row, LV_PCT(100));
    lv_obj_set_flex_align(fc_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < WEATHER_FC_DAYS; i++) {
        lv_obj_t * col = ui_col(fc_row);
        lv_obj_set_width(col, LV_PCT(31));
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(col, lv_color_hex(0x0E1428), 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_60, 0);
        lv_obj_set_style_radius(col, 14, 0);
        lv_obj_set_style_pad_all(col, 10, 0);

        ui_label(col, (i == 0) ? "明天" : (i == 1 ? "后天" : "大后天"), UI_FONT_CN, UI_C_TEXT_DIM);

        s_fc_icon[i] = lv_image_create(col);
        lv_image_set_src(s_fc_icon[i], &ui_img_w_partly);
        lv_image_set_scale(s_fc_icon[i], 100);      /* 96 -> 37 */

        s_fc_text[i] = ui_label(col, "-", UI_FONT_CN, UI_C_TEXT);
        s_fc_temp[i] = ui_label(col, "-- / --", UI_FONT_BODY, UI_C_TEXT_DIM);
    }

    ui_Screen_Weather_refresh();
    return parent;
}

void ui_Screen_Weather_refresh(void)
{
    if (s_temp == NULL) {
        return;
    }

    const weather_t * w = weather_service_get();
    if (!w->valid) {
        return;
    }

    static int8_t last_kind = -1;
    if (last_kind != (int8_t)w->kind) {
        last_kind = (int8_t)w->kind;
        lv_image_set_src(s_icon, weather_icon(w->kind));
        for (int i = 0; i < WEATHER_FC_DAYS; i++) {
            lv_image_set_src(s_fc_icon[i], weather_icon(w->fc[i].kind));
        }
    }

    ui_label_set_fmt_if_changed(s_temp, "%d°", w->temp);
    ui_label_set_text_if_changed(s_text, w->text);
    ui_label_set_text_if_changed(s_city, w->city);
    ui_label_set_fmt_if_changed(s_range, "最高 %d° / 最低 %d°", w->high, w->low);

    static int8_t last_demo = -1;
    if (last_demo != (int8_t)w->demo) {
        last_demo = (int8_t)w->demo;
        lv_obj_set_style_text_color(s_source,
                                    lv_color_hex(w->demo ? UI_C_WARN : UI_C_TEXT_MUTE), 0);
    }
    if (w->demo) {
        ui_label_set_text_if_changed(s_source, "演示数据 | 未联网");
    } else {
        ui_label_set_fmt_if_changed(s_source, "更新于 %d 分钟前", w->updated_min_ago);
    }

    ui_label_set_fmt_if_changed(s_feel, "%d°", w->feels);
    if (w->humidity > 0) {
        ui_label_set_fmt_if_changed(s_hum, "%d%%", w->humidity);
    } else {
        ui_label_set_text_if_changed(s_hum, "-");
    }
    if (w->wind_kmh > 0) {
        ui_label_set_fmt_if_changed(s_wind, "%.0f km/h", w->wind_kmh);
    } else {
        ui_label_set_text_if_changed(s_wind, "-");
    }

    for (int i = 0; i < WEATHER_FC_DAYS; i++) {
        lv_image_set_src(s_fc_icon[i], weather_icon(w->fc[i].kind));
        ui_label_set_text_if_changed(s_fc_text[i], w->fc[i].text);
        ui_label_set_fmt_if_changed(s_fc_temp[i], "%d° / %d°", w->fc[i].high, w->fc[i].low);
    }
}
