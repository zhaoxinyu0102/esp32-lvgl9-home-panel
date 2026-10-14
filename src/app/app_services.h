/**
 * @file app_services.h
 * @brief 应用层服务：把 WiFi / 时间 / 天气 / 设备模型串起来，并驱动 UI 刷新。
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 启动后台任务（在 ui_init() 之后调用一次）。
 *
 * 任务以 250ms 为节拍：
 *   - 每拍：WiFi 状态机、设备模型推进（能耗/室温/窗帘）；
 *   - 每 4 拍（1 秒）：持 LVGL 锁刷新界面（状态栏时间、当前屏数据）。
 */
void app_services_start(void);

#ifdef __cplusplus
}
#endif
