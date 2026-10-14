/**
 * @file ui_Screen_Settings.c
 * @brief 设置：WiFi 配网（扫描 + 键盘输入，存 NVS）、背光亮度、关于。
 */

#include "ui_Screen_Settings.h"

#include <Arduino.h>

#include "ui.h"
#include "ui_theme.h"
#include "images/ui_images.h"

#include "app_config.h"
#include "app_display.h"
#include "weather_service.h"
#include "wifi_manager.h"

#define SCAN_MAX 6

static lv_obj_t * s_wifi_state;
static lv_obj_t * s_wifi_detail;
static lv_obj_t * s_scan_list;
static lv_obj_t * s_ssid_ta;
static lv_obj_t * s_pass_ta;
static lv_obj_t * s_city_ta;
static lv_obj_t * s_kb;
static lv_obj_t * s_bright_value;
static lv_obj_t * s_ap_info;

/* ---------------------------------------------------------------- WiFi 操作 */
static char s_scan_names[SCAN_MAX][33];
static int  s_scan_count = 0;

/** 点某一行扫描结果：把名称填进输入框 */
static void scan_pick_cb(lv_event_t * e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_scan_count) {
        return;
    }
    lv_textarea_set_text(s_ssid_ta, s_scan_names[idx]);
    ui_toastf("已选择 %s，接着输入密码", s_scan_names[idx]);
}

static void scan_cb(lv_event_t * e)
{
    (void)e;
    ui_toast("正在扫描...");

    static int8_t rssi[SCAN_MAX];
    int n = wifi_manager_scan(s_scan_names, rssi, SCAN_MAX);
    s_scan_count = (n > 0) ? n : 0;

    lv_obj_clean(s_scan_list);

    if (s_scan_count == 0) {
        ui_label(s_scan_list, "没有扫描到网络", UI_FONT_CN, UI_C_TEXT_MUTE);
        return;
    }

    for (int i = 0; i < s_scan_count; i++) {
        lv_obj_t * row = ui_row(s_scan_list);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_ver(row, 6, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, scan_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        ui_label(row, s_scan_names[i], UI_FONT_CN, UI_C_TEXT);

        lv_obj_t * chip = ui_chip(row, "", (rssi[i] > -67) ? UI_C_OK : UI_C_WARN);
        lv_obj_t * chip_text = lv_obj_get_child(chip, 0);
        ui_label_set_fmt_if_changed(chip_text, "%d dBm", (int)rssi[i]);
    }
}

static void connect_cb(lv_event_t * e)
{
    (void)e;
    const char * ssid = lv_textarea_get_text(s_ssid_ta);
    const char * pass = lv_textarea_get_text(s_pass_ta);

    if (ssid == NULL || ssid[0] == '\0') {
        ui_toast("请先输入或选择 WiFi 名称");
        return;
    }

    wifi_manager_connect(ssid, pass, true);
    ui_toastf("正在连接 %s ...", ssid);
    ui_Screen_Settings_refresh();
}

static void forget_cb(lv_event_t * e)
{
    (void)e;
    wifi_manager_forget();
    lv_textarea_set_text(s_ssid_ta, "");
    lv_textarea_set_text(s_pass_ta, "");
    ui_toast("已清除保存的 WiFi");
    ui_Screen_Settings_refresh();
}

/** 保存天气城市（拼音/英文），写入 NVS 后立刻重新获取 */
static void city_save_cb(lv_event_t * e)
{
    (void)e;
    const char * c = lv_textarea_get_text(s_city_ta);
    if (c == NULL || c[0] == '\0') {
        ui_toast("请输入城市名（拼音，如 hefei）");
        return;
    }
    weather_service_set_city(c);
    ui_toastf("天气城市已设为 %s", c);
}

static void ta_focus_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_obj_t * ta = lv_event_get_target(e);
        lv_keyboard_set_textarea(s_kb, ta);
        lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_kb);
    }
}

static void kb_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------------------------------------------------------- 亮度 */
static void bright_cb(lv_event_t * e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    app_set_backlight((uint8_t)v);
    ui_label_set_fmt_if_changed(s_bright_value, "%d%%", v);
}

