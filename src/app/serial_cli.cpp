/**
 * @file serial_cli.cpp
 * @brief 串口命令行实现。
 *
 * 用法（115200，行尾回车）：
 *   help                      显示帮助
 *   wifi <SSID> <密码>         连接并保存到 NVS（密码可省略 = 开放网络）
 *   wifi?                     查看当前连接状态
 *   forget                    清除保存的 WiFi
 *   scan                      扫描附近 AP
 *   ui <0-7>                  切到指定屏幕（0首页 1灯光 2空调 3窗帘 4能耗 5安防 6天气 7设置）
 *   shot [步长]               把当前屏幕截图从串口导出（默认隔 1 点采样，见下方协议）
 *   heap                      打印内存
 *   reboot                    重启
 */

#include "serial_cli.h"

#include <Arduino.h>

#include <string.h>

#include "app_events.h"
#include "app_log.h"
#include "device_model.h"
#include "lvgl.h"
#include "lvgl_v9_port.h"
#include "time_service.h"
#include "ui.h"
#include "weather_service.h"
#include "wifi_manager.h"

#define CLI_LINE_MAX 128

static char s_line[CLI_LINE_MAX];
static int  s_len = 0;

static void print_help(void)
{
    Serial.println();
    Serial.println("======== 串口命令 ========");
    Serial.println(" help                      显示本帮助");
    Serial.println(" wifi <SSID> <密码>         连接并保存（密码可省略）");
    Serial.println(" wifi?                     查看连接状态");
    Serial.println(" forget                    清除保存的 WiFi");
    Serial.println(" scan                      扫描附近 AP");
    Serial.println(" ui <0-7>                  切屏: 0首页 1灯光 2空调 3窗帘 4能耗 5安防 6天气 7设置");
    Serial.println(" city [名称]               查看/设置天气城市（拼音，如 hefei / anhui）");
    Serial.println(" heap                      打印内存占用");
    Serial.println(" sync                      重发一次全量设备状态（给 STM32 对齐用）");
    Serial.println(" scene <0-3>               应用场景: 0回家 1离家 2睡眠 3影院");
    Serial.println(" events 0|1                关闭/打开状态事件输出（默认开）");
    Serial.println(" coffee start|stop|type <0-3>  咖啡机：冲泡/停止/选类型");
    Serial.println(" shot [步长]               导出当前屏幕截图（默认 2，=隔点采样 400x240）");
    Serial.println(" reboot                    重启");
    Serial.println("==========================");
    Serial.println();
}

static void print_heap(void)
{
    Serial.printf("[cli] heap=%u  PSRAM=%u  (最小可用 heap=%u)\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
                  (unsigned)ESP.getMinFreeHeap());
}

static void cmd_wifi(char * args)
{
    while (*args == ' ') {
        args++;
    }
    if (*args == '\0') {
        Serial.println("[cli] 用法: wifi <SSID> <密码>");
        return;
    }

    char * ssid = args;
    char * pass = strchr(ssid, ' ');
    if (pass != NULL) {
        *pass = '\0';
        pass++;
        while (*pass == ' ') {
            pass++;
        }
    } else {
        pass = (char *)"";
    }

    Serial.printf("[cli] 连接中: SSID=\"%s\" 密码长度=%d\n", ssid, (int)strlen(pass));
    wifi_manager_connect(ssid, pass, true);
}

static void cmd_scan(void)
{
    Serial.println("[cli] 扫描中（约 2~5 秒）...");
    static char names[12][33];
    static int8_t rssi[12];
    int n = wifi_manager_scan(names, rssi, 12);
    if (n == 0) {
        Serial.println("[cli] 没有扫描到网络");
        return;
    }
    for (int i = 0; i < n; i++) {
        Serial.printf("  %2d) %-32s %d dBm\n", i + 1, names[i], (int)rssi[i]);
    }
    Serial.println("[cli] 用 `wifi <名称> <密码>` 连接");
}

static const char * SCREEN_NAME[] = {
    "首页", "灯光", "空调", "窗帘", "能耗", "咖啡", "天气", "设置"
};

/** 真正执行切屏的地方：由 lv_async_call 在 LVGL 任务里调用，
 *  和手指点导航栏走的是同一条路径（而不是用后台任务的栈去建整个界面） */
