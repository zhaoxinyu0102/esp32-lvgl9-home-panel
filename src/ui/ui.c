/**
 * @file ui.c
 * @brief 界面总入口：建全部屏幕（每屏自带状态栏 + 底部导航）、切屏、每秒刷新、提示条。
 */

#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "ui_theme.h"
#include "components/ui_comp_statusbar.h"
#include "components/ui_comp_navbar.h"
#include "images/ui_images.h"

#include "screens/ui_Screen_Dashboard.h"
#include "screens/ui_Screen_Lights.h"
#include "screens/ui_Screen_Climate.h"
#include "screens/ui_Screen_Curtains.h"
#include "screens/ui_Screen_Energy.h"
#include "screens/ui_Screen_Coffee.h"
#include "screens/ui_Screen_Weather.h"
#include "screens/ui_Screen_Settings.h"

#include "app_config.h"
#include "app_events.h"
#include "time_service.h"
#include "weather_service.h"
#include "wifi_manager.h"

typedef lv_obj_t * (*screen_build_fn)(lv_obj_t *);
typedef void (*screen_refresh_fn)(void);

static const screen_build_fn BUILDERS[UI_SCREEN_COUNT] = {
    ui_Screen_Dashboard_create,
    ui_Screen_Lights_create,
    ui_Screen_Climate_create,
    ui_Screen_Curtains_create,
    ui_Screen_Energy_create,
    ui_Screen_Coffee_create,
    ui_Screen_Weather_create,
    ui_Screen_Settings_create,
};

static const screen_refresh_fn REFRESH[UI_SCREEN_COUNT] = {
    ui_Screen_Dashboard_refresh,
    ui_Screen_Lights_refresh,
    ui_Screen_Climate_refresh,
    ui_Screen_Curtains_refresh,
    ui_Screen_Energy_refresh,
    ui_Screen_Coffee_refresh,
    ui_Screen_Weather_refresh,
    ui_Screen_Settings_refresh,
};

static lv_obj_t *     s_screens[UI_SCREEN_COUNT];
static lv_obj_t *     s_statusbar[UI_SCREEN_COUNT];
static lv_obj_t *     s_navbar[UI_SCREEN_COUNT];
static bool           s_ready[UI_SCREEN_COUNT];
static ui_screen_id_t s_current = UI_SCREEN_COUNT;

static lv_obj_t *   s_toast;
static lv_obj_t *   s_toast_label;
static lv_timer_t * s_toast_timer;

/* -------------------------------------------------------------------------
 * 提示条
 * ---------------------------------------------------------------------- */
static void toast_timeout_cb(lv_timer_t * t)
{
    (void)t;
    if (s_toast != NULL) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_pause(s_toast_timer);
}

void ui_toast(const char * msg)
{
    if (s_toast == NULL || s_toast_label == NULL) {
        return;
    }
    ui_label_set_text_if_changed(s_toast_label, msg);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);
    lv_timer_reset(s_toast_timer);
    lv_timer_resume(s_toast_timer);
}

void ui_toastf(const char * fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ui_toast(buf);
}

/* -------------------------------------------------------------------------
 * 异步提示队列
 *
 * 手机网页/串口命令是在 app 任务里改状态的，那里没有 LVGL 锁、也不能阻塞，
 * 所以只把文案排进队列，真正的显示留给拿到锁的一方调用 ui_toast_flush()。
 * ---------------------------------------------------------------------- */
#define TOAST_Q_LEN 8
#define TOAST_Q_TEXT 72

static char             s_toast_q[TOAST_Q_LEN][TOAST_Q_TEXT];
static volatile uint8_t s_toast_head = 0;
static volatile uint8_t s_toast_tail = 0;

void ui_toast_async(const char * msg)
{
    if (msg == NULL || msg[0] == '\0') {
        return;
    }
    uint8_t next = (uint8_t)((s_toast_head + 1) % TOAST_Q_LEN);
    if (next == s_toast_tail) {
        return;                     /* 队列满就丢，绝不阻塞调用方 */
    }
    strncpy(s_toast_q[s_toast_head], msg, TOAST_Q_TEXT - 1);
    s_toast_q[s_toast_head][TOAST_Q_TEXT - 1] = '\0';
    s_toast_head = next;
}

