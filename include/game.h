#ifndef RACING_GAME_H
#define RACING_GAME_H

#include <stdbool.h>

#include "config.h"

typedef struct Point {
    float x;
    float y;
    float z;
    float X;
    float Y;
    float scale;
    float tx;
    float tz;
} Point;

typedef struct Road {
    float x;
    float y;
    float z;
    float X;
    float Y;
    float W;
    float scale;
    float tz;
    float tx;
} Road;

typedef struct Nailong {
    Point p[4];
    bool eaten;
    int respawnMs;   /* 被吃后到重生前的倒计时；<=0 表示不重生或已就绪 */
} Nailong;

typedef enum RacingMode {
    RACING_MODE_START,
    RACING_MODE_MAP_SELECT,   /* 关卡/地图选择界面 */
    RACING_MODE_PLAYING,
    RACING_MODE_PAUSED,
    RACING_MODE_WIN
} RacingMode;

typedef struct RacingInput {
    bool accelerate;
    bool brake;
    bool left;
    bool right;
    bool boost;
    bool fly;
    bool start;
    bool restart;
    bool pause;
    bool map1;
    bool map2;
    bool map3;
    bool mapSelect;   /* 开始界面 → 进入地图选择 */
    bool back;        /* 地图选择 → 返回开始界面 */
    bool cyclePrev;   /* 地图选择：上一张图 */
    bool cycleNext;   /* 地图选择：下一张图 */
} RacingInput;

typedef struct RacingGame {
    RacingMode mode;
    Road roads[ROAD_COUNT];
    Nailong collectibles[COLLECTIBLE_COUNT];
    int collectiblePositions[COLLECTIBLE_COUNT];
    int camX;
    int camY;
    int camZ;
    int energy;
    int lap;
    int hitFrames;
    int finalSeconds;
    int flyFrames;
    int elapsedMs;
    float angle;
    float distance;
    bool turnLeft;
    bool turnRight;
    bool isOut;
    bool isFlying;
    int mapIndex;
    float roadWidth;
    int collectibleRespawnMs;   /* 收集物被吃后多久重生(地图2 校徽刷新更快)；0=不重生 */
} RacingGame;

void racing_game_init(RacingGame *game, unsigned int seed);
void racing_game_set_map(RacingGame *game, int mapIndex);
const char *racing_game_map_name(int mapIndex);
const char *racing_game_map_name_ascii(int mapIndex);
void racing_game_reset_to_start(RacingGame *game);
void racing_game_start(RacingGame *game);
void racing_game_update(RacingGame *game, const RacingInput *input, int deltaMs);
int racing_game_start_segment(const RacingGame *game);

#endif
