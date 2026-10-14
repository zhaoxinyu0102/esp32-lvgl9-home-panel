/**
 * @file lv_mem_psram.c
 * @brief 让 LVGL 的堆优先使用 PSRAM。
 *
 * 为什么需要它：
 *   界面一共有 8 个屏，每个屏的控件 + 局部样式都是 LVGL 动态分配的。默认走内部 SRAM，
 *   内部 SRAM 还要留给 WiFi / TCP-IP / 图形缓冲，屏一多就会吃紧；一旦分配失败，
 *   LVGL 里就会出现"点某个页面直接卡死/重启"这类现象。
 *   板子上有 8MB PSRAM（帧缓冲只用了 2.3MB），把 LVGL 的对象/样式放到 PSRAM 里，
 *   内部 SRAM 就宽松了。
 *
 * 打开方式：src/lv_conf.h 里 `#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM`
 */

#include "lvgl.h"

#include "esp_heap_caps.h"

#define LV_MEM_PSRAM_CAPS   (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

/* LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM 时，LVGL 要求由用户完成堆的初始化/反初始化。
 * 这里直接用系统的 heap_caps，无需额外初始化。 */
void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

void * lv_malloc_core(size_t size)
{
    /* 优先 PSRAM，PSRAM 不够时自动退回内部 RAM */
    return heap_caps_malloc_prefer(size, 2, LV_MEM_PSRAM_CAPS, MALLOC_CAP_DEFAULT);
}

void * lv_realloc_core(void * p, size_t new_size)
{
    return heap_caps_realloc_prefer(p, new_size, 2, LV_MEM_PSRAM_CAPS, MALLOC_CAP_DEFAULT);
}

void lv_free_core(void * p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t * mon_p)
{
    if (mon_p == NULL) {
        return;
    }

    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);

    size_t total = info.total_free_bytes + info.total_allocated_bytes;
    size_t used = info.total_allocated_bytes;

    mon_p->total_size = total;
    mon_p->free_size = info.total_free_bytes;
    mon_p->free_biggest_size = info.largest_free_block;
    mon_p->used_cnt = (uint32_t)info.allocated_blocks;
    mon_p->free_cnt = (uint32_t)info.free_blocks;
    mon_p->used_pct = (total > 0) ? (uint8_t)(used * 100 / total) : 0;
    mon_p->frag_pct = (info.total_free_bytes > 0)
                      ? (uint8_t)(100 - info.largest_free_block * 100 / info.total_free_bytes) : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}