bool ui_toast_pending(void)
{
    return s_toast_tail != s_toast_head;
}

void ui_toast_flush(void)
{
    if (s_toast_tail == s_toast_head) {
        return;
    }
    ui_toast(s_toast_q[s_toast_tail]);
    s_toast_tail = (uint8_t)((s_toast_tail + 1) % TOAST_Q_LEN);
}

/* -------------------------------------------------------------------------
 * 建屏
 * ---------------------------------------------------------------------- */
/* -------------------------------------------------------------------------
 * 手势：左右滑动切换屏幕（像手机翻页）
 * 正在拖滑条/圆弧/图表时不触发，免得调亮度被当成翻页
 * ---------------------------------------------------------------------- */
static bool is_dragging_widget(lv_indev_t * indev)
{
    lv_obj_t * act = lv_indev_get_active_obj();  /* LVGL 9.5: 无参 */
    if (act == NULL) {
        return false;
    }
    return lv_obj_check_type(act, &lv_slider_class)
           || lv_obj_check_type(act, &lv_arc_class)
           || lv_obj_check_type(act, &lv_chart_class)
           || lv_obj_check_type(act, &lv_textarea_class)
           || lv_obj_check_type(act, &lv_keyboard_class);
}

static void screen_gesture_cb(lv_event_t * e)
{
    (void)e;
    lv_indev_t * indev = lv_indev_active();
    if (indev == NULL || is_dragging_widget(indev)) {
        return;
    }
    if (s_current >= UI_SCREEN_COUNT) {
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        ui_nav_goto((ui_screen_id_t)((s_current + 1) % UI_SCREEN_COUNT));
    } else if (dir == LV_DIR_RIGHT) {
        ui_nav_goto((ui_screen_id_t)((s_current + UI_SCREEN_COUNT - 1) % UI_SCREEN_COUNT));
    }
}

