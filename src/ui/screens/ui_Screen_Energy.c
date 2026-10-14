/**
 * @file ui_Screen_Energy.c
 * @brief 能耗统计：今日用电 / 当前功率 / 电费 + 近 7 天柱状图。
 */

#include "ui_Screen_Energy.h"

#include "ui.h"
#include "ui_theme.h"

#include "device_model.h"

static lv_obj_t * s_today;
static lv_obj_t * s_power;
static lv_obj_t * s_cost;
static lv_obj_t * s_chart;

static const char * DAY_LABEL[DM_ENERGY_DAYS] = { "6天前", "5天前", "4天前", "3天前", "前天", "昨天", "今天" };

static lv_obj_t * build_metric(lv_obj_t * parent, const char * title, lv_obj_t ** out_value)
{
    lv_obj_t * card = ui_card(parent, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, 110);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(card, title, UI_FONT_CN, UI_C_TEXT_DIM);
    *out_value = ui_label(card, "--", UI_FONT_NUM_L, UI_C_TEXT);
    return card;
}

lv_obj_t * ui_Screen_Energy_create(lv_obj_t * parent)
{
    lv_obj_t * metrics = ui_row(parent);
    lv_obj_set_width(metrics, LV_PCT(100));
    lv_obj_set_style_pad_column(metrics, UI_GAP, 0);
    build_metric(metrics, "今日用电 (kWh)", &s_today);
    build_metric(metrics, "当前功率", &s_power);
    build_metric(metrics, "今日电费 (元)", &s_cost);

    lv_obj_t * chart_card = ui_card(parent, 0x1B2547, UI_C_CARD_2);
    lv_obj_set_width(chart_card, LV_PCT(100));
    lv_obj_set_height(chart_card, LV_SIZE_CONTENT);

    lv_obj_t * right = NULL;
    ui_card_header(chart_card, "近 7 天用电趋势", "单位: kWh", &right);
    if (right != NULL) {
        lv_obj_set_style_text_color(right, lv_color_hex(UI_C_TEXT_MUTE), 0);
    }

    s_chart = lv_chart_create(chart_card);
    lv_obj_set_size(s_chart, LV_PCT(100), 180);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(s_chart, DM_ENERGY_DAYS);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 10);
    lv_chart_set_div_line_count(s_chart, 5, 0);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_all(s_chart, 0, 0);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(UI_C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_chart, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(UI_C_ACCENT), LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_color(s_chart, lv_color_hex(UI_C_ACCENT_2), LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_dir(s_chart, LV_GRAD_DIR_VER, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_chart, 6, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, LV_PART_ITEMS);

    lv_chart_series_t * ser = lv_chart_add_series(s_chart, lv_color_hex(UI_C_ACCENT), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < DM_ENERGY_DAYS; i++) {
        lv_chart_set_next_value(s_chart, ser, (int32_t)(dm_energy()->days[i] * 10));
    }

    /* 横轴文字 */
    lv_obj_t * axis = ui_row(chart_card);
    lv_obj_set_width(axis, LV_PCT(100));
    lv_obj_set_flex_align(axis, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < DM_ENERGY_DAYS; i++) {
        ui_label(axis, DAY_LABEL[i], UI_FONT_SMALL, UI_C_TEXT_MUTE);
    }

    ui_Screen_Energy_refresh();
    return parent;
}

void ui_Screen_Energy_refresh(void)
{
    if (s_today == NULL) {
        return;
    }

    dm_energy_t * e = dm_energy();
    ui_label_set_fmt_if_changed(s_today, "%.2f", e->today_kwh);
    if (e->power_w >= 1000) {
        ui_label_set_fmt_if_changed(s_power, "%.1f kW", (float)e->power_w / 1000.0f);
    } else {
        ui_label_set_fmt_if_changed(s_power, "%d W", e->power_w);
    }
    ui_label_set_fmt_if_changed(s_cost, "%.2f", e->today_kwh * e->tariff);

    /* 柱状图：自动范围，避免超过 10kWh 被截断 */
    int32_t maxv = 10;
    for (int i = 0; i < DM_ENERGY_DAYS; i++) {
        int32_t v = (int32_t)(e->days[i] * 10);
        if (v > maxv) {
            maxv = v;
        }
    }
    /* 图表只在数据真的变了才刷新：lv_chart_refresh() 会让整块图表区重绘（很大一块面积） */
    static int32_t last_v[DM_ENERGY_DAYS] = { -1, -1, -1, -1, -1, -1, -1 };
    static int32_t last_max = -1;
    bool chart_dirty = false;

    for (int i = 0; i < DM_ENERGY_DAYS; i++) {
        int32_t v = (int32_t)(e->days[i] * 10);
        if (v != last_v[i]) {
            last_v[i] = v;
            chart_dirty = true;
        }
    }
    if (last_max != maxv + 5) {
        last_max = maxv + 5;
        lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, last_max);
        chart_dirty = true;
    }

    if (chart_dirty) {
        lv_chart_series_t * ser = lv_chart_get_series_next(s_chart, NULL);
        if (ser != NULL) {
            for (int i = 0; i < DM_ENERGY_DAYS; i++) {
                lv_chart_set_value_by_id(s_chart, ser, i, last_v[i]);
            }
            lv_chart_refresh(s_chart);
        }
    }
}
