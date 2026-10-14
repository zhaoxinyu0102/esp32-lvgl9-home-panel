/**
 * @file weather_service.cpp
 * @brief 天气服务：Seniverse / OpenWeatherMap HTTP 拉取 + JSON 解析；没有 key 走演示数据。
 *
 * 依赖：ArduinoJson（工程 lib/ArduinoJson，离线可用）
 */

#include "weather_service.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <math.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "app_events.h"
#include "app_log.h"
#include "images/ui_images.h"

static weather_t s_w;
static char      s_key[64] = { 0 };
static char      s_city[24] = "beijing";
static uint32_t  s_last_fetch_ms = 0;
static bool      s_force = false;
static uint32_t  s_demo_seed = 0;

/* -------------------------------------------------------------------------
 * 工具
 * ---------------------------------------------------------------------- */
const char * weather_kind_text(weather_kind_t kind)
{
    switch (kind) {
    case WX_CLEAR:   return "晴";
    case WX_PARTLY:  return "多云";
    case WX_CLOUDY:  return "阴";
    case WX_RAIN:    return "雨";
    case WX_SNOW:    return "雪";
    case WX_THUNDER: return "雷阵雨";
    case WX_FOG:     return "雾";
    default:         return "未知";
    }
}

const void * weather_icon(weather_kind_t kind)
{
    switch (kind) {
    case WX_CLEAR:   return &ui_img_w_sunny;
    case WX_PARTLY:  return &ui_img_w_partly;
    case WX_CLOUDY:  return &ui_img_w_cloudy;
    case WX_RAIN:    return &ui_img_w_rain;
    case WX_SNOW:    return &ui_img_w_snow;
    case WX_THUNDER: return &ui_img_w_thunder;
    case WX_FOG:     return &ui_img_w_fog;
    default:         return &ui_img_w_cloudy;
    }
}

/** 按中文描述文本猜类型（两个 API 都能用） */
static weather_kind_t kind_from_text(const char * text)
{
    if (text == NULL) {
        return WX_UNKNOWN;
    }
    if (strstr(text, "雷")) return WX_THUNDER;
    if (strstr(text, "雪")) return WX_SNOW;
    if (strstr(text, "雨")) return WX_RAIN;
    if (strstr(text, "雾") || strstr(text, "沙") || strstr(text, "尘")) return WX_FOG;
    if (strstr(text, "阴")) return WX_CLOUDY;
    if (strstr(text, "多云") || strstr(text, "晴间")) return WX_PARTLY;
    if (strstr(text, "晴")) return WX_CLEAR;
    return WX_UNKNOWN;
}

/** OpenWeatherMap 的 weather[].id 映射 */
static weather_kind_t kind_from_owm_id(int id)
{
    if (id >= 200 && id < 300) return WX_THUNDER;
    if (id >= 300 && id < 600) return WX_RAIN;
    if (id >= 600 && id < 700) return WX_SNOW;
    if (id >= 700 && id < 800) return WX_FOG;
    if (id == 800) return WX_CLEAR;
    if (id == 801) return WX_PARTLY;
    if (id >= 802) return WX_CLOUDY;
    return WX_UNKNOWN;
}

static void set_text_and_kind(weather_t * w, const char * text, weather_kind_t kind)
{
    if (text != NULL && text[0] != '\0') {
        strncpy(w->text, text, sizeof(w->text) - 1);
    } else {
        strncpy(w->text, weather_kind_text(kind), sizeof(w->text) - 1);
    }
    w->kind = kind;
}

/* -------------------------------------------------------------------------
 * 演示数据：没有 key 或请求失败时用，跟着时间缓慢变化，界面不会空着
 * ---------------------------------------------------------------------- */