/* ---------------------------------------------------------------- 构建 */
lv_obj_t * ui_Screen_Settings_create(lv_obj_t * parent)
{
    /* 当前连接状态 */
    lv_obj_t * card = ui_card(parent, 0x1B2547, UI_C_CARD_2);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * left = ui_col(card);
    lv_obj_set_flex_grow(left, 1);
    s_wifi_state = ui_label(left, "未连接", UI_FONT_CN, UI_C_TEXT);
    s_wifi_detail = ui_label(left, "还没有配置网络", UI_FONT_CN, UI_C_TEXT_DIM);
    s_ap_info = ui_label(left, "热点 --", UI_FONT_SMALL, UI_C_TEXT_MUTE);

    /* 扫描 / 忘记 放到最右边 */
    lv_obj_t * btns = ui_row(card);
    lv_obj_set_width(btns, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_button(btns, "扫描", UI_C_ACCENT, scan_cb, NULL);
    ui_button(btns, "忘记", UI_C_BORDER, forget_cb, NULL);

    /* 扫描结果 */
    lv_obj_t * scan_card = ui_card(parent, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_width(scan_card, LV_PCT(100));
    lv_obj_set_height(scan_card, LV_SIZE_CONTENT);
    ui_card_header(scan_card, "附近网络", "点一下填到下面的名称框", NULL);
    s_scan_list = ui_col(scan_card);
    lv_obj_set_width(s_scan_list, LV_PCT(100));
    ui_label(s_scan_list, "点[扫描]开始搜索", UI_FONT_CN, UI_C_TEXT_MUTE);

    /* 手动输入 */
    lv_obj_t * form = ui_card(parent, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_width(form, LV_PCT(100));
    lv_obj_set_height(form, LV_SIZE_CONTENT);
    ui_card_header(form, "手动输入", NULL, NULL);

    lv_obj_t * row1 = ui_row(form);
    lv_obj_set_style_pad_column(row1, 10, 0);
    ui_label(row1, "名称", UI_FONT_CN, UI_C_TEXT_DIM);
    s_ssid_ta = lv_textarea_create(row1);
    lv_textarea_set_one_line(s_ssid_ta, true);
    lv_obj_set_style_text_font(s_ssid_ta, UI_FONT_CN, 0);
    lv_textarea_set_placeholder_text(s_ssid_ta, "WiFi 名称");
    lv_obj_set_flex_grow(s_ssid_ta, 1);
    lv_obj_set_style_bg_color(s_ssid_ta, lv_color_hex(0x0E1428), 0);
    lv_obj_set_style_text_color(s_ssid_ta, lv_color_hex(UI_C_TEXT), 0);
    lv_obj_set_style_border_color(s_ssid_ta, lv_color_hex(UI_C_BORDER), 0);
    lv_obj_add_event_cb(s_ssid_ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t * row2 = ui_row(form);
    lv_obj_set_style_pad_column(row2, 10, 0);
    ui_label(row2, "密码", UI_FONT_CN, UI_C_TEXT_DIM);
    s_pass_ta = lv_textarea_create(row2);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_obj_set_style_text_font(s_pass_ta, UI_FONT_CN, 0);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta, "WiFi 密码");
    lv_obj_set_flex_grow(s_pass_ta, 1);
    lv_obj_set_style_bg_color(s_pass_ta, lv_color_hex(0x0E1428), 0);
    lv_obj_set_style_text_color(s_pass_ta, lv_color_hex(UI_C_TEXT), 0);
    lv_obj_set_style_border_color(s_pass_ta, lv_color_hex(UI_C_BORDER), 0);
    lv_obj_add_event_cb(s_pass_ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t * act = ui_row(form);
    lv_obj_set_flex_align(act, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_button(act, "保存并连接", UI_C_ACCENT, connect_cb, NULL);

    /* 天气城市（拼音/英文，存 NVS；改完立刻重新获取天气） */
    lv_obj_t * city_card = ui_card(parent, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_width(city_card, LV_PCT(100));
    lv_obj_set_height(city_card, LV_SIZE_CONTENT);
    ui_card_header(city_card, "天气城市", "拼音或英文，如 hefei / anhui", NULL);

    lv_obj_t * crow = ui_row(city_card);
    lv_obj_set_width(crow, LV_PCT(100));
    lv_obj_set_style_pad_column(crow, 10, 0);
    ui_label(crow, "城市", UI_FONT_CN, UI_C_TEXT_DIM);
    s_city_ta = lv_textarea_create(crow);
    lv_textarea_set_one_line(s_city_ta, true);
    lv_obj_set_style_text_font(s_city_ta, UI_FONT_CN, 0);
    lv_textarea_set_placeholder_text(s_city_ta, "hefei");
    lv_textarea_set_text(s_city_ta, weather_service_city());
    lv_obj_set_flex_grow(s_city_ta, 1);
    lv_obj_set_style_bg_color(s_city_ta, lv_color_hex(0x0E1428), 0);
    lv_obj_set_style_text_color(s_city_ta, lv_color_hex(UI_C_TEXT), 0);
    lv_obj_set_style_border_color(s_city_ta, lv_color_hex(UI_C_BORDER), 0);
    lv_obj_add_event_cb(s_city_ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t * cact = ui_row(city_card);
    lv_obj_set_flex_align(cact, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_button(cact, "保存并刷新", UI_C_ACCENT, city_save_cb, NULL);

    /* 亮度卡（整行铺满，避免小字被挤掉） */
    lv_obj_t * bright = ui_card(parent, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_width(bright, LV_PCT(100));
    lv_obj_set_height(bright, LV_SIZE_CONTENT);
    lv_obj_t * bh = ui_row(bright);
    lv_obj_set_width(bh, LV_PCT(100));
    lv_obj_set_flex_align(bh, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(bh, "屏幕亮度", UI_FONT_CN, UI_C_TEXT_DIM);
    s_bright_value = ui_label(bh, "80%", UI_FONT_CN, UI_C_TEXT);
    ui_slider(bright, 5, 100, app_get_backlight(), bright_cb, NULL);
    ui_label(bright, "调低更省电，夜里更护眼", UI_FONT_SMALL, UI_C_TEXT_MUTE);

    /* 关于卡（整行铺满；文字都短，不会再超出屏幕） */
    lv_obj_t * about = ui_card(parent, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_width(about, LV_PCT(100));
    lv_obj_set_height(about, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(about, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(about, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(about, 14, 0);

    lv_obj_t * logo = lv_image_create(about);
    lv_image_set_src(logo, &ui_img_logo);
    lv_image_set_scale(logo, 112);            /* 128 -> 56 */

    lv_obj_t * info = ui_col(about);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_style_pad_row(info, 4, 0);
    ui_label(info, APP_TITLE, UI_FONT_CN, UI_C_TEXT);
    ui_label(info, APP_SUBTITLE " | 版本 " APP_VERSION, UI_FONT_SMALL, UI_C_TEXT_DIM);
    ui_label(info, "ESP32-S3--LVGL 9.5--800x480", UI_FONT_SMALL, UI_C_TEXT_MUTE);
    ui_label(info, "串口配网: wifi <名称> <密码>", UI_FONT_SMALL, UI_C_TEXT_MUTE);
    ui_label(info, "天气: Open-Meteo（免费免 key）", UI_FONT_SMALL, UI_C_TEXT_DIM);

    /* 软键盘（默认隐藏，点输入框弹出） */
    s_kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_kb, 720, 240);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, kb_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(UI_C_CARD), 0);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, 0);

    ui_Screen_Settings_refresh();
    return parent;
}

void ui_Screen_Settings_refresh(void)
{
    if (s_wifi_state == NULL) {
        return;
    }

    wifi_state_t st = wifi_manager_state();

    /* 状态文字颜色只在状态变化时写，避免每秒重绘 */
    static int8_t last_st = -1;
    if (last_st != (int8_t)st) {
        last_st = (int8_t)st;
        uint32_t color = (st == WIFI_ST_CONNECTED) ? UI_C_OK
                         : (st == WIFI_ST_CONNECTING ? UI_C_WARN : UI_C_TEXT_DIM);
        lv_obj_set_style_text_color(s_wifi_state, lv_color_hex(color), 0);
    }

    /* 热点信息：手机连它就能直接控制，这里显示 SSID 和网址 */
    ui_label_set_fmt_if_changed(s_ap_info, "热点 %s | http://%s",
                                wifi_manager_ap_ssid(), wifi_manager_ap_ip());

    switch (st) {
    case WIFI_ST_CONNECTED:
        ui_label_set_text_if_changed(s_wifi_state, "已连接");
        ui_label_set_fmt_if_changed(s_wifi_detail, "%s | %s | %d dBm",
                              wifi_manager_ssid(), wifi_manager_ip(), (int)wifi_manager_rssi());
        break;
    case WIFI_ST_CONNECTING:
        ui_label_set_text_if_changed(s_wifi_state, "连接中...");
        ui_label_set_fmt_if_changed(s_wifi_detail, "%s", wifi_manager_ssid());
        break;
    default:
        ui_label_set_text_if_changed(s_wifi_state, "未连接");
        ui_label_set_text_if_changed(s_wifi_detail,
                          wifi_manager_has_saved() ? "已保存凭据，正在重试..." : "还没有配置网络");
        break;
    }
}
