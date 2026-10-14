/**
 * @file ui_Screen_Curtains.c
 * @brief 窗帘控制：3 路，进度滑条 + 开/停/关。
 */

#include "ui_Screen_Curtains.h"

#include "ui.h"
#include "ui_theme.h"

#include "device_model.h"

static lv_obj_t * s_pos_label[DM_CURTAIN_COUNT];
static lv_obj_t * s_bar[DM_CURTAIN_COUNT];
static lv_obj_t * s_slider[DM_CURTAIN_COUNT];
static lv_obj_t * s_state[DM_CURTAIN_COUNT];

static void open_cb(lv_event_t * e)  { dm_curtain_open((int)(intptr_t)lv_event_get_user_data(e)); ui_Screen_Curtains_refresh(); }
static void close_cb(lv_event_t * e) { dm_curtain_close((int)(intptr_t)lv_event_get_user_data(e)); ui_Screen_Curtains_refresh(); }
static void stop_cb(lv_event_t * e)  { dm_curtain_stop((int)(intptr_t)lv_event_get_user_data(e)); ui_Screen_Curtains_refresh(); }

static void slider_cb(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    dm_curtain_set(i, (int)lv_slider_get_value(lv_event_get_target(e)));
    ui_Screen_Curtains_refresh();
}

static void build_curtain(lv_obj_t * parent, int i)
{
    dm_curtain_t * c = &dm_curtains()[i];

    lv_obj_t * card = ui_card(parent, UI_C_CARD, UI_C_CARD_2);
    lv_obj_set_width(card, LV_PCT(31));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui_label(card, c->name, UI_FONT_CN, UI_C_TEXT);

    /* 竖直示意条 + 百分比 */
    lv_obj_t * visual = lv_obj_create(card);
    ui_transparent(visual);
    lv_obj_set_size(visual, 56, 130);
    lv_obj_set_style_radius(visual, 14, 0);
    lv_obj_set_style_bg_color(visual, lv_color_hex(0x0E1428), 0);
    lv_obj_set_style_bg_opa(visual, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(visual, 1, 0);
    lv_obj_set_style_border_color(visual, lv_color_hex(UI_C_BORDER), 0);
    lv_obj_set_flex_flow(visual, LV_FLEX_FLOW_COLUMN_REVERSE);
    lv_obj_set_flex_align(visual, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(visual, 4, 0);

    lv_obj_t * bar = lv_obj_create(visual);
    ui_transparent(bar);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, 40);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_bg_grad_color(bar, lv_color_hex(UI_C_CYAN), 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_80, 0);

    lv_obj_t * pct_row = ui_row(card);
    lv_obj_set_style_pad_column(pct_row, 2, 0);
    s_pos_label[i] = ui_label(pct_row, "0", UI_FONT_NUM_L, UI_C_TEXT);
    ui_label(pct_row, "%", UI_FONT_CN, UI_C_TEXT_DIM);
    s_state[i] = ui_label(card, "停止", UI_FONT_SMALL, UI_C_TEXT_MUTE);

    s_slider[i] = ui_slider(card, 0, 100, c->position, slider_cb, (void *)(intptr_t)i);

    lv_obj_t * btns = ui_row(card);
    lv_obj_set_style_pad_column(btns, 6, 0);
    lv_obj_t * b1 = ui_button(btns, "打开", UI_C_ACCENT, open_cb, (void *)(intptr_t)i);
    lv_obj_t * b2 = ui_button(btns, "停止", UI_C_BORDER, stop_cb, (void *)(intptr_t)i);
    lv_obj_t * b3 = ui_button(btns, "关闭", UI_C_ACCENT_2, close_cb, (void *)(intptr_t)i);
    /* 三个按钮均分整行宽度。
     * 原来是固定 74px：而 ui_button 的内边距是 22px，"关闭"两个字就 48px，
     * 48 + 44 = 92px > 74px —— 文字被按钮边框截掉，才会看到"关闭"缺一半。 */
    lv_obj_t * bs[3] = { b1, b2, b3 };
    for (int k = 0; k < 3; k++) {
        lv_obj_set_height(bs[k], 40);
        lv_obj_set_width(bs[k], 0);
        lv_obj_set_flex_grow(bs[k], 1);
        lv_obj_set_style_pad_hor(bs[k], 6, 0);
    }

    s_bar[i] = bar;
}

lv_obj_t * ui_Screen_Curtains_create(lv_obj_t * parent)
{
    lv_obj_t * row = ui_row(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    for (int i = 0; i < DM_CURTAIN_COUNT; i++) {
        build_curtain(row, i);
    }

    ui_Screen_Curtains_refresh();
    return parent;
}

void ui_Screen_Curtains_refresh(void)
{
    if (s_pos_label[0] == NULL) {
        return;
    }

    dm_curtain_t * cs = dm_curtains();
    static int8_t last_dir[DM_CURTAIN_COUNT] = { 0xFF, 0xFF, 0xFF };

    for (int i = 0; i < DM_CURTAIN_COUNT; i++) {
        ui_label_set_fmt_if_changed(s_pos_label[i], "%d", cs[i].position);

        /* 进度条高度只在百分比变化时改（改高度会重绘） */
        int32_t bar_h = (int32_t)(cs[i].position * 12 / 10) + 6;
        if (lv_obj_get_height(s_bar[i]) != bar_h) {
            lv_obj_set_height(s_bar[i], bar_h);
        }

        if (cs[i].dir > 0) {
            ui_label_set_text_if_changed(s_state[i], "正在打开...");
        } else if (cs[i].dir < 0) {
            ui_label_set_text_if_changed(s_state[i], "正在关闭...");
        } else {
            ui_label_set_text_if_changed(s_state[i],
                                         (cs[i].position == 0) ? "已关闭" : (cs[i].position == 100 ? "已打开" : "暂停"));
        }

        static const uint32_t STATE_COLOR[3] = { UI_C_TEXT_MUTE, UI_C_OK, UI_C_WARN };
        int8_t dir_slot = (cs[i].dir > 0) ? 1 : ((cs[i].dir < 0) ? 2 : 0);
        if (last_dir[i] != dir_slot) {
            last_dir[i] = dir_slot;
            lv_obj_set_style_text_color(s_state[i], lv_color_hex(STATE_COLOR[dir_slot]), 0);
        }

        if (!lv_obj_has_state(s_slider[i], LV_STATE_PRESSED) &&
                lv_slider_get_value(s_slider[i]) != (int32_t)cs[i].position) {
            lv_slider_set_value(s_slider[i], cs[i].position, LV_ANIM_OFF);
        }
    }
}
