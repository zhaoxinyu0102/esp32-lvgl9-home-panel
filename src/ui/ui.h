/**
 * @file ui.h
 * @brief 界面总入口与跨屏接口。
 *
 * 结构上等同 SquareLine 的导出：屏在 screens/，公共部件在 components/，
 * 只是这些代码是手写的（见 PORTING/README 说明），对外只暴露 ui_init()。
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 屏幕 ID（顺序就是底部导航的顺序）
 * ---------------------------------------------------------------------- */
typedef enum {
    UI_SCREEN_DASHBOARD = 0,
    UI_SCREEN_LIGHTS,
    UI_SCREEN_CLIMATE,
    UI_SCREEN_CURTAINS,
    UI_SCREEN_ENERGY,
    UI_SCREEN_COFFEE,
    UI_SCREEN_WEATHER,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_COUNT
} ui_screen_id_t;

/** 建好全部屏幕并显示首页（在 LVGL 锁内、lvgl_port_init 之后调用一次） */
void ui_init(void);

/** 切屏（带淡入淡出 + 更新底部导航高亮） */
void ui_nav_goto(ui_screen_id_t id);

/** 当前屏幕 */
ui_screen_id_t ui_nav_current(void);

/** 取某个屏幕对象（已创建，可能还没显示过） */
lv_obj_t * ui_screen_obj(ui_screen_id_t id);

/**
 * 刷新所有动态内容（状态栏时间/天气/WiFi、当前屏的数据）。
 * 由 app 任务每秒调用一次，调用方必须已经持有 LVGL 锁。
 */
void ui_refresh(void);

/** 底部弹出的提示条（2 秒后自动消失），用于"已连接""场景已应用"之类 */
void ui_toast(const char * msg);
void ui_toastf(const char * fmt, ...);

/**
 * 从**任意任务**安全地排一条提示（带小队列，不阻塞、不需要持锁）。
 * 手机网页操作、串口命令都会走它；真正的弹出由 LVGL 任务里的 ui_toast_flush() 完成。
 */
void ui_toast_async(const char * msg);

/** 队列里还有没有待弹的提示（调用方无需持锁） */
bool ui_toast_pending(void);

/** 把排队的提示真正弹出来（调用方**必须**持有 LVGL 锁） */
void ui_toast_flush(void);

/* -------------------------------------------------------------------------
 * 各屏（screens/ 下实现）
 * ---------------------------------------------------------------------- */
lv_obj_t * ui_Screen_Dashboard_create(lv_obj_t * parent);
void       ui_Screen_Dashboard_refresh(void);

lv_obj_t * ui_Screen_Lights_create(lv_obj_t * parent);
void       ui_Screen_Lights_refresh(void);

lv_obj_t * ui_Screen_Climate_create(lv_obj_t * parent);
void       ui_Screen_Climate_refresh(void);

lv_obj_t * ui_Screen_Curtains_create(lv_obj_t * parent);
void       ui_Screen_Curtains_refresh(void);

lv_obj_t * ui_Screen_Energy_create(lv_obj_t * parent);
void       ui_Screen_Energy_refresh(void);

lv_obj_t * ui_Screen_Coffee_create(lv_obj_t * parent);
void       ui_Screen_Coffee_refresh(void);

lv_obj_t * ui_Screen_Weather_create(lv_obj_t * parent);
void       ui_Screen_Weather_refresh(void);

lv_obj_t * ui_Screen_Settings_create(lv_obj_t * parent);
void       ui_Screen_Settings_refresh(void);

#ifdef __cplusplus
}
#endif
