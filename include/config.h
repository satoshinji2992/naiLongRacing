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

#ifndef AIR_WALL_HALF
/* 空气墙距赛道中心的横向距离，略在路缘(ROAD_WIDTH/1.5)外侧，留一点出界缓冲。 */
#define AIR_WALL_HALF (ROAD_WIDTH / 1.2f)
#endif

#ifndef AIR_WALL_BOUNCE
/* 撞墙瞬间向赛道内侧的弹回距离，给一点「弹」的手感。 */
#define AIR_WALL_BOUNCE 120
#endif

#ifndef AIR_WALL_FEEDBACK_FRAMES
/* 撞墙红色边带反馈持续的帧数，越大反馈越久。 */
#define AIR_WALL_FEEDBACK_FRAMES 12
#endif

#ifndef AIR_WALL_ENERGY_COST
/* 贴墙时每帧扣除的能量，作为可见反馈，阻止磨墙。 */
#define AIR_WALL_ENERGY_COST 2
#endif

#ifndef TREE_COUNT
/* 路边树数量，沿整条赛道随机分布在空气墙外(纯装饰，无需碰撞)。 */
#define TREE_COUNT 220
#endif

#ifndef TREE_WORLD_W
/* 树在世界里的宽度(横向)。与 raw 200:320 同比例，blit 时不变形。 */
#define TREE_WORLD_W 2000.0f
#endif

#ifndef TREE_WORLD_H
/* 树在世界里的高度(纵向，向上生长)。 */
#define TREE_WORLD_H 3200.0f
#endif

#ifndef TREE_MIN_BEYOND
/* 树根离空气墙的最小额外距离(世界单位)。 */
#define TREE_MIN_BEYOND 200
#endif

#ifndef TREE_SPREAD
/* 树根离墙距离的随机范围(加在 TREE_MIN_BEYOND 上)。 */
#define TREE_SPREAD 1200
#endif

#ifndef HOUSE_COUNT
/* 路边房子数量(空气墙外，立体盒子装饰)。 */
#define HOUSE_COUNT 50
#endif

#ifndef HOUSE_WORLD_W
/* 房子(正面)在世界里的横向宽度。与 raw 160:200(0.8)同屏比例，blit 不变形。 */
#define HOUSE_WORLD_W 2000.0f
#endif

#ifndef HOUSE_WORLD_H
/* 房子在世界里的高度。W/H=0.533 → 屏上 0.8 竖比，匹配正面 raw。 */
#define HOUSE_WORLD_H 3750.0f
#endif

#ifndef HOUSE_MIN_BEYOND
/* 房子离空气墙的最小额外距离(世界单位)。 */
#define HOUSE_MIN_BEYOND 200
#endif

#ifndef HOUSE_SPREAD
/* 房子离墙距离的随机范围(加在 HOUSE_MIN_BEYOND 上)。 */
#define HOUSE_SPREAD 1400
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
