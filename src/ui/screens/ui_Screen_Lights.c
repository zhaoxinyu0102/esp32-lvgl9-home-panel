/**
 * @file ui_Screen_Lights.c
 * @brief 灯光控制：6 路灯具，开关 + 亮度 + （氛围灯）颜色预设。
 */

#include "ui_Screen_Lights.h"

#include "ui.h"
#include "ui_theme.h"

#include "device_model.h"

#define LIGHT_COLS 2

static lv_obj_t * s_toggle[DM_LIGHT_COUNT];
static lv_obj_t * s_slider[DM_LIGHT_COUNT];
static lv_obj_t * s_value[DM_LIGHT_COUNT];
static lv_obj_t * s_swatch[DM_LIGHT_COUNT];
static lv_obj_t * s_summary;

static void toggle_cb(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    dm_light_toggle(i);
    ui_Screen_Lights_refresh();
}

static void slider_cb(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t * sl = lv_event_get_target(e);
    int v = (int)lv_slider_get_value(sl);
    if (v < 1) {
        v = 1;
    }
    dm_light_set(i, true, v, -1, 0, 0);
    ui_label_set_fmt_if_changed(s_value[i], "亮度 %d%%", v);
    if (s_toggle[i] != NULL && !lv_obj_has_state(s_toggle[i], LV_STATE_CHECKED)) {
        lv_obj_add_state(s_toggle[i], LV_STATE_CHECKED);
    }
}

static const uint32_t PRESET_COLORS[6] = {
    0xFFD9A0, 0xFFFFFF, 0x4F8CFF, 0x8B5CF6, 0x22C55E, 0xEF4444
};

static void swatch_cb(lv_event_t * e)
{
    /* user_data: 高 8 位 = 灯序号，低 8 位 = 预设颜色序号 */
    uintptr_t packed = (uintptr_t)lv_event_get_user_data(e);
    int i = (int)(packed >> 8);
    int c = (int)(packed & 0xFF);
    dm_light_set(i, true, -1, 1, 0, PRESET_COLORS[c]);
    ui_Screen_Lights_refresh();
}

static void all_on_cb(lv_event_t * e)
{
    (void)e;
    dm_light_all(true);
    ui_toast("已全部打开");
    ui_Screen_Lights_refresh();
}

static void all_off_cb(lv_event_t * e)
{
    (void)e;
    dm_light_all(false);
    ui_toast("已全部关闭");
    ui_Screen_Lights_refresh();
}