static void fill_demo(void)
{
    struct tm t = { 0 };
    time_t now = 0;
    time(&now);
    localtime_r(&now, &t);

    s_demo_seed++;
    int hour = t.tm_hour;

    /* 用一个简单的日变化曲线造温度，再加点缓慢漂移 */
    float base = 22.0f + 6.0f * sinf((float)(hour - 6) / 24.0f * 2.0f * (float)M_PI);
    float drift = 1.5f * sinf((float)(s_demo_seed) * 0.05f);
    int temp = (int)lroundf(base + drift);

    static const weather_kind_t pattern[6] = { WX_CLEAR, WX_PARTLY, WX_CLOUDY, WX_PARTLY, WX_CLEAR, WX_CLOUDY };
    weather_kind_t kind = pattern[(s_demo_seed / 12) % 6];

    s_w.valid = true;
    s_w.demo = true;
    strncpy(s_w.city, "演示数据", sizeof(s_w.city) - 1);
    s_w.temp = temp;
    s_w.feels = temp + 1;
    s_w.humidity = 45 + (int)(s_demo_seed % 20);
    s_w.wind_kmh = 6.0f + (float)(s_demo_seed % 8);
    s_w.high = temp + 4;
    s_w.low = temp - 7;
    set_text_and_kind(&s_w, NULL, kind);
    s_w.updated_min_ago = 0;

    static const char * days[WEATHER_FC_DAYS] = { "明天", "后天", "大后天" };
    for (int i = 0; i < WEATHER_FC_DAYS; i++) {
        weather_kind_t k = pattern[(s_demo_seed / 12 + i + 1) % 6];
        snprintf(s_w.fc[i].day, sizeof(s_w.fc[i].day), "%s", days[i]);
        snprintf(s_w.fc[i].text, sizeof(s_w.fc[i].text), "%s", weather_kind_text(k));
        s_w.fc[i].kind = k;
        s_w.fc[i].high = temp + 3 - i;
        s_w.fc[i].low = temp - 6 - i;
    }
}

/* -------------------------------------------------------------------------
 * Seniverse（心知天气）v3
 * ---------------------------------------------------------------------- */
static bool fetch_seniverse(void)
{
    WiFiClientSecure client;
    client.setInsecure();                    /* 不做证书校验，够用且省 flash */
    HTTPClient http;

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.seniverse.com/v3/weather/now.json?key=%s&location=%s&language=zh-Hans&unit=c",
             s_key, s_city);

    http.setConnectTimeout(6000);
    http.setTimeout(8000);
    if (!http.begin(client, url)) {
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        APP_LOG("天气 now 请求失败: %d", code);
        http.end();
        return false;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err) {
        APP_LOG("天气 now JSON 解析失败: %s", err.c_str());
        return false;
    }

    JsonObject now = doc["results"][0]["now"].as<JsonObject>();
    JsonObject loc = doc["results"][0]["location"].as<JsonObject>();
    if (now.isNull()) {
        return false;
    }

    s_w.temp = atoi(now["temperature"] | "0");
    strncpy(s_w.city, loc["name"] | s_city, sizeof(s_w.city) - 1);
    const char * text = now["text"] | "";
    set_text_and_kind(&s_w, text, kind_from_text(text));

    /* 逐日预报（拿最高/最低） */
    char url2[256];
    snprintf(url2, sizeof(url2),
             "https://api.seniverse.com/v3/weather/daily.json?key=%s&location=%s&language=zh-Hans&unit=c&start=0&days=%d",
             s_key, s_city, WEATHER_FC_DAYS + 1);

    WiFiClientSecure client2;
    client2.setInsecure();
    HTTPClient http2;
    if (http2.begin(client2, url2)) {
        if (http2.GET() == HTTP_CODE_OK) {
            DynamicJsonDocument doc2(6144);
            if (!deserializeJson(doc2, http2.getString())) {
                JsonArray daily = doc2["results"][0]["daily"].as<JsonArray>();
                /* daily[0] 是今天 */
                if (daily.size() > 0) {
                    s_w.high = atoi(daily[0]["high"] | "0");
                    s_w.low = atoi(daily[0]["low"] | "0");
                }
                for (int i = 0; i < WEATHER_FC_DAYS && (i + 1) < (int)daily.size(); i++) {
                    JsonObject d = daily[i + 1].as<JsonObject>();
                    const char * t = d["text_day"] | "";
                    weather_kind_t k = kind_from_text(t);
                    snprintf(s_w.fc[i].day, sizeof(s_w.fc[i].day), "%s", (i == 0) ? "明天" : (i == 1 ? "后天" : "大后天"));
                    snprintf(s_w.fc[i].text, sizeof(s_w.fc[i].text), "%s", weather_kind_text(k));
                    s_w.fc[i].kind = k;
                    s_w.fc[i].high = atoi(d["high"] | "0");
                    s_w.fc[i].low = atoi(d["low"] | "0");
                }
            }
        }
        http2.end();
    }

    s_w.feels = s_w.temp;             /* Seniverse now 没有体感，用气温代替 */
    s_w.humidity = 0;
    s_w.wind_kmh = 0;
    s_w.valid = true;
    s_w.demo = false;
    s_w.updated_min_ago = 0;
    APP_LOG("天气已更新(Seniverse): %s %d°C %s", s_w.city, s_w.temp, s_w.text);
    return true;
}