static void do_screen_switch(void * arg)
{
    ui_nav_goto((ui_screen_id_t)(intptr_t)arg);
}

static void cmd_ui(char * args)
{
    while (*args == ' ') {
        args++;
    }
    int id = atoi(args);
    if (id < 0 || id >= (int)UI_SCREEN_COUNT) {
        Serial.println("[cli] 用法: ui <0-7>");
        return;
    }

    lv_async_call(do_screen_switch, (void *)(intptr_t)id);
    Serial.printf("[cli] 已请求切到 %d (%s)\n", id, SCREEN_NAME[id]);
}

/* -------------------------------------------------------------------------
 * 屏幕截图：把当前界面从串口导出来（开发时在电脑上看真实效果用）
 *
 * 协议：先一行 ASCII 头，随后是 RGB565（小端）像素，按采样后的行逐行发送
 *   @@SHOT <原宽> <原高> <采样步长> <原行字节数>\n
 *   结束再发一行 @@SHOT_END
 * 步长 2 = 隔点采样（400x240，约 15 秒传完）；步长 1 = 全分辨率（约 60 秒）。
 * 截图必须在 LVGL 任务里做（lv_async_call），发送放后台任务逐行吐，避免卡界面。
 * ---------------------------------------------------------------------- */
static lv_draw_buf_t * s_shot_buf = NULL;
static bool            s_shot_sending = false;
static uint16_t        s_shot_w = 0, s_shot_h = 0, s_shot_step = 2, s_shot_row = 0;
static uint16_t        s_shot_rowbuf[800];

static void do_snapshot(void * arg)
{
    uint16_t step = (uint16_t)(intptr_t)arg;
    if (step < 1) step = 1;
    if (step > 8) step = 8;

    if (s_shot_buf != NULL) {               /* 上一张还没发完 */
        lv_draw_buf_destroy(s_shot_buf);
        s_shot_buf = NULL;
    }
    s_shot_buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (s_shot_buf == NULL) {
        Serial.println("[shot] 截图失败（LVGL 堆不足？）");
        return;
    }
    s_shot_w = (uint16_t)s_shot_buf->header.w;
    s_shot_h = (uint16_t)s_shot_buf->header.h;
    s_shot_step = step;
    s_shot_row = 0;
    s_shot_sending = true;
    Serial.printf("@@SHOT %u %u %u %u\n",
                  (unsigned)s_shot_w, (unsigned)s_shot_h,
                  (unsigned)step, (unsigned)s_shot_buf->header.stride);
}

static void cmd_shot(char * args)
{
    while (*args == ' ') {
        args++;
    }
    int step = (*args != '\0') ? atoi(args) : 2;
    if (step < 1) step = 1;
    if (step > 8) step = 8;
    lv_async_call(do_snapshot, (void *)(intptr_t)step);   /* 必须在 LVGL 任务里截 */
    Serial.printf("[cli] 开始截图：采样 1/%d（约 %d 行，每拍 1 行）...\n", step, 480 / step);
}

/** 每拍发一行（约 130ms@115200），把 CPU 让给 LVGL，界面不会卡死 */
static void shot_tick(void)
{
    if (!s_shot_sending || s_shot_buf == NULL) {
        return;
    }
    const uint16_t * src = (const uint16_t *)s_shot_buf->data;
    uint32_t stride_px = s_shot_buf->header.stride / 2;
    uint32_t sw = ((uint32_t)s_shot_w + s_shot_step - 1) / s_shot_step;
    uint32_t sh = ((uint32_t)s_shot_h + s_shot_step - 1) / s_shot_step;

    if (s_shot_row >= sh || sw > 800) {
        s_shot_sending = false;
        lv_draw_buf_destroy(s_shot_buf);
        s_shot_buf = NULL;
        Serial.println();
        Serial.println("@@SHOT_END");
        return;
    }

    const uint16_t * row = src + (uint32_t)s_shot_row * s_shot_step * stride_px;
    for (uint32_t x = 0; x < sw; x++) {
        uint32_t sx = x * s_shot_step;
        if (sx >= s_shot_w) {
            sx = s_shot_w - 1;
        }
        s_shot_rowbuf[x] = row[sx];
    }
    Serial.write((const uint8_t *)s_shot_rowbuf, sw * 2);
    s_shot_row++;
}

