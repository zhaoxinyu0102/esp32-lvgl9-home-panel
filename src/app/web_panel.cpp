/**
 * @file web_panel.cpp
 * @brief 网页控制面板实现：HTTP + SSE + mDNS，纯 Arduino 核心自带库，离线可编译。
 *
 * 路由：
 *   GET  /              内嵌控制台页面（src/web/index.html -> src/app/index_html.h）
 *   GET  /manifest.json PWA 清单（可"添加到主屏幕"）
 *   GET  /icon.svg      图标
 *   GET  /api/state     设备状态 JSON（快照）
 *   POST /api           控制命令 JSON
 *   GET  /events        SSE：状态变化主动推给手机
 *
 * 没有 WebSocket 库（核心没带），改用 SSE（服务器推送）+ POST（手机下发），
 * 手机浏览器原生支持 EventSource，实时性一样。
 *
 * 与屏幕 UI 共用同一个 device_model：任何一侧改动都会立刻反映到另一侧。
 */

#include "web_panel.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include <vector>

#include "app_config.h"
#include "app_display.h"
#include "app_log.h"
#include "device_model.h"
#include "index_html.h"
#include "lvgl_v9_port.h"
#include "time_service.h"
#include "ui.h"
#include "weather_service.h"
#include "wifi_manager.h"

/* ---------------------------------------------------------------- 常量 */
static const char MANIFEST_JSON[] =
    "{\"name\":\"智慧家居\",\"short_name\":\"智慧家居\",\"start_url\":\"/\","
    "\"display\":\"standalone\",\"background_color\":\"#080B16\",\"theme_color\":\"#0A0E1A\","
    "\"icons\":[{\"src\":\"/icon.svg\",\"sizes\":\"192x192\",\"type\":\"image/svg+xml\",\"purpose\":\"any\"}]}";

static const char ICON_SVG[] =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 192 192'>"
    "<defs><linearGradient id='g' x1='0' y='0' x2='1' y2='1'>"
    "<stop offset='0' stop-color='#4F8CFF'/><stop offset='1' stop-color='#8B5CF6'/>"
    "</linearGradient></defs>"
    "<rect width='192' height='192' rx='42' fill='url(#g)'/>"
    "<path d='M96 40 L152 88 H136 V148 H56 V88 H40 Z' fill='#fff' opacity='.95'/>"
    "</svg>";

/* ---------------------------------------------------------------- 状态 */
static WebServer server(80);
/* SSE 单独用一个裸 socket 服务（81 端口）：
 * esp32 的 WebServer 库不支持长连接 -- 它回完响应就 Connection: close，
 * 而且声明 Transfer-Encoding: chunked，原始写入会被浏览器当成分块错误丢掉。
 * 所以推送必须绕开它，自己写 HTTP 头 + 长连接。 */
static WiFiServer sse_server(81);
static std::vector<WiFiClient> sse_clients;
static uint32_t s_last_push_ms = 0;
static uint32_t s_last_state_hash = 0;
static uint32_t s_last_ping_ms = 0;

/* WiFi 扫描结果（异步：命令只置标志，由 app 任务扫描后放进状态里） */
static volatile bool s_scan_wanted = false;
static char          s_scan_ssid[12][33];
static int8_t        s_scan_rssi[12];
static int           s_scan_count = 0;
static uint32_t      s_scan_ms = 0;

