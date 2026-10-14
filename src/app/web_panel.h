/**
 * @file web_panel.h
 * @brief ESP32 内置网页控制面板：手机浏览器直接打开，无需 App。
 *
 * - GET  /           返回内嵌的 index.html（src/web/index.html -> src/app/index_html.h）
 * - GET  /api/state  一次性返回设备状态 JSON
 * - POST /api        接收控制命令 JSON（{"c":"light","id":0,"on":1,...}）
 * - GET  /events     服务器推送（SSE）：状态变了就往手机推 JSON
 * - mDNS             主机名 smart-home.local
 *
 * 和屏幕 UI 共用同一个 device_model，两者实时同步。
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** 上电后调用（WiFi 起来即可被访问；即使还没连上，服务也先挂着） */
void web_panel_init(void);

/** 后台任务里高频调用（处理 HTTP 请求 / SSE 保活） */
void web_panel_loop(void);

/** 状态变化时调用（内部节流 + 去重，放心高频调） */
void web_panel_push_state(void);

#ifdef __cplusplus
}
#endif
