/**
 * @file ui_Screen_Dashboard.c
 * @brief 首页仪表盘：室内温度 + 场景快捷 + 关键指标 + 设备入口。
 */

#include "ui_Screen_Dashboard.h"

#include "ui.h"
#include "ui_theme.h"
#include "images/coffee/ui_coffee_images.h"
#include "images/ui_images.h"

#include "device_model.h"
#include "weather_service.h"

static lv_obj_t * s_room_temp;      /* 大号室温 */
static lv_obj_t * s_ac_state;       /* 空调状态 chip 文字 */
static lv_obj_t * s_w_icon;
static lv_obj_t * s_w_temp;
static lv_obj_t * s_w_text;
static lv_obj_t * s_stat_light;     /* 指标：灯 */
static lv_obj_t * s_stat_curtain;   /* 指标：窗帘 */
static lv_obj_t * s_stat_power;     /* 指标：功率 */
static lv_obj_t * s_stat_energy;    /* 指标：今日用电 */

/* ---------------------------------------------------------------- 场景按钮 */
static void scene_cb(lv_event_t * e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    dm_scene_apply(idx);
    ui_toastf("已应用 | %s", dm_scene_name(idx));
    ui_Screen_Dashboard_refresh();
}

static const char * SCENE_ICON[DM_SCENE_COUNT] = {
    LV_SYMBOL_HOME, LV_SYMBOL_CLOSE, LV_SYMBOL_EYE_CLOSE, LV_SYMBOL_PLAY
};