/* ---------------------------------------------------------------- 状态 JSON */
static String build_state(void)
{
    DynamicJsonDocument d(7168);

    /* 网络 + 热点 + 扫描结果 */
    JsonObject w = d.createNestedObject("wifi");
    w["state"] = (int)wifi_manager_state();
    w["ssid"] = wifi_manager_ssid();
    w["ip"] = wifi_manager_ip();
    w["rssi"] = (int)wifi_manager_rssi();
    w["saved"] = wifi_manager_has_saved() ? 1 : 0;

    JsonObject ap = d.createNestedObject("ap");
    ap["on"] = wifi_manager_ap_enabled() ? 1 : 0;
    ap["ssid"] = wifi_manager_ap_ssid();
    ap["pass"] = wifi_manager_ap_pass();
    ap["ip"] = wifi_manager_ap_ip();
    ap["clients"] = wifi_manager_ap_clients();

    if (s_scan_count > 0) {
        JsonArray scan = w.createNestedArray("scan");
        for (int i = 0; i < s_scan_count; i++) {
            JsonObject o = scan.createNestedObject();
            o["ssid"] = s_scan_ssid[i];
            o["rssi"] = (int)s_scan_rssi[i];
        }
    }

    /* 时间 */
    char tm[16] = { 0 }, dt[32] = { 0 }, wk[8] = { 0 };
    time_service_format(tm, sizeof(tm), dt, sizeof(dt), wk, sizeof(wk));
    d["time"] = tm;
    d["date"] = dt;

    /* 天气 */
    const weather_t * wx = weather_service_get();
    JsonObject wj = d.createNestedObject("weather");
    wj["kind"] = (int)wx->kind;
    wj["temp"] = wx->temp;
    wj["feels"] = wx->feels;                     /* 体感温度 */
    wj["hum"] = wx->humidity;                    /* 相对湿度 */
    wj["wind"] = (int)(wx->wind_kmh + 0.5f);     /* 风速 km/h */
    wj["high"] = wx->high;
    wj["low"] = wx->low;
    wj["text"] = wx->text;
    wj["city"] = wx->city;
    wj["demo"] = wx->demo ? 1 : 0;
    wj["updated"] = wx->updated_min_ago;
    JsonArray fc = wj.createNestedArray("fc");   /* 未来几天预报 */
    for (int i = 0; i < WEATHER_FC_DAYS; i++) {
        JsonObject o = fc.createNestedObject();
        o["day"] = wx->fc[i].day;
        o["text"] = wx->fc[i].text;
        o["kind"] = (int)wx->fc[i].kind;
        o["high"] = wx->fc[i].high;
        o["low"] = wx->fc[i].low;
    }

    /* 灯光 */
    JsonArray lights = d.createNestedArray("lights");
    for (int i = 0; i < DM_LIGHT_COUNT; i++) {
        dm_light_t * l = &dm_lights()[i];
        JsonObject o = lights.createNestedObject();
        o["name"] = l->name;
        o["on"] = l->on ? 1 : 0;
        o["br"] = l->brightness;
        o["mode"] = l->mode;
        o["rgb"] = (uint32_t)l->rgb;
    }

    /* 空调 */
    dm_ac_t * a = dm_ac();
    JsonObject ac = d.createNestedObject("ac");
    ac["on"] = a->on ? 1 : 0;
    ac["target"] = a->target;
    ac["room"] = (int)(a->room * 10.0f + 0.5f);   /* 0.1°C，前端 /10 */
    ac["mode"] = (int)a->mode;
    ac["fan"] = (int)a->fan;
    ac["swing"] = a->swing ? 1 : 0;

    /* 窗帘 */
    JsonArray cur = d.createNestedArray("cur");
    for (int i = 0; i < DM_CURTAIN_COUNT; i++) {
        dm_curtain_t * c = &dm_curtains()[i];
        JsonObject o = cur.createNestedObject();
        o["name"] = c->name;
        o["pos"] = c->position;
        o["dir"] = (int)c->dir;
    }

    /* 开关设备（屏幕无入口，但网页可以控制） */
    JsonArray sw = d.createNestedArray("sw");
    for (int i = 0; i < DM_SWITCH_COUNT; i++) {
        dm_switch_t * s = &dm_switches()[i];
        JsonObject o = sw.createNestedObject();
        o["name"] = s->name;
        o["on"] = s->on ? 1 : 0;
    }

    /* 咖啡机 */
    dm_coffee_t * cf = dm_coffee();
    JsonObject cj = d.createNestedObject("coffee");
    cj["power"] = cf->power ? 1 : 0;
    cj["type"] = (int)cf->type;
    cj["name"] = dm_coffee_type_name((int)cf->type);
    cj["strength"] = (int)cf->strength;
    cj["coffee_ml"] = (int)cf->coffee_ml;
    cj["milk_ml"] = (int)cf->milk_ml;
    cj["temp"] = (int)cf->temp;
    cj["cups"] = (int)cf->cups;
    cj["brewing"] = cf->brewing ? 1 : 0;
    cj["stage"] = (int)cf->stage;
    cj["stage_text"] = dm_coffee_stage_name((int)cf->stage);
    cj["progress"] = (int)cf->progress;
    cj["water"] = (int)cf->water_pct;
    cj["beans"] = (int)cf->beans_pct;
    cj["cups_today"] = (int)cf->cups_today;
    JsonArray ct = d.createNestedArray("coffee_types");
    for (int i = 0; i < DM_COFFEE_TYPES; i++) {
        ct.add(dm_coffee_type_name(i));
    }

    /* 能耗 */
    dm_energy_t * e = dm_energy();
    JsonObject ej = d.createNestedObject("energy");
    ej["today"] = e->today_kwh;
    ej["power"] = e->power_w;
    ej["tariff"] = e->tariff;
    JsonArray days = ej.createNestedArray("days");
    for (int i = 0; i < DM_ENERGY_DAYS; i++) {
        days.add(e->days[i]);
    }

    /* 场景 */
    JsonArray scenes = d.createNestedArray("scenes");
    for (int i = 0; i < DM_SCENE_COUNT; i++) {
        scenes.add(dm_scene_name(i));
    }
    d["scene"] = dm_last_scene();
    d["screen"] = (int)ui_nav_current();      /* 当前屏幕号：网页据此跟随切页 */
    d["backlight"] = app_get_backlight();
    d["v"] = APP_VERSION;

    /* 系统信息 */
    JsonObject sys = d.createNestedObject("sys");
    sys["heap"] = (uint32_t)ESP.getFreeHeap();
    sys["psram"] = (uint32_t)ESP.getFreePsram();
    sys["uptime"] = (uint32_t)(millis() / 1000);
    sys["ver"] = APP_VERSION;

    String out;
    serializeJson(d, out);
    return out;
}