static void handle(char * line)
{
    while (*line == ' ') {
        line++;
    }
    if (*line == '\0') {
        return;
    }

    char * args = strchr(line, ' ');
    if (args != NULL) {
        *args = '\0';
        args++;
    } else {
        args = (char *)"";
    }

    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        print_help();
    } else if (strcmp(line, "wifi") == 0) {
        cmd_wifi(args);
    } else if (strcmp(line, "wifi?") == 0) {
        Serial.printf("[cli] 状态=%d SSID=%s IP=%s RSSI=%d\n",
                      (int)wifi_manager_state(), wifi_manager_ssid(), wifi_manager_ip(),
                      (int)wifi_manager_rssi());
    } else if (strcmp(line, "forget") == 0) {
        wifi_manager_forget();
        Serial.println("[cli] 已清除保存的 WiFi");
    } else if (strcmp(line, "scan") == 0) {
        cmd_scan();
    } else if (strcmp(line, "ui") == 0) {
        cmd_ui(args);
    } else if (strcmp(line, "city") == 0) {
        while (*args == ' ') {
            args++;
        }
        if (*args == '\0') {
            Serial.printf("[cli] 当前天气城市: %s\n", weather_service_city());
        } else {
            weather_service_set_city(args);
            Serial.printf("[cli] 天气城市已改为 %s，正在重新获取\n", args);
        }
    } else if (strcmp(line, "heap") == 0) {
        print_heap();
    } else if (strcmp(line, "scene") == 0) {
        while (*args == ' ') {
            args++;
        }
        if (*args == '\0') {
            Serial.printf("[cli] 场景: 0=%s 1=%s 2=%s 3=%s\n",
                          dm_scene_name(0), dm_scene_name(1), dm_scene_name(2), dm_scene_name(3));
        } else {
            int n = atoi(args);
            if (n < 0 || n >= DM_SCENE_COUNT) {
                Serial.println("[cli] 用法: scene <0-3>");
            } else {
                dm_scene_apply(n);          /* 会从串口上报"场景-xxx"一行 */
                Serial.printf("[cli] 已应用 %s\n", dm_scene_name(n));
            }
        }
    } else if (strcmp(line, "sync") == 0) {
        app_events_sync();
        Serial.println("[cli] 已重发全量状态");
    } else if (strcmp(line, "events") == 0) {
        while (*args == ' ') { args++; }
        bool on = (args[0] != '0');
        app_events_set_enabled(on);
        Serial.printf("[cli] 状态事件输出: %s\n", on ? "开" : "关");
    } else if (strcmp(line, "reboot") == 0) {
        Serial.println("[cli] 重启...");
        delay(100);
        ESP.restart();
    } else if (strcmp(line, "coffee") == 0) {
        if (strncmp(args, "start", 5) == 0) {
            dm_coffee_start();
        } else if (strncmp(args, "stop", 4) == 0) {
            dm_coffee_stop();
        } else if (strncmp(args, "type", 4) == 0) {
            dm_coffee_set_type(atoi(args + 4));
        } else if (strncmp(args, "water", 5) == 0) {
            dm_coffee()->water_pct = (uint8_t)atoi(args + 5);
        } else if (strncmp(args, "beans", 5) == 0) {
            dm_coffee()->beans_pct = (uint8_t)atoi(args + 5);
        } else {
            Serial.println("[cli] 用法: coffee start|stop|type <0-3>|water <0-100>|beans <0-100>");
        }
    } else if (strcmp(line, "shot") == 0) {
        cmd_shot(args);
    } else {
        Serial.printf("[cli] 未知命令: %s（输入 help 查看）\n", line);
    }
}

void serial_cli_init(void)
{
    Serial.println();
    print_help();
    print_heap();
}

void serial_cli_tick(void)
{
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) {
            break;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            s_line[s_len] = '\0';
            handle(s_line);
            s_len = 0;
        } else if (s_len < CLI_LINE_MAX - 1) {
            s_line[s_len++] = (char)c;
        } else {
            s_len = 0;      /* 超长丢弃 */
        }
    }

    shot_tick();            /* 截图数据分块外发（没在截图时立即返回） */
}
