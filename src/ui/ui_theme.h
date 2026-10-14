/**
 * @file ui_theme.h
 * @brief 设计系统：配色、字体、通用控件构建器。
 *
 * 所有屏幕都用这里的构建器搭，改配色/圆角/间距只需要改这一个文件。
 * 风格：深色玻璃拟态（深蓝底 + 半透明卡片 + 蓝色/紫色渐变强调色）。
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 调色板（与网页面板同一套色，风格统一）
 * ---------------------------------------------------------------------- */
#define UI_C_BG             0x070A14    /* 页面底色（更深，衬托卡片） */
#define UI_C_BG_2           0x101733    /* 页面渐变下沿 */
#define UI_C_CARD           0x151D36    /* 卡片 */
#define UI_C_CARD_2         0x1D2748    /* 卡片渐变下沿 / 次级卡片 */
#define UI_C_BORDER         0x2C3860    /* 描边（比底色亮一档，分隔更清晰） */
#define UI_C_TEXT           0xEAF0FF    /* 主文字 */
#define UI_C_TEXT_DIM       0x96A3C6    /* 次要文字 */
#define UI_C_TEXT_MUTE      0x5D6A8E    /* 更弱的文字/单位 */
#define UI_C_ACCENT         0x4F8CFF    /* 主强调色（蓝） */
#define UI_C_ACCENT_2       0x8B5CF6    /* 次强调色（紫） */
#define UI_C_CYAN           0x22D3EE    /* 冷色点缀 */
#define UI_C_OK             0x22C55E    /* 开/正常 */
#define UI_C_WARN           0xF59E0B    /* 警告 */
#define UI_C_DANGER         0xEF4444    /* 告警/布防 */
#define UI_C_WARM           0xFF8A3D    /* 暖色（制热/暖光） */

/* 布局常量 */
#define UI_PAD              16          /* 屏幕外边距 */
#define UI_GAP              14          /* 控件间距（比上一版更舒展） */
#define UI_RADIUS           20          /* 卡片圆角（更圆润） */
#define UI_STATUS_H         60          /* 状态栏高度 */
#define UI_NAV_H            88          /* 底部导航高度（要放得下图标 + 24px 中文标签）*/

/* -------------------------------------------------------------------------
 * 字体
 * ---------------------------------------------------------------------- */
extern const lv_font_t * UI_FONT_NUM_XL;    /* 48 数字（大温度） */
extern const lv_font_t * UI_FONT_NUM_L;     /* 32 数字/大标题（英文） */
extern const lv_font_t * UI_FONT_NUM_M;     /* 24 数字 */
extern const lv_font_t * UI_FONT_H1;        /* 20 小标题（英文） */
extern const lv_font_t * UI_FONT_BODY;      /* 16 正文（英文） */
extern const lv_font_t * UI_FONT_SMALL;     /* 14 说明 */

/**
 * 中文（SquareLine 工程里那份 SourceHanSans 24px 字库，见 src/lv_font_cjk_24.c）。
 * 中文一律用它；它同时含 ASCII，所以中英混排不会缺字。
 */
extern const lv_font_t * UI_FONT_CN;        /* 24 中文正文 */
extern const lv_font_t * UI_FONT_CN_SMALL;  /* 也指向 24px（只有一个字号可用） */

/** 初始化字体指针（ui_init 里第一件事就是调它） */
void ui_theme_init(void);

/* -------------------------------------------------------------------------
 * 通用控件构建器
 * ---------------------------------------------------------------------- */

/** 页面根容器：铺满 + 深色渐变底 + 竖向 flex（给状态栏/内容/导航留位由屏幕自己排） */
lv_obj_t * ui_screen_root(lv_obj_t * screen);

/** 卡片：圆角、描边、上下渐变，默认竖向 flex、内边距 UI_PAD */
lv_obj_t * ui_card(lv_obj_t * parent, uint32_t top_color, uint32_t bottom_color);

/** 横向/纵向 flex 容器（透明无样式） */
lv_obj_t * ui_row(lv_obj_t * parent);
lv_obj_t * ui_col(lv_obj_t * parent);

/** 文本标签 */
lv_obj_t * ui_label(lv_obj_t * parent, const char * text, const lv_font_t * font, uint32_t color);

/**
 * 更新标签文本（内容相同则什么都不做）。
 * 每秒刷新界面时用它们，可以避免无意义的失效重绘 -- 对流畅度影响很大。
 */
void ui_label_set_text_if_changed(lv_obj_t * label, const char * text);
void ui_label_set_fmt_if_changed(lv_obj_t * label, const char * fmt, ...);

/** 图标标签（用 LVGL 内置符号，见 LV_SYMBOL_xxx；字体决定大小） */
lv_obj_t * ui_icon(lv_obj_t * parent, const char * symbol, const lv_font_t * font, uint32_t color);

/** 小胶囊标签，例如 "已开启" / "26°C" */
lv_obj_t * ui_chip(lv_obj_t * parent, const char * text, uint32_t color);

/** 开关（LVGL switch，带强调色样式）；checked 初始状态 */
lv_obj_t * ui_toggle(lv_obj_t * parent, bool checked, lv_event_cb_t cb, void * user_data);

/** 通用按钮（圆角、强调色底、点击回调） */
lv_obj_t * ui_button(lv_obj_t * parent, const char * text, uint32_t color, lv_event_cb_t cb, void * user_data);

/** 细长的图标按钮（用在卡片右上角的 ⚙ 之类） */
lv_obj_t * ui_icon_button(lv_obj_t * parent, const char * symbol, lv_event_cb_t cb, void * user_data);

/** 滑条（带强调色指示条与旋钮） */
lv_obj_t * ui_slider(lv_obj_t * parent, int32_t min, int32_t max, int32_t value,
                     lv_event_cb_t cb, void * user_data);

/** 一条"图标 + 标题 + 右侧值/开关"的列表行；返回行容器，右侧控件按需自己加 */
lv_obj_t * ui_list_row(lv_obj_t * parent, const char * symbol, const char * title, const char * value);

/** 卡片顶部一行的"标题 + 右侧小字"，返回标题 label（值 label 通过 out 返回，可传 NULL） */
lv_obj_t * ui_card_header(lv_obj_t * card, const char * title, const char * right_text, lv_obj_t ** out_right);

/** 把对象底色设成上下渐变 */
void ui_gradient(lv_obj_t * obj, uint32_t top_color, uint32_t bottom_color);

/** 容器透明化（去掉默认白底/边框/内边距） */
void ui_transparent(lv_obj_t * obj);

/** 让子对象均匀分布：横向等分（给底部导航用） */
void ui_flex_fill_row(lv_obj_t * obj);

#ifdef __cplusplus
}
#endif
