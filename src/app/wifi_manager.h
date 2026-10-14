/**
 * @file wifi_manager.h
 * @brief WiFi 管理：连接、自动重连、扫描、凭据存 NVS。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_ST_DISCONNECTED = 0,
    WIFI_ST_CONNECTING,
    WIFI_ST_CONNECTED,
} wifi_state_t;

/** 读 NVS 里的凭据（有的话）并尝试连接；调用后不会阻塞 */
void wifi_manager_init(void);

/** 在 app 任务里周期调用：处理重连与状态迁移 */
void wifi_manager_tick(void);

wifi_state_t wifi_manager_state(void);
int8_t       wifi_manager_rssi(void);
const char * wifi_manager_ssid(void);

/* ---- 板子自己的热点（手机直连控制用） ---- */
bool         wifi_manager_ap_enabled(void);
const char * wifi_manager_ap_ssid(void);
const char * wifi_manager_ap_pass(void);
const char * wifi_manager_ap_ip(void);
int          wifi_manager_ap_clients(void);
const char * wifi_manager_ip(void);
bool         wifi_manager_has_saved(void);

/** 连接（save=true 时写入 NVS 供下次开机自动连）；会先断开旧连接 */
bool wifi_manager_connect(const char * ssid, const char * password, bool save);

/** 忘记已保存的网络 */
void wifi_manager_forget(void);

/**
 * 扫描附近 AP。
 * @param out      二维数组，每行至少 33 字节
 * @param max      最多返回多少个
 * @param rssi_out 可为 NULL，返回信号强度
 * @return 实际数量
 */
int wifi_manager_scan(char out[][33], int8_t * rssi_out, int max);

#ifdef __cplusplus
}
#endif
