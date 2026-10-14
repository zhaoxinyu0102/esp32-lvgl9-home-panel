/**
 * @file ui_comp_navbar.c
 * @brief 底部导航栏实现（句柄式）。
 */

#include "ui_comp_navbar.h"

#include <stdlib.h>

#include "ui_theme.h"
#include "images/ui_images.h"
#include "images/coffee/ui_coffee_images.h"

typedef struct {
    const char * label;
    const void * icon;
} nav_item_t;

static const nav_item_t ITEMS[UI_SCREEN_COUNT] = {
    { "首页", &ui_img_nav_home },
    { "灯光", &ui_img_nav_bulb },
    { "空调", &ui_img_nav_ac },
    { "窗帘", &ui_img_nav_curtain },
    { "能耗", &ui_img_nav_bolt },
    { "咖啡", &ui_img_cup_png },
    { "天气", &ui_img_nav_cloud },
    { "设置", &ui_img_nav_gear },
};

typedef struct {
    lv_obj_t * item;
    lv_obj_t * icon;
    lv_obj_t * label;
    lv_obj_t * indicator;
} nav_slot_t;

typedef struct {
    nav_slot_t    slot[UI_SCREEN_COUNT];
    ui_screen_id_t active;
} nav_ctx_t;

static nav_ctx_t * ctx_of(lv_obj_t * navbar)
{
    return (navbar != NULL) ? (nav_ctx_t *)lv_obj_get_user_data(navbar) : NULL;
}

static void nav_item_cb(lv_event_t * e)
{
    ui_screen_id_t id = (ui_screen_id_t)(intptr_t)lv_event_get_user_data(e);
    ui_nav_goto(id);
}

lv_obj_t * ui_comp_navbar_create(lv_obj_t * parent)
{
    nav_ctx_t * ctx = (nav_ctx_t *)calloc(1, sizeof(nav_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }
    ctx->active = UI_SCREEN_COUNT;      /* 保证第一次一定刷新样式 */

    lv_obj_t * bar = lv_obj_create(parent);
    ui_transparent(bar);
    lv_obj_set_user_data(bar, ctx);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, UI_NAV_H);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    /* 底部同样做"玻璃"：上沿亮、往下更深 */
    ui_gradient(bar, UI_C_BG, UI_C_CARD);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(UI_C_BORDER), 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_70, 0);

    for (int i = 0; i < UI_SCREEN_COUNT; i++) {
        lv_obj_t * item = lv_obj_create(bar);
        ui_transparent(item);
        lv_obj_set_size(item, 92, UI_NAV_H - 10);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(item, 2, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_style_radius(item, 14, 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(item, lv_color_hex(UI_C_ACCENT), LV_STATE_PRESSED);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(item, nav_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t * ind = lv_obj_create(item);
        ui_transparent(ind);
        lv_obj_set_size(ind, 22, 3);
        lv_obj_set_style_radius(ind, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ind, lv_color_hex(UI_C_ACCENT), 0);
        lv_obj_set_style_bg_opa(ind, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(ind, 10, 0);
        lv_obj_set_style_shadow_opa(ind, LV_OPA_50, 0);
        lv_obj_set_style_shadow_color(ind, lv_color_hex(UI_C_ACCENT), 0);

        lv_obj_t * icon = lv_image_create(item);
        lv_image_set_src(icon, ITEMS[i].icon);
        lv_image_set_scale(icon, 104);          /* 48 -> 20px */
        lv_obj_set_style_image_recolor(icon, lv_color_hex(UI_C_TEXT_DIM), 0);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);

        lv_obj_t * label = ui_label(item, ITEMS[i].label, UI_FONT_CN, UI_C_TEXT_DIM);

        ctx->slot[i].item = item;
        ctx->slot[i].icon = icon;
        ctx->slot[i].label = label;
        ctx->slot[i].indicator = ind;
    }

    ui_comp_navbar_set_active(bar, UI_SCREEN_DASHBOARD);
    return bar;
}

void ui_comp_navbar_set_active(lv_obj_t * navbar, ui_screen_id_t id)
{
    nav_ctx_t * ctx = ctx_of(navbar);
    if (ctx == NULL || id >= UI_SCREEN_COUNT || id == ctx->active) {
        return;
    }
    ctx->active = id;

    for (int i = 0; i < UI_SCREEN_COUNT; i++) {
        bool on = (i == (int)id);
        lv_obj_set_style_bg_opa(ctx->slot[i].item, on ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(ctx->slot[i].item, lv_color_hex(UI_C_ACCENT), 0);
        lv_obj_set_style_image_recolor(ctx->slot[i].icon, lv_color_hex(on ? UI_C_ACCENT : UI_C_TEXT_DIM), 0);
        lv_obj_set_style_text_color(ctx->slot[i].label, lv_color_hex(on ? UI_C_ACCENT : UI_C_TEXT_DIM), 0);
        lv_obj_set_style_bg_opa(ctx->slot[i].indicator, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}