/* -------------------------------------------------------------------------
 * OpenWeatherMap
 * ---------------------------------------------------------------------- */
static bool fetch_owm(void)
{
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    char url[320];
    if (strlen(APP_WEATHER_LAT) > 0 && strlen(APP_WEATHER_LON) > 0) {
        snprintf(url, sizeof(url),
                 "https://api.openweathermap.org/data/2.5/weather?lat=%s&lon=%s&appid=%s&units=metric&lang=zh_cn",
                 APP_WEATHER_LAT, APP_WEATHER_LON, s_key);
    } else {
        snprintf(url, sizeof(url),
                 "https://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=metric&lang=zh_cn",
                 s_city, s_key);
    }

    http.setConnectTimeout(6000);
    http.setTimeout(8000);
    if (!http.begin(client, url)) {
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        APP_LOG("天气请求失败: %d", code);
        http.end();
        return false;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err) {
        return false;
    }

    JsonObject main = doc["main"].as<JsonObject>();
    JsonArray weather = doc["weather"].as<JsonArray>();
    if (main.isNull()) {
        return false;
    }

    s_w.temp = (int)lroundf(main["temp"] | 0.0f);
    s_w.feels = (int)lroundf(main["feels_like"] | (float)s_w.temp);
    s_w.humidity = main["humidity"] | 0;
    s_w.wind_kmh = (doc["wind"]["speed"] | 0.0f) * 3.6f;
    strncpy(s_w.city, doc["name"] | s_city, sizeof(s_w.city) - 1);

    int id = weather[0]["id"] | 800;
    const char * desc = weather[0]["description"] | "";
    set_text_and_kind(&s_w, desc, kind_from_owm_id(id));

    /* 5 天/3 小时预报：按日期聚合出每天最高/最低与代表天气 */
    char url2[320];
    if (strlen(APP_WEATHER_LAT) > 0 && strlen(APP_WEATHER_LON) > 0) {
        snprintf(url2, sizeof(url2),
                 "https://api.openweathermap.org/data/2.5/forecast?lat=%s&lon=%s&appid=%s&units=metric&lang=zh_cn",
                 APP_WEATHER_LAT, APP_WEATHER_LON, s_key);
    } else {
        snprintf(url2, sizeof(url2),
                 "https://api.openweathermap.org/data/2.5/forecast?q=%s&appid=%s&units=metric&lang=zh_cn",
                 s_city, s_key);
    }

    WiFiClientSecure client2;
    client2.setInsecure();
    HTTPClient http2;
    if (http2.begin(client2, url2) && http2.GET() == HTTP_CODE_OK) {
        DynamicJsonDocument doc2(12288);
        if (!deserializeJson(doc2, http2.getString())) {
            JsonArray list = doc2["list"].as<JsonArray>();
            char cur_day[12] = { 0 };
            int slot = -1;
            float hi = -100, lo = 100;
            weather_kind_t k = WX_UNKNOWN;
            for (JsonVariant v : list) {
                JsonObject item = v.as<JsonObject>();
                const char * dt = item["dt_txt"] | "";
                if (strlen(dt) >= 10) {
                    char day[11];
                    strncpy(day, dt, 10);
                    day[10] = '\0';
                    if (slot < 0) {
                        strncpy(cur_day, day, sizeof(cur_day) - 1);
                        slot = 0;
                    } else if (strcmp(day, cur_day) != 0) {
                        /* 上一天收尾 */
                        if (slot > 0 && slot <= WEATHER_FC_DAYS) {
                            snprintf(s_w.fc[slot - 1].day, sizeof(s_w.fc[slot - 1].day), "%s",
                                     slot == 1 ? "明天" : (slot == 2 ? "后天" : "大后天"));
                            snprintf(s_w.fc[slot - 1].text, sizeof(s_w.fc[slot - 1].text), "%s", weather_kind_text(k));
                            s_w.fc[slot - 1].kind = k;
                            s_w.fc[slot - 1].high = (int)lroundf(hi);
                            s_w.fc[slot - 1].low = (int)lroundf(lo);
                        }
                        strncpy(cur_day, day, sizeof(cur_day) - 1);
                        slot++;
                        hi = -100;
                        lo = 100;
                        k = WX_UNKNOWN;
                    }
                }
                float t = item["main"]["temp"] | 0.0f;
                if (t > hi) hi = t;
                if (t < lo) lo = t;
                if (k == WX_UNKNOWN) {
                    const char * d = item["weather"][0]["description"] | "";
                    k = kind_from_text(d);
                    if (k == WX_UNKNOWN) {
                        k = kind_from_owm_id(item["weather"][0]["id"] | 800);
                    }
                }
            }
        }
        http2.end();
    }

    s_w.valid = true;
    s_w.demo = false;
    s_w.updated_min_ago = 0;
    APP_LOG("天气已更新(OWM): %s %d°C %s", s_w.city, s_w.temp, s_w.text);
    return true;
}