/* ---------------------------------------------------------------- 控制命令 */
/** 真正切屏的地方：必须由 lv_async_call 在 LVGL 任务里执行
 *  （网页请求跑在 app 任务，直接调 ui_nav_goto 不安全） */
static void do_ui_switch(void * arg)
{
    ui_nav_goto((ui_screen_id_t)(intptr_t)arg);
}

static void handle_api(void)
{
    DynamicJsonDocument d(1024);
    DeserializationError err = deserializeJson(d, server.arg("plain"));
    if (err) {
        server.send(400, "application/json", "{\"ok\":0,\"err\":\"bad json\"}");
        return;
    }

    const char * c = d["c"] | "";

    if (strcmp(c, "light_all") == 0) {
        dm_light_all(d["on"] | 0);

    } else if (strcmp(c, "light") == 0) {
        int i = d["id"] | -1;
        if (i >= 0 && i < DM_LIGHT_COUNT) {
            dm_light_t * l = &dm_lights()[i];
            bool on;
            if (d.containsKey("on")) {
                on = (d["on"] | 0) != 0;
            } else if (d.containsKey("br") || d.containsKey("rgb")) {
                on = true;                    /* 拖亮度/选颜色就点亮，和屏幕行为一致 */
            } else {
                on = l->on;
            }
            int br = d["br"] | l->brightness;
            uint32_t rgb = d["rgb"] | l->rgb;
            dm_light_set(i, on, br, l->mode, l->ct, rgb);
        }

    } else if (strcmp(c, "ac") == 0) {
        if (d.containsKey("on"))     dm_ac_set_on(d["on"] | 0);
        if (d.containsKey("target")) dm_ac_set_target(d["target"] | 25);
        if (d.containsKey("mode"))   dm_ac_set_mode((dm_ac_mode_t)(d["mode"] | 0));
        if (d.containsKey("fan"))    dm_ac_set_fan((dm_fan_t)(d["fan"] | 0));
        if (d.containsKey("swing"))  dm_ac_set_swing(d["swing"] | 0);

    } else if (strcmp(c, "cur") == 0) {
        int i = d["id"] | -1;
        if (i >= 0 && i < DM_CURTAIN_COUNT) {
            int a = d["a"] | 1;
            if (a == 0)       dm_curtain_open(i);
            else if (a == 2)  dm_curtain_close(i);
            else if (a == 3)  dm_curtain_set(i, d["pos"] | dm_curtains()[i].position);
            else              dm_curtain_stop(i);
        }

    } else if (strcmp(c, "sw") == 0) {
        int i = d["id"] | -1;
        if (i >= 0 && i < DM_SWITCH_COUNT) {
            dm_switch_set(i, d["on"] | 0);
        }

    } else if (strcmp(c, "coffee") == 0) {
        if (d.containsKey("power"))     dm_coffee_set_power(d["power"] | 0);
        if (d.containsKey("type"))      dm_coffee_set_type(d["type"] | 0);
        if (d.containsKey("strength"))  dm_coffee_set_strength(d["strength"] | 3);
        if (d.containsKey("coffee_ml")) dm_coffee_set_coffee_ml(d["coffee_ml"] | 60);
        if (d.containsKey("milk_ml"))   dm_coffee_set_milk_ml(d["milk_ml"] | 0);
        if (d.containsKey("temp"))      dm_coffee_set_temp(d["temp"] | 92);
        if (d.containsKey("cups"))      dm_coffee_set_cups(d["cups"] | 1);
        if (d.containsKey("start"))     dm_coffee_start();
        if (d.containsKey("stop"))      dm_coffee_stop();

    } else if (strcmp(c, "scene") == 0) {
        dm_scene_apply(d["id"] | 0);

    } else if (strcmp(c, "ui") == 0) {
        int id = d["id"] | -1;
        if (id >= 0 && id < (int)UI_SCREEN_COUNT) {
            lv_async_call(do_ui_switch, (void *)(intptr_t)id);   /* 手机点页签 → 屏幕跟着切 */
        }

    } else if (strcmp(c, "backlight") == 0) {
        app_set_backlight((uint8_t)(d["pct"] | APP_DEFAULT_BRIGHTNESS));

    } else if (strcmp(c, "city") == 0) {
        weather_service_set_city(d["name"] | "");

    } else if (strcmp(c, "wrefresh") == 0) {
        weather_service_request_refresh();

    } else if (strcmp(c, "scan") == 0) {
        s_scan_wanted = true;                 /* 交给 app 任务扫，避免卡住 HTTP */
        ui_toast_async("正在扫描附近 WiFi...");

    } else if (strcmp(c, "wifi") == 0) {
        const char * ssid = d["ssid"] | "";
        const char * pass = d["pass"] | "";
        if (strlen(ssid) > 0) {
            wifi_manager_connect(ssid, pass, true);
            ui_toast_async("正在连接新的 WiFi...");
        }

    } else if (strcmp(c, "wifi_forget") == 0) {
        wifi_manager_forget();
        ui_toast_async("已清除保存的 WiFi");
    }

    /* 手机点完立刻同步到屏幕：弹提示 + 刷新界面（不用等下一个刷新周期） */
    if (lvgl_port_lock(200)) {
        ui_toast_flush();
        ui_refresh();
        lvgl_port_unlock();
    }

    server.send(200, "application/json", "{\"ok\":1}");
    web_panel_push_state();   /* 立即把新状态推给所有客户端 */
}