static lv_obj_t * make_screen(int idx)
{
    /* printf 走 IDF console（UART），串口监视器可见，用来定位"卡在哪一屏" */
    printf("[ui] 创建屏幕 %d ...\n", idx);

    lv_obj_t * scr = lv_obj_create(NULL);
    ui_screen_root(scr);

    /* 让手势事件冒泡到屏幕，才能左右滑动翻页 */
    lv_obj_add_flag(scr, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(scr, screen_gesture_cb, LV_EVENT_GESTURE, NULL);

    s_statusbar[idx] = ui_comp_statusbar_create(scr);

    lv_obj_t * content = lv_obj_create(scr);
    ui_transparent(content);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(content, UI_PAD, 0);
    lv_obj_set_style_pad_row(content, UI_GAP, 0);
    lv_obj_set_style_pad_right(content, UI_PAD + 4, 0);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(content, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(content, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(content, lv_color_hex(UI_C_BORDER), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, LV_PART_SCROLLBAR);

    printf("[ui]   屏幕 %d 内容构建中...\n", idx);
    BUILDERS[idx](content);
    printf("[ui]   屏幕 %d 底部导航...\n", idx);

    s_navbar[idx] = ui_comp_navbar_create(scr);
    s_ready[idx] = true;
    printf("[ui] 屏幕 %d 完成 (内部RAM剩余 %u B, PSRAM剩余 %u B)\n",
           idx, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return scr;
}

/**
 * 按需建屏：8 个屏如果开机全部建好，LVGL 对象 + 局部样式会一次性占用大量堆内存，
 * 既拖慢开机也容易在内存紧张时出问题。这里只立即建当前屏，其余首次切过去时再建。
 */
static lv_obj_t * ensure_screen(ui_screen_id_t id)
{
    if (id >= UI_SCREEN_COUNT) {
        return NULL;
    }
    if (s_screens[id] == NULL) {
        s_screens[id] = make_screen((int)id);
    }
    return s_screens[id];
}

/* -------------------------------------------------------------------------
 * 状态栏刷新（每个屏各自一份，所以按需刷新对应的那一份）
 * ---------------------------------------------------------------------- */
static void update_statusbar(ui_screen_id_t id)
{
    if (id >= UI_SCREEN_COUNT || s_statusbar[id] == NULL) {
        return;
    }

    char time_text[8] = { 0 };
    char date_text[16] = { 0 };
    char week_text[8] = { 0 };
    time_service_format(time_text, sizeof(time_text), date_text, sizeof(date_text), week_text, sizeof(week_text));
    ui_comp_statusbar_set_time(s_statusbar[id], time_text, date_text, week_text);

    bool conn = (wifi_manager_state() == WIFI_ST_CONNECTED);
    ui_comp_statusbar_set_wifi(s_statusbar[id], conn, wifi_manager_rssi());

    const weather_t * w = weather_service_get();
    if (w->valid) {
        char temp[8];
        snprintf(temp, sizeof(temp), "%d°", w->temp);
        ui_comp_statusbar_set_weather(s_statusbar[id], weather_icon(w->kind), temp, w->text);
    }
}

/* -------------------------------------------------------------------------
 * 对外接口
 * ---------------------------------------------------------------------- */
void ui_init(void)
{
    printf("[ui] ui_init 开始（只建首页，其余屏首次进入时再建）\n");
    ui_theme_init();

    ensure_screen(UI_SCREEN_DASHBOARD);

    /* 提示条挂在顶层，切屏不会消失 */
    s_toast = lv_obj_create(lv_layer_top());
    ui_transparent(s_toast);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, 44);
    lv_obj_set_style_pad_hor(s_toast, 20, 0);
    lv_obj_set_style_radius(s_toast, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x1E2746), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_border_color(s_toast, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_border_opa(s_toast, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(s_toast, 24, 0);
    lv_obj_set_style_shadow_opa(s_toast, LV_OPA_40, 0);
    lv_obj_set_style_shadow_color(s_toast, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(s_toast, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_toast, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -(UI_NAV_H + 18));
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    s_toast_label = ui_label(s_toast, "", UI_FONT_CN, UI_C_TEXT);

    s_toast_timer = lv_timer_create(toast_timeout_cb, 2200, NULL);
    lv_timer_pause(s_toast_timer);

    /* 显示首页 */
    s_current = UI_SCREEN_DASHBOARD;
    lv_screen_load(s_screens[UI_SCREEN_DASHBOARD]);
    ui_comp_navbar_set_active(s_navbar[UI_SCREEN_DASHBOARD], UI_SCREEN_DASHBOARD);
    update_statusbar(UI_SCREEN_DASHBOARD);
    printf("[ui] ui_init 完成，已显示首页\n");
}

void ui_nav_goto(ui_screen_id_t id)
{
    if (id >= UI_SCREEN_COUNT || id == s_current) {
        return;
    }

    if (ensure_screen(id) == NULL) {
        return;
    }

    REFRESH[id]();

    update_statusbar(id);

    /* 注意：这里**必须用 lv_screen_load()**，不能用 lv_screen_load_anim()。
     * ui_nav_goto() 是在 LVGL 任务里、由 lv_timer_handler() 内部调用的（触摸事件回调 /
     * lv_async_call 回调），此时 lv_screen_load_anim() 会新建动画定时器并删除旧屏，
     * 破坏 LVGL 正在遍历的定时器链表 -> 直接死循环卡死（安防屏/首页复现）。 */
    lv_screen_load(s_screens[id]);

    s_current = id;
    ui_comp_navbar_set_active(s_navbar[id], id);

    ev_screen((int)id);          /* 上报"切到了哪个界面" */
}

ui_screen_id_t ui_nav_current(void)
{
    return s_current;
}

lv_obj_t * ui_screen_obj(ui_screen_id_t id)
{
    return (id < UI_SCREEN_COUNT) ? s_screens[id] : NULL;
}

void ui_refresh(void)
{
    if (s_current >= UI_SCREEN_COUNT || !s_ready[s_current]) {
        return;
    }
    update_statusbar(s_current);
    REFRESH[s_current]();
}
