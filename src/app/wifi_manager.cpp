/**
 * @file wifi_manager.cpp
 * @brief WiFi 管理实现（Arduino WiFi + Preferences/NVS）。
 *
 * 说明：文件名是 .cpp，因为 WiFi.h / Preferences.h 都是 C++ 接口。
 */

#include "wifi_manager.h"

#include <string.h>

#include <WiFi.h>
#include <Preferences.h>

#include "app_config.h"
#include "app_log.h"

static wifi_state_t s_state = WIFI_ST_DISCONNECTED;
static char         s_ssid[33] = { 0 };
static char         s_pass[65] = { 0 };
static char         s_ip[16] = "0.0.0.0";
static bool         s_ap_on = false;
static char         s_ap_ip[16] = "192.168.4.1";
static int8_t       s_rssi = 0;          /* 由 tick 更新，界面只读缓存 */
static uint32_t     s_try_at = 0;        /* 下次尝试重连的时间戳（ms） */
static uint32_t     s_connecting_since = 0;

static Preferences s_prefs;

static void load_saved(void)
{
    s_prefs.begin("smart-home", true);
    String ssid = s_prefs.getString("ssid", "");
    String pass = s_prefs.getString("pass", "");
    s_prefs.end();

    strncpy(s_ssid, ssid.c_str(), sizeof(s_ssid) - 1);
    strncpy(s_pass, pass.c_str(), sizeof(s_pass) - 1);
}

static void save_creds(const char * ssid, const char * pass)
{
    s_prefs.begin("smart-home", false);
    s_prefs.putString("ssid", ssid);
    s_prefs.putString("pass", pass);
    s_prefs.end();
}

static void start_connect(void)
{
    if (s_ssid[0] == '\0') {
        return;
    }

#if APP_AP_ENABLE
    WiFi.mode(WIFI_AP_STA);          /* 保持热点同时连路由器 */
#else
    WiFi.mode(WIFI_STA);
#endif
    WiFi.setHostname(APP_WIFI_HOSTNAME);
    WiFi.setSleep(false);            /* 界面刷新要稳，不省这点电 */
    WiFi.begin(s_ssid, s_pass);

    s_state = WIFI_ST_CONNECTING;
    s_connecting_since = millis();
    APP_LOG("WiFi 连接中: %s", s_ssid);
}

void wifi_manager_init(void)
{
#if APP_AP_ENABLE
    /* 先起热点：手机随时能连上控制面板（AP+STA 并存，STA 还能上网取天气） */
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(APP_AP_SSID,
                (strlen(APP_AP_PASSWORD) >= 8) ? APP_AP_PASSWORD : NULL,
                APP_AP_CHANNEL, APP_AP_HIDDEN, APP_AP_MAX_CONN);
    strncpy(s_ap_ip, WiFi.softAPIP().toString().c_str(), sizeof(s_ap_ip) - 1);
    s_ap_on = true;
    APP_LOG("热点已开启: SSID=%s  密码=%s  手机浏览器打开 http://%s",
            APP_AP_SSID,
            (strlen(APP_AP_PASSWORD) >= 8) ? APP_AP_PASSWORD : "(开放)",
            s_ap_ip);
#endif
    load_saved();

    if (s_ssid[0] == '\0' && strlen(APP_WIFI_SSID) > 0) {
        strncpy(s_ssid, APP_WIFI_SSID, sizeof(s_ssid) - 1);
        strncpy(s_pass, APP_WIFI_PASSWORD, sizeof(s_pass) - 1);
        save_creds(s_ssid, s_pass);
    }

    if (s_ssid[0] != '\0') {
        start_connect();
    } else {
        APP_LOG("没有保存的 WiFi，等待在设置页配网");
    }
}

void wifi_manager_tick(void)
{
    wl_status_t st = WiFi.status();

    if (st == WL_CONNECTED) {
        if (s_state != WIFI_ST_CONNECTED) {
            s_state = WIFI_ST_CONNECTED;
            strncpy(s_ip, WiFi.localIP().toString().c_str(), sizeof(s_ip) - 1);
            s_rssi = (int8_t)WiFi.RSSI();
            APP_LOG("WiFi 已连接: %s  IP=%s  RSSI=%d", s_ssid, s_ip, (int)s_rssi);
        } else {
            /* 每秒更新一次缓存的信号强度。
             * 关键：界面层不要去调 WiFi.RSSI()（那是 WiFi 驱动的 API，会抢 WiFi 锁），
             * 否则后台正在发 HTTP 请求时，UI 线程可能被一起卡住。 */
            s_rssi = (int8_t)WiFi.RSSI();
        }
        return;
    }

    if (s_state == WIFI_ST_CONNECTED) {
        s_state = WIFI_ST_DISCONNECTED;
        s_rssi = 0;
        strncpy(s_ip, "0.0.0.0", sizeof(s_ip) - 1);
        APP_LOG("WiFi 断开");
    }

    if (s_state == WIFI_ST_CONNECTING) {
        if (millis() - s_connecting_since > APP_WIFI_CONNECT_TIMEOUT_MS) {
            APP_LOG("WiFi 连接超时");
            WiFi.disconnect(false);
            s_state = WIFI_ST_DISCONNECTED;
        }
        return;
    }

    /* 未连接：每 10 秒重试一次 */
    if (s_ssid[0] != '\0' && (s_try_at == 0 || millis() - s_try_at > 10000)) {
        s_try_at = millis();
        start_connect();
    }
}

wifi_state_t wifi_manager_state(void) { return s_state; }
/* 返回 wifi_manager_tick() 缓存的信号强度：不调用 WiFi API，界面线程可安全使用 */
int8_t wifi_manager_rssi(void)        { return (s_state == WIFI_ST_CONNECTED) ? s_rssi : 0; }
const char * wifi_manager_ssid(void)  { return s_ssid; }

bool         wifi_manager_ap_enabled(void) { return s_ap_on; }
const char * wifi_manager_ap_ssid(void)    { return s_ap_on ? APP_AP_SSID : ""; }
const char * wifi_manager_ap_pass(void)    { return APP_AP_PASSWORD; }
const char * wifi_manager_ap_ip(void)      { return s_ap_ip; }
int          wifi_manager_ap_clients(void) { return s_ap_on ? (int)WiFi.softAPgetStationNum() : 0; }
const char * wifi_manager_ip(void)    { return s_ip; }
bool wifi_manager_has_saved(void)     { return s_ssid[0] != '\0'; }

bool wifi_manager_connect(const char * ssid, const char * password, bool save)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return false;
    }

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_pass, password != NULL ? password : "", sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = '\0';

    if (save) {
        save_creds(s_ssid, s_pass);
    }

    WiFi.disconnect(false);
    delay(120);
    start_connect();
    return true;
}

void wifi_manager_forget(void)
{
    s_prefs.begin("smart-home", false);
    s_prefs.clear();
    s_prefs.end();

    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    WiFi.disconnect(false);
    s_state = WIFI_ST_DISCONNECTED;
    strncpy(s_ip, "0.0.0.0", sizeof(s_ip) - 1);
}

int wifi_manager_scan(char out[][33], int8_t * rssi_out, int max)
{
    WiFi.mode(WIFI_STA);

    int n = WiFi.scanNetworks(false, false);
    if (n <= 0) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) {
            continue;
        }
        strncpy(out[count], ssid.c_str(), 32);
        out[count][32] = '\0';
        if (rssi_out != NULL) {
            rssi_out[count] = (int8_t)WiFi.RSSI(i);
        }
        count++;
    }
    WiFi.scanDelete();
    return count;
}