/* -------------------------------------------------------------------------
 * 对外接口
 * ---------------------------------------------------------------------- */
/* -------------------------------------------------------------------------
 * Open-Meteo：完全免费、不需要 API key，也不限次数（https://open-meteo.com）
 *   第 1 步：城市名 -> 经纬度（geocoding，language=zh 返回中文城市名）
 *   第 2 步：按经纬度取实时 + 未来 3 天（weather_code 是 WMO 标准天气码）
 * 这样"连上网就有真实天气"，不用申请任何 key。
 * ---------------------------------------------------------------------- */
static weather_kind_t wmo_kind(int code)
{
    if (code == 0)                                  return WX_CLEAR;
    if (code == 1 || code == 2)                     return WX_PARTLY;
    if (code == 3)                                  return WX_CLOUDY;
    if (code == 45 || code == 48)                   return WX_FOG;
    if (code >= 51 && code <= 57)                   return WX_RAIN;      /* 毛毛雨 */
    if (code >= 61 && code <= 67)                   return WX_RAIN;
    if (code >= 71 && code <= 77)                   return WX_SNOW;
    if (code >= 80 && code <= 82)                   return WX_RAIN;      /* 阵雨 */
    if (code == 85 || code == 86)                   return WX_SNOW;
    if (code >= 95)                                 return WX_THUNDER;
    return WX_CLOUDY;
}

static void wmo_text(int code, char * out, size_t out_len)
{
    const char * t = "多云";
    if (code == 0)               t = "晴";
    else if (code == 1)          t = "晴间多云";
    else if (code == 2)          t = "多云";
    else if (code == 3)          t = "阴";
    else if (code == 45)         t = "雾";
    else if (code == 48)         t = "冻雾";  /* 字库里没有「凇」字，改用「冻雾」 */
    else if (code <= 55)         t = "毛毛雨";
    else if (code <= 57)         t = "冻毛毛雨";
    else if (code <= 65)         t = "雨";
    else if (code <= 67)         t = "冻雨";
    else if (code <= 75)         t = "雪";
    else if (code == 77)         t = "米雪";
    else if (code <= 82)         t = "阵雨";
    else if (code <= 86)         t = "阵雪";
    else                         t = "雷阵雨";
    snprintf(out, out_len, "%s", t);
}