/* ---------------------------------------------------------------- 路由 */
/** 页面/接口一律禁止浏览器缓存：否则手机一直用旧版网页，
 *  改了前端也"没反应"（页签不发切屏命令就是这么来的） */
static void no_cache(void)
{
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
}

static void handle_root(void)
{
    no_cache();
    server.send(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handle_manifest(void)
{
    no_cache();
    server.send(200, "application/manifest+json; charset=utf-8", MANIFEST_JSON);
}

static void handle_icon(void)
{
    no_cache();
    server.send(200, "image/svg+xml", ICON_SVG);
}

static void handle_state(void)
{
    no_cache();
    server.send(200, "application/json; charset=utf-8", build_state());
}

/* 80 端口上的 /events 只作为提示：真正的事件流在 81 端口（见 web_panel_sse_accept） */
static void handle_events_redirect(void)
{
    server.send(200, "text/plain; charset=utf-8",
                "SSE 在 81 端口: http://<ip>:81/events");
}

static void handle_notfound(void)
{
    server.send(404, "text/plain; charset=utf-8", "not found");
}

/* ---------------------------------------------------------------- 扫描（app 任务里跑） */
static void do_scan(void)
{
    int n = wifi_manager_scan(s_scan_ssid, s_scan_rssi, 12);
    if (n < 0) {
        n = 0;
    }
    /* 按信号从强到弱排序 */
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            if (s_scan_rssi[j] < s_scan_rssi[j + 1]) {
                int8_t tr = s_scan_rssi[j];
                s_scan_rssi[j] = s_scan_rssi[j + 1];
                s_scan_rssi[j + 1] = tr;
                char ts[33];
                memcpy(ts, s_scan_ssid[j], 33);
                memcpy(s_scan_ssid[j], s_scan_ssid[j + 1], 33);
                memcpy(s_scan_ssid[j + 1], ts, 33);
            }
        }
    }
    s_scan_count = n;
    s_scan_ms = millis();
    APP_LOG("WiFi 扫描完成: %d 个", n);
    ui_toast_async(n > 0 ? "扫描完成，手机上选一个" : "没扫到 WiFi");
    web_panel_push_state();
}

