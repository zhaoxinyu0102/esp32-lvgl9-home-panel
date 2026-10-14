/**
 * @file serial_cli.h
 * @brief 串口命令行：不依赖屏幕就能配网 / 查状态 / 切屏，白屏时也能用。
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** 打印帮助 */
void serial_cli_init(void);

/** 在后台任务里周期调用（非阻塞读串口） */
void serial_cli_tick(void);

#ifdef __cplusplus
}
#endif
