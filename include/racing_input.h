#ifndef RACING_INPUT_H
#define RACING_INPUT_H

#include "game.h"

/* 初始化触摸(/dev/input0)与 GPIO 按键。 */
void racing_input_init(void);

/* 释放设备。 */
void racing_input_deinit(void);

/* 非阻塞读触摸采样,更新内部输入状态。主循环每帧调用。 */
void racing_input_poll(void);

/* 取当前输入(并清掉 start/pause/restart/back 等一次性标志)。 */
RacingInput racing_input_get(void);

/* 告诉输入层当前菜单屏(RACING_MODE_START/MAP_SELECT/CONTROL_SELECT);
 * 非菜单(游戏中 / 暂停)传 -1。触摸按屏映射成对应动作。
 * (不能用 0:RACING_MODE_START==0,会和主菜单冲突。) */
void racing_input_set_menu_screen(int mode);

/* 告诉输入层游戏中操控模式:gyro!=0 为体感模式(触摸=加速,不转向),
 * 否则普通模式(左/右 1/3 转向,中间 1/3 切换加速)。 */
void racing_input_set_drive_mode(int gyro);

/* 清空所有输入(进入新界面/新一局时调用)。 */
void racing_input_reset(void);

#endif