static void build_scene_buttons(lv_obj_t * parent)
{
    lv_obj_t * col = ui_col(parent);
    lv_obj_set_width(col, 260);
    lv_obj_set_style_pad_row(col, 8, 0);

    ui_label(col, "快捷场景", UI_FONT_CN, UI_C_TEXT_DIM);

    lv_obj_t * grid = lv_obj_create(col);
    ui_transparent(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_style_pad_row(grid, 8, 0);

    for (int i = 0; i < DM_SCENE_COUNT; i++) {
        lv_obj_t * btn = lv_obj_create(grid);
        ui_transparent(btn);
        lv_obj_set_size(btn, 122, 44);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_C_ACCENT), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_20, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(UI_C_ACCENT), 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_40, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(btn, 6, 0);
        lv_obj_add_event_cb(btn, scene_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        ui_icon(btn, SCENE_ICON[i], UI_FONT_BODY, UI_C_ACCENT);
        ui_label(btn, dm_scene_name(i), UI_FONT_CN, UI_C_TEXT);
    }
}

/* ---------------------------------------------------------------- 指标小卡 */
static lv_obj_t * build_stat(lv_obj_t * parent, const char * label, const char * symbol, lv_obj_t ** out_value)
{
    lv_obj_t * card = ui_card(parent, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 12, 0);

    lv_obj_t * row = ui_row(card);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t * icon_box = lv_obj_create(row);
    ui_transparent(icon_box);
    lv_obj_set_size(icon_box, 32, 32);
    lv_obj_set_style_radius(icon_box, 10, 0);
    lv_obj_set_style_bg_color(icon_box, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_bg_opa(icon_box, LV_OPA_20, 0);
    lv_obj_set_flex_flow(icon_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_icon(icon_box, symbol, UI_FONT_SMALL, UI_C_ACCENT);

    lv_obj_t * v = ui_label(row, "--", UI_FONT_NUM_M, UI_C_TEXT);
    if (out_value != NULL) {
        *out_value = v;
    }

    ui_label(card, label, UI_FONT_CN, UI_C_TEXT_DIM);
    return card;
}

/* ---------------------------------------------------------------- 设备入口 */
static void device_tile_cb(lv_event_t * e)
{
    ui_nav_goto((ui_screen_id_t)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t * build_device_tile(lv_obj_t * parent, const void * icon, const char * title,
                                    const char * sub, ui_screen_id_t target)
{
    lv_obj_t * tile = ui_card(parent, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_height(tile, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(tile, 12, 0);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, device_tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)target);
    /* 按压反馈：边框亮起 */
    lv_obj_set_style_border_color(tile, lv_color_hex(UI_C_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(tile, LV_OPA_COVER, LV_STATE_PRESSED);

    lv_obj_t * img = lv_image_create(tile);
    lv_image_set_src(img, icon);
    lv_image_set_scale(img, 96);                 /* 48 -> 18 */
    lv_obj_set_style_image_recolor(img, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);

    ui_label(tile, title, UI_FONT_CN, UI_C_TEXT);
    ui_label(tile, sub, UI_FONT_SMALL, UI_C_TEXT_DIM);
    return tile;
}

/* ---------------------------------------------------------------- 构建 */
lv_obj_t * ui_Screen_Dashboard_create(lv_obj_t * parent)
{
    /* 顶部大卡：室温 + 空调状态 + 天气 */
    lv_obj_t * hero = ui_card(parent, 0x1B2547, UI_C_CARD_2);
    lv_obj_set_width(hero, LV_PCT(100));
    lv_obj_set_height(hero, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 左：室温 */
    lv_obj_t * left = ui_col(hero);
    lv_obj_set_width(left, 300);
    lv_obj_set_style_pad_row(left, 2, 0);
    ui_label(left, "室内温度", UI_FONT_CN, UI_C_TEXT_DIM);
    lv_obj_t * temp_row = ui_row(left);
    lv_obj_set_style_pad_column(temp_row, 4, 0);
    s_room_temp = ui_label(temp_row, "--.-", UI_FONT_NUM_XL, UI_C_TEXT);
    ui_label(temp_row, "°C", UI_FONT_NUM_M, UI_C_TEXT_DIM);

    lv_obj_t * chip_row = ui_row(left);
    lv_obj_set_style_pad_column(chip_row, 8, 0);
    lv_obj_t * ac_chip = ui_chip(chip_row, "空调 关", UI_C_CYAN);
    s_ac_state = lv_obj_get_child(ac_chip, 0);
    ui_chip(chip_row, "湿度 48%", UI_C_ACCENT_2);

    /* 中：场景 */
    build_scene_buttons(hero);

    /* 右：天气缩略 */
    lv_obj_t * right = ui_col(hero);
    lv_obj_set_width(right, 190);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(right, 2, 0);

    s_w_icon = lv_image_create(right);
    lv_image_set_src(s_w_icon, &ui_img_w_sunny);
    lv_image_set_scale(s_w_icon, 128);           /* 96 -> 48 */

    s_w_temp = ui_label(right, "--°", UI_FONT_NUM_L, UI_C_TEXT);
    s_w_text = ui_label(right, "-", UI_FONT_CN, UI_C_TEXT_DIM);

    /* 指标行 */
    lv_obj_t * stats = ui_row(parent);
    lv_obj_set_width(stats, LV_PCT(100));
    lv_obj_set_style_pad_column(stats, UI_GAP, 0);
    build_stat(stats, "灯光开启", LV_SYMBOL_CHARGE, &s_stat_light);
    build_stat(stats, "窗帘开启", LV_SYMBOL_LIST, &s_stat_curtain);
    build_stat(stats, "当前功率", LV_SYMBOL_CHARGE, &s_stat_power);
    build_stat(stats, "今日用电", LV_SYMBOL_BARS, &s_stat_energy);

    /* 设备入口 */
    lv_obj_t * tiles = ui_row(parent);
    lv_obj_set_width(tiles, LV_PCT(100));
    lv_obj_set_style_pad_column(tiles, UI_GAP, 0);
    build_device_tile(tiles, &ui_img_nav_bulb, "灯光控制", "6 路灯具", UI_SCREEN_LIGHTS);
    build_device_tile(tiles, &ui_img_nav_ac, "空调温控", "制冷 / 制热", UI_SCREEN_CLIMATE);
    build_device_tile(tiles, &ui_img_nav_curtain, "窗帘", "3 路窗帘", UI_SCREEN_CURTAINS);

    lv_obj_t * tiles2 = ui_row(parent);
    lv_obj_set_width(tiles2, LV_PCT(100));
    lv_obj_set_style_pad_column(tiles2, UI_GAP, 0);
    build_device_tile(tiles2, &ui_img_nav_bolt, "能耗统计", "7 天趋势", UI_SCREEN_ENERGY);
    build_device_tile(tiles2, &ui_img_cup_png, "咖啡机", "拿铁 60/150ml", UI_SCREEN_COFFEE);
    build_device_tile(tiles2, &ui_img_nav_cloud, "天气", "实时 + 预报", UI_SCREEN_WEATHER);

    ui_Screen_Dashboard_refresh();
    return parent;
}

/* ---------------------------------------------------------------- 刷新 */
void ui_Screen_Dashboard_refresh(void)
{
    if (s_room_temp == NULL) {
        return;
    }

    dm_ac_t * ac = dm_ac();
    ui_label_set_fmt_if_changed(s_room_temp, "%.1f", ac->room);

    static const char * MODE_CN[4] = { "制冷", "制热", "送风", "除湿" };
    static int8_t last_ac_on = -1;
    if (ac->on) {
        ui_label_set_fmt_if_changed(s_ac_state, "空调 %s %d°C", MODE_CN[ac->mode], ac->target);
    } else {
        ui_label_set_text_if_changed(s_ac_state, "空调 关");
    }
    if (last_ac_on != (int8_t)ac->on) {
        last_ac_on = (int8_t)ac->on;
        lv_obj_set_style_text_color(s_ac_state,
                                    lv_color_hex(ac->on ? UI_C_CYAN : UI_C_TEXT_DIM), 0);
    }

    /* 天气（图片只在类型变化时换，换 src 会触发重绘） */
    const weather_t * w = weather_service_get();
    if (w->valid) {
        static int8_t last_kind = -1;
        if (last_kind != (int8_t)w->kind) {
            last_kind = (int8_t)w->kind;
            lv_image_set_src(s_w_icon, weather_icon(w->kind));
        }
        ui_label_set_fmt_if_changed(s_w_temp, "%d°", w->temp);
        ui_label_set_fmt_if_changed(s_w_text, "%s", w->text);
    }

    /* 指标 */
    dm_light_t * lights = dm_lights();
    int on = 0;
    for (int i = 0; i < DM_LIGHT_COUNT; i++) {
        if (lights[i].on) {
            on++;
        }
    }
    ui_label_set_fmt_if_changed(s_stat_light, "%d/%d", on, DM_LIGHT_COUNT);

    dm_curtain_t * curtains = dm_curtains();
    int open = 0;
    for (int i = 0; i < DM_CURTAIN_COUNT; i++) {
        if (curtains[i].position > 0) {
            open++;
        }
    }
    ui_label_set_fmt_if_changed(s_stat_curtain, "%d/%d", open, DM_CURTAIN_COUNT);

    dm_energy_t * e = dm_energy();
    if (e->power_w >= 1000) {
        ui_label_set_fmt_if_changed(s_stat_power, "%.1fkW", (float)e->power_w / 1000.0f);
    } else {
        ui_label_set_fmt_if_changed(s_stat_power, "%dW", e->power_w);
    }
    ui_label_set_fmt_if_changed(s_stat_energy, "%.2fkWh", e->today_kwh);
}