static void build_light_card(lv_obj_t * parent, int i)
{
    dm_light_t * l = &dm_lights()[i];

    lv_obj_t * card = ui_card(parent, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_width(card, LV_PCT(48));
    lv_obj_set_height(card, LV_SIZE_CONTENT);

    lv_obj_t * head = ui_row(card);
    lv_obj_set_style_pad_column(head, 8, 0);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * title = ui_col(head);
    lv_obj_set_flex_grow(title, 1);
    ui_label(title, l->name, UI_FONT_CN, UI_C_TEXT);
    ui_label(title, l->room, UI_FONT_SMALL, UI_C_TEXT_MUTE);

    s_toggle[i] = ui_toggle(head, l->on, toggle_cb, (void *)(intptr_t)i);

    /* 第二行：亮度数值（单独一行，避免和下面的颜色圆点挤在一起重叠） */
    lv_obj_t * info = ui_row(card);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_value[i] = ui_label(info, "--", UI_FONT_CN, UI_C_TEXT_DIM);

    /* 第三行：彩光预设色（只有氛围灯有），自己的行 + 标题，绝不和数值重叠 */
    if (l->mode == 1) {
        lv_obj_t * color_row = ui_row(card);
        lv_obj_set_width(color_row, LV_PCT(100));
        lv_obj_set_flex_align(color_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(color_row, 10, 0);

        ui_label(color_row, "颜色", UI_FONT_CN, UI_C_TEXT_MUTE);

        lv_obj_t * dots = ui_row(color_row);
        lv_obj_set_style_pad_column(dots, 8, 0);
        for (int c = 0; c < 6; c++) {
            lv_obj_t * sw = lv_obj_create(dots);
            ui_transparent(sw);
            lv_obj_set_size(sw, 22, 22);
            lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(sw, lv_color_hex(PRESET_COLORS[c]), 0);
            lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(sw, 2, 0);
            lv_obj_set_style_border_color(sw, lv_color_hex(UI_C_BG), 0);
            lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(sw, swatch_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)((i << 8) | c));
            s_swatch[i] = sw;
        }
    }

    s_slider[i] = ui_slider(card, 1, 100, l->brightness, slider_cb, (void *)(intptr_t)i);
}

lv_obj_t * ui_Screen_Lights_create(lv_obj_t * parent)
{
    lv_obj_t * head = ui_card(parent, 0x1B2547, UI_C_CARD_2);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * left = ui_col(head);
    lv_obj_set_flex_grow(left, 1);
    ui_label(left, "全部灯光", UI_FONT_CN, UI_C_TEXT);
    s_summary = ui_label(left, "--", UI_FONT_CN, UI_C_TEXT_DIM);

    /* 两个按钮靠最右边 */
    lv_obj_t * btns = ui_row(head);
    lv_obj_set_width(btns, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_button(btns, "全部打开", UI_C_ACCENT, all_on_cb, NULL);
    ui_button(btns, "全部关闭", UI_C_BORDER, all_off_cb, NULL);

    lv_obj_t * grid = lv_obj_create(parent);
    ui_transparent(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, UI_GAP, 0);

    for (int i = 0; i < DM_LIGHT_COUNT; i++) {
        build_light_card(grid, i);
    }

    ui_Screen_Lights_refresh();
    return parent;
}

void ui_Screen_Lights_refresh(void)
{
    if (s_summary == NULL) {
        return;
    }

    dm_light_t * l = dm_lights();
    int on = 0;
    static int8_t last_on[DM_LIGHT_COUNT] = { -1, -1, -1, -1, -1, -1 };
    static uint8_t last_bri[DM_LIGHT_COUNT] = { 0 };

    for (int i = 0; i < DM_LIGHT_COUNT; i++) {
        if (l[i].on) {
            on++;
        }
        if (s_toggle[i] != NULL) {
            if (l[i].on) {
                lv_obj_add_state(s_toggle[i], LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(s_toggle[i], LV_STATE_CHECKED);
            }
        }
        /* 值没变就别写：滑条/文本的颜色写入都会触发局部重绘 */
        if (s_slider[i] != NULL && !lv_obj_has_state(s_slider[i], LV_STATE_PRESSED) &&
                lv_slider_get_value(s_slider[i]) != (int32_t)l[i].brightness) {
            lv_slider_set_value(s_slider[i], l[i].brightness, LV_ANIM_OFF);
        }
        if (s_value[i] != NULL) {
            if (l[i].on) {
                ui_label_set_fmt_if_changed(s_value[i], "亮度 %d%%", l[i].brightness);
            } else {
                ui_label_set_text_if_changed(s_value[i], "已关闭");
            }
            if (last_on[i] != (int8_t)l[i].on) {
                last_on[i] = (int8_t)l[i].on;
                lv_obj_set_style_text_color(s_value[i],
                                            lv_color_hex(l[i].on ? UI_C_TEXT_DIM : UI_C_TEXT_MUTE), 0);
            }
        }
        if (s_swatch[i] != NULL && last_bri[i] == 0xFF) {
            /* 只处理一次（建卡时已设好样式） */
            last_bri[i] = 0;
        }
    }
    ui_label_set_fmt_if_changed(s_summary, "已开启 %d / %d 路", on, DM_LIGHT_COUNT);
}