static bool fetch_open_meteo(void)
{
    char lat[16] = APP_WEATHER_LAT;
    char lon[16] = APP_WEATHER_LON;
    char city_zh[24] = { 0 };

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(6000);

    /* ---- 1) 城市名 -> 经纬度（拿不到就用配置里的经纬度兜底）---- */
    char url[256];
    snprintf(url, sizeof(url),
             "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=zh&format=json",
             s_city);
    if (http.begin(client, url) && http.GET() == HTTP_CODE_OK) {
        DynamicJsonDocument doc(3072);
        if (!deserializeJson(doc, http.getString())) {
            JsonObject r0 = doc["results"][0].as<JsonObject>();
            if (!r0.isNull()) {
                float la = r0["latitude"] | 0.0f;
                float lo = r0["longitude"] | 0.0f;
                snprintf(lat, sizeof(lat), "%.4f", la);
                snprintf(lon, sizeof(lon), "%.4f", lo);
                const char * nm = r0["name"] | "";
                if (nm[0] != '\0') {
                    strncpy(city_zh, nm, sizeof(city_zh) - 1);
                }
            }
        }
    }
    http.end();

    /* ---- 2) 实时 + 3 天预报 ---- */
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min"
             "&timezone=%s&forecast_days=%d",
             lat, lon, "Asia%2FShanghai", WEATHER_FC_DAYS + 1);

    DynamicJsonDocument doc(6144);
    if (!http.begin(client, url)) {
        APP_LOG("天气：Open-Meteo 连接失败");
        return false;
    }
    int code_http = http.GET();
    if (code_http != HTTP_CODE_OK) {
        APP_LOG("天气：Open-Meteo HTTP %d", code_http);
        http.end();
        return false;
    }
    DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err) {
        APP_LOG("天气：Open-Meteo JSON 解析失败 %s", err.c_str());
        return false;
    }

    JsonObject cur = doc["current"].as<JsonObject>();
    if (cur.isNull()) {
        return false;
    }

    int now_code = cur["weather_code"] | 3;
    s_w.valid = true;
    s_w.demo = false;
    s_w.temp = (int)lroundf(cur["temperature_2m"] | 0.0f);
    s_w.feels = (int)lroundf(cur["apparent_temperature"] | 0.0f);
    s_w.humidity = cur["relative_humidity_2m"] | 0;
    s_w.wind_kmh = cur["wind_speed_10m"] | 0.0f;

    char text[24];
    wmo_text(now_code, text, sizeof(text));
    set_text_and_kind(&s_w, text, wmo_kind(now_code));

    snprintf(s_w.city, sizeof(s_w.city), "%s", city_zh[0] ? city_zh : s_city);

    JsonObject daily = doc["daily"].as<JsonObject>();
    if (!daily.isNull()) {
        JsonArray tmax = daily["temperature_2m_max"].as<JsonArray>();
        JsonArray tmin = daily["temperature_2m_min"].as<JsonArray>();
        JsonArray dcode = daily["weather_code"].as<JsonArray>();

        s_w.high = (int)lroundf(tmax[0] | 0.0f);
        s_w.low = (int)lroundf(tmin[0] | 0.0f);

        static const char * days[WEATHER_FC_DAYS] = { "明天", "后天", "大后天" };
        for (int i = 0; i < WEATHER_FC_DAYS; i++) {
            int idx = i + 1;
            int c = (idx < (int)dcode.size()) ? (int)(dcode[idx] | 3) : 3;
            snprintf(s_w.fc[i].day, sizeof(s_w.fc[i].day), "%s", days[i]);
            wmo_text(c, s_w.fc[i].text, sizeof(s_w.fc[i].text));
            s_w.fc[i].kind = wmo_kind(c);
            s_w.fc[i].high = (int)lroundf(tmax[idx] | 0.0f);
            s_w.fc[i].low = (int)lroundf(tmin[idx] | 0.0f);
        }
    }

    s_w.updated_min_ago = 0;
    APP_LOG("天气：Open-Meteo 成功 %s %d° %s (湿度 %d%%)",
            s_w.city, s_w.temp, s_w.text, s_w.humidity);
    return true;
}