/* ---------------------------------------------------------------- 对外接口 */
void web_panel_init(void)
{
    server.on("/", handle_root);
    server.on("/manifest.json", handle_manifest);
    server.on("/icon.svg", handle_icon);
    server.on("/api/state", handle_state);
    server.on("/api", HTTP_POST, handle_api);
    server.on("/events", handle_events_redirect);
    server.onNotFound(handle_notfound);
    server.begin();

    /* 推送服务：81 端口，自己管的裸 socket 长连接 */
    sse_server.begin();
    sse_server.setNoDelay(true);

    if (MDNS.begin("smart-home")) {
        MDNS.addService("http", "tcp", 80);
    }
    APP_LOG("网页面板已启动: 热点 http://%s  局域网 http://%s",
            wifi_manager_ap_ip(), wifi_manager_ip());
}

/* ---------------------------------------------------------------- 推送（81 端口长连接） */
static void push_state(bool force);

/** 接受新的 SSE 连接：自己读掉请求、自己回 HTTP 头，然后**保持连接不关** */
static void sse_accept(void)
{
    WiFiClient c = sse_server.accept();
    if (!c) {
        return;
    }
    c.setNoDelay(true);

    String req;
    uint32_t t0 = millis();
    while (c.connected() && (millis() - t0) < 400) {
        while (c.available() > 0) {
            req += (char)c.read();
        }
        if (req.indexOf("\r\n\r\n") >= 0) {
            break;
        }
        delay(1);
    }
    if (req.indexOf("/events") < 0) {
        c.stop();
        return;
    }

    c.print("HTTP/1.1 200 OK\r\n");
    c.print("Content-Type: text/event-stream; charset=utf-8\r\n");
    c.print("Cache-Control: no-cache, no-store\r\n");
    c.print("Connection: keep-alive\r\n");
    c.print("Access-Control-Allow-Origin: *\r\n");
    c.print("\r\n");
    c.print(": connected\n\n");        /* 立刻给一帧，让浏览器确认通道可用 */
    sse_clients.push_back(c);
    APP_LOG("网页推送已连接（当前 %d 个）", (int)sse_clients.size());
    push_state(true);                  /* 连上先给一份全量状态 */
}

void web_panel_loop(void)
{
    server.handleClient();
    sse_accept();

    /* 清理断开的长连接，并每 20 秒发一次心跳（防止中间设备掐掉空闲连接） */
    for (size_t i = 0; i < sse_clients.size();) {
        if (!sse_clients[i].connected()) {
            sse_clients.erase(sse_clients.begin() + i);
        } else {
            i++;
        }
    }
    if (!sse_clients.empty() && (millis() - s_last_ping_ms) > 20000) {
        s_last_ping_ms = millis();
        for (size_t i = 0; i < sse_clients.size(); i++) {
            sse_clients[i].print(": ping\n\n");
        }
    }

    /* 请求过的扫描在这里做（阻塞几百毫秒~2 秒，但不会卡住 LVGL 任务） */
    if (s_scan_wanted) {
        s_scan_wanted = false;
        do_scan();
    }

    /* 扫描结果 60 秒过期 */
    if (s_scan_count > 0 && (millis() - s_scan_ms) > 60000) {
        s_scan_count = 0;
    }
}

/** force = true 时忽略去重（新客户端连上要立刻拿到全量状态） */
static void push_state(bool force)
{
    if (sse_clients.empty()) {
        return;
    }

    String json = build_state();
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < json.length(); i++) {
        h = (h ^ (unsigned char)json[i]) * 16777619u;
    }
    uint32_t now = millis();
    if (!force && h == s_last_state_hash && (now - s_last_push_ms) < 500) {
        return;                        /* 状态没变且刚推过，跳过 */
    }
    s_last_state_hash = h;
    s_last_push_ms = now;

    String msg = "data: " + json + "\n\n";
    for (size_t i = 0; i < sse_clients.size();) {
        if (sse_clients[i].connected()) {
            sse_clients[i].write((const uint8_t *)msg.c_str(), msg.length());
            i++;
        } else {
            sse_clients.erase(sse_clients.begin() + i);
        }
    }
}

void web_panel_push_state(void)
{
    push_state(false);
}
