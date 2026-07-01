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

#ifndef RACING_MAP_COUNT
/* 固定地图数量：0=简单 1=中等 2=困难（分段随机蜿蜒赛道）。 */
#define RACING_MAP_COUNT 3
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

#ifndef SLOW_GRASS_HALF_SCALE
/* 超出铺装路面后进入减速草地；1.0f 约等于渲染路面半宽。 */
#define SLOW_GRASS_HALF_SCALE 1.05f
#endif

#ifndef AIR_WALL_HALF_SCALE
/* 空气墙在减速草地外侧，按当前地图实际 roadWidth 缩放。 */
#define AIR_WALL_HALF_SCALE 2.65f
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
/* 树在世界里的宽度(横向)。 */
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
/* 房子在世界里的横向宽度。 */
#define HOUSE_WORLD_W 2000.0f
#endif

#ifndef HOUSE_WORLD_H
/* 房子在世界里的高度。 */
#define HOUSE_WORLD_H 3750.0f
#endif

#ifndef HOUSE_WORLD_D
/* 房子在世界里的纵向深度(沿赛道方向)。 */
#define HOUSE_WORLD_D 2000.0f
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
