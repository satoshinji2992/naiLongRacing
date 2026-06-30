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
} Nailong;

/* 路边树：纯装饰精灵，种在空气墙外，无需碰撞。投影时按贴地 billboard 计算屏幕四角。 */
typedef struct Tree {
    float centerWorldX;   /* 树中心的世界 x，横向定位用 */
    int segment;          /* 所在赛段，视锥剔除用 */
    int segDist;          /* 本帧相对摄像机的向前段距(>0 且 <=VIEW_DISTANCE 才可见) */
    bool visible;         /* 本帧是否在视野内，渲染端读 */
    Point p[4];           /* 投影后的屏幕四角(渲染端读);p[].tz 用于剔除 */
} Tree;

/* 路边房子：立体盒子装饰(正面 + 压暗素侧墙合成贴图)，种在空气墙外，无需碰撞。 */
typedef struct House {
    float centerWorldX;   /* 房子(正面+侧面整体)中心的世界 x */
    int segment;          /* 所在赛段，视锥剔除用 */
    int segDist;          /* 本帧相对摄像机的向前段距 */
    int type;             /* 0=house_a(小楼)，1=house_b(神庙盒) */
    int side;             /* +1 路右(正面朝左)，-1 路左(正面朝右，用镜像贴图) */
    bool visible;         /* 本帧是否在视野内，渲染端读 */
    Point p[4];           /* 投影后的屏幕四角(贴地 billboard);p[].tz 用于剔除 */
} House;

typedef enum RacingMode {
    RACING_MODE_START,
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
} RacingInput;

typedef struct RacingGame {
    RacingMode mode;
    Road roads[ROAD_COUNT];
    Nailong collectibles[COLLECTIBLE_COUNT];
    Tree trees[TREE_COUNT];
    House houses[HOUSE_COUNT];
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
    int wallHitFrames;   /* 撞墙反馈剩余帧，渲染端读 */
    int wallHitSide;     /* -1=撞左墙 +1=撞右墙 0=无，渲染端按侧画红边 */
} RacingGame;

void racing_game_init(RacingGame *game, unsigned int seed);
void racing_game_reset_to_start(RacingGame *game);
void racing_game_start(RacingGame *game);
void racing_game_update(RacingGame *game, const RacingInput *input, int deltaMs);
int racing_game_start_segment(const RacingGame *game);

#endif
