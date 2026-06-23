#ifndef RACING_INPUT_H
#define RACING_INPUT_H

#include "game.h"

/* 初始化触摸(/dev/input0)与 GPIO 按键。 */
void racing_input_init(void);

/* 释放设备。 */
void racing_input_deinit(void);

/* 非阻塞读触摸采样,更新内部输入状态。主循环每帧调用。 */
void racing_input_poll(void);

/* 取当前输入(并清掉 start/pause/restart 等一次性标志)。 */
RacingInput racing_input_get(void);

/* 清空所有输入。 */
void racing_input_reset(void);

#endif