void weather_service_init(const char * api_key, const char * city)
{
    if (api_key != NULL) {
        strncpy(s_key, api_key, sizeof(s_key) - 1);
    }
    if (city != NULL && city[0] != '\0') {
        strncpy(s_city, city, sizeof(s_city) - 1);
    }

    /* 设置页 / 串口命令改过城市的话，NVS 里的值优先（否则用 app_config.h 的默认值） */
    {
        Preferences p;
        if (p.begin("smart-home", true)) {
            String saved = p.getString("wx_city", "");
            if (saved.length() > 0) {
                strncpy(s_city, saved.c_str(), sizeof(s_city) - 1);
            }
            p.end();
        }
    }

    memset(&s_w, 0, sizeof(s_w));
    s_w.kind = WX_UNKNOWN;
    fill_demo();

    if (APP_WEATHER_PROVIDER == 0) {
        APP_LOG("天气：使用 Open-Meteo（免 API key），城市=%s，连上 WiFi 后自动获取真实天气", s_city);
    } else if (s_key[0] == '\0') {
        APP_LOG("天气：没有 API key，使用演示数据（在 app_config.h 里填 APP_WEATHER_API_KEY 即接真实数据）");
    }
}

void weather_service_request_refresh(void)
{
    s_force = true;
}

void weather_service_set_city(const char * city)
{
    if (city == NULL || city[0] == '\0') {
        return;
    }
    strncpy(s_city, city, sizeof(s_city) - 1);

    Preferences p;
    if (p.begin("smart-home", false)) {
        p.putString("wx_city", s_city);
        p.end();
    }

    APP_LOG("天气城市已改为 %s，立即重新获取", s_city);
    ev_city(s_city);
    s_force = true;
}

const char * weather_service_city(void)
{
    return s_city;
}

void weather_service_tick(void)
{
    uint32_t now = millis();
    uint32_t interval = (uint32_t)APP_WEATHER_REFRESH_MIN * 60u * 1000u;

    /* 还没拿到真实数据（开机时 WiFi 往往还没连上）-> 30 秒重试一次，别让用户干等 20 分钟 */
    if (s_w.demo) {
        interval = 30u * 1000u;
    }

    if (!s_force && s_last_fetch_ms != 0 && (now - s_last_fetch_ms) < interval) {
        /* 更新时间显示 */
        if (s_last_fetch_ms != 0) {
            s_w.updated_min_ago = (int)((now - s_last_fetch_ms) / 60000u);
        }
        return;
    }

    s_force = false;
    s_last_fetch_ms = now;

    /* 没连上 WiFi 就继续用演示数据 */
    if (WiFi.status() != WL_CONNECTED) {
        if (!s_w.valid) {
            fill_demo();
        }
        return;
    }

    if (APP_WEATHER_PROVIDER != 0 && s_key[0] == '\0') {
        fill_demo();                 /* 需要 key 的接口但没填 -> 演示数据 */
        return;
    }

    bool ok;
    if (APP_WEATHER_PROVIDER == 0) {
        ok = fetch_open_meteo();       /* 免 key，连上网就有真实天气 */
    } else if (APP_WEATHER_PROVIDER == 2) {
        ok = fetch_owm();
    } else {
        ok = fetch_seniverse();
    }
    if (!ok) {
        APP_LOG("天气拉取失败，继续用上一次的数据");
        s_w.updated_min_ago = 0;
    }
}

const weather_t * weather_service_get(void)
{
    return &s_w;
}
