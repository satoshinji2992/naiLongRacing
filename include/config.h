#ifndef RACING_CONFIG_H
#define RACING_CONFIG_H

#ifndef WIN_WIDTH
#define WIN_WIDTH 480
#endif

#ifndef WIN_HEIGHT
#define WIN_HEIGHT 320
#endif

#ifndef ROAD_WIDTH
#define ROAD_WIDTH 2300
#endif

#ifndef SEG_LENGTH
#define SEG_LENGTH 180
#endif

#ifndef ROAD_COUNT
#define ROAD_COUNT 1884
#endif

#ifndef VIEW_DISTANCE
#define VIEW_DISTANCE 60
#endif

#ifndef COLLECTIBLE_COUNT
#define COLLECTIBLE_COUNT 3
#endif

#ifndef NAILONG_HIT_Z_MARGIN
/* 奶龙碰撞的前后判定范围，越大越容易吃到。 */
#define NAILONG_HIT_Z_MARGIN 200.0f
#endif

#ifndef NAILONG_HIT_X_MARGIN
/* 奶龙碰撞的左右判定范围，越大越容易吃到。 */
#define NAILONG_HIT_X_MARGIN 200.0f
#endif

#ifndef RACING_TARGET_FRAME_MS
/* 桌面和嵌入式共用的目标帧耗时，33ms 约等于 30FPS。 */
#define RACING_TARGET_FRAME_MS 33
#endif

#ifndef RACING_LVGL_FRAME_MS
/* LVGL 刷新节拍直接跟随统一帧率。 */
#define RACING_LVGL_FRAME_MS RACING_TARGET_FRAME_MS
#endif

#ifndef RACING_PRINT_FRAME_TIME
#define RACING_PRINT_FRAME_TIME 0
#endif

#define TRACK_LENGTH (ROAD_COUNT * SEG_LENGTH)

#endif
