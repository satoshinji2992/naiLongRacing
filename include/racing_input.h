#ifndef RACING_INPUT_H
#define RACING_INPUT_H

#include "game.h"

typedef enum RacingMenuSelection {
    RACING_MENU_NONE = 0,
    RACING_MENU_ORIGINAL,
    RACING_MENU_GYRO,
    RACING_MENU_TEST,
    RACING_MENU_MAP_CYCLE   /* 触摸菜单顶部 = 循环切换地图 */
} RacingMenuSelection;

/* 初始化触摸(/dev/input0)与 GPIO 按键。 */
void racing_input_init(void);

/* 释放设备。 */
void racing_input_deinit(void);

/* 非阻塞读触摸采样,更新内部输入状态。主循环每帧调用。 */
void racing_input_poll(void);

/* 取当前输入(并清掉 start/pause/restart 等一次性标志)。 */
RacingInput racing_input_get(void);

/* 取主界面的模式选择(一次性标志)。 */
RacingMenuSelection racing_input_get_menu_selection(void);

/* 触摸按住 0.1s 后返回 true；调用方决定是否把它映射为 boost。 */
bool racing_input_get_touch_boost(void);

/* 清空所有输入。 */
void racing_input_reset(void);

#endif
