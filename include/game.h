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

/* 路边树：纯装饰精灵，种在空气墙外，无碰撞。投影时按贴地 billboard 算屏幕 4 角。 */
typedef struct Tree {
    float centerWorldX;   /* 树中心的世界 x(横向定位) */
    int segment;          /* 所在赛段(视锥剔除用) */
    int segDist;          /* 本帧相对摄像机的向前段距(>0 且 <=VIEW_DISTANCE 才可见) */
    bool visible;         /* 本帧是否在视野内(渲染端读) */
    Point p[4];           /* 投影后的屏幕 4 角(渲染端读);p[].tz 用于剔除 */
} Tree;

/* 路边房子：立方体装饰(正面/侧/顶三面贴图)，种在空气墙外，无碰撞。 */
typedef struct House {
    float centerWorldX;   /* 房子中心的世界 x */
    int segment;          /* 所在赛段 */
    int segDist;          /* 本帧相对摄像机的向前段距 */
    int type;             /* 0=house_a(小楼) 1=house_b(神庙) */
    int side;             /* +1 路右(正面朝左) -1 路左(正面朝右) */
    bool visible;         /* 本帧是否在视野内(渲染端读) */
    Point corner[8];      /* 投影后的 8 个屏幕角(渲染端组三个可见面) */
} House;

typedef enum RacingMode {
    RACING_MODE_START,
    RACING_MODE_MAP_SELECT,      /* 关卡/赛道选择界面 */
    RACING_MODE_CONTROL_SELECT,  /* 操作/操控方式选择界面 */
    RACING_MODE_NETWORK_SELECT,  /* 网络/小智语音启动界面 */
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
    bool mapSelect;       /* 开始界面 → 进入关卡选择 */
    bool controlSelect;   /* 开始界面 → 进入操作选择 */
    bool networkSelect;   /* 开始界面 → 进入网络/语音启动页 */
    bool back;            /* 二级菜单 → 返回主菜单 */
    bool cyclePrev;       /* 二级菜单：上一项 */
    bool cycleNext;       /* 二级菜单：下一项 */
    bool toMenu;          /* 暂停/胜利 → 返回主菜单 */
    bool ctrl1;           /* 操作选择:直接选第 1 项(Original) */
    bool ctrl2;           /* 操作选择:直接选第 2 项(Gyro) */
    bool ctrl3;           /* 操作选择:直接选第 3 项(Test) */
    bool nailongCheat;    /* 语音"我是奶龙":开启无限能量作弊 */
    bool voiceBoost;      /* 语音"加速":短时间 boost */
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
    bool boosting;          /* 本帧 boost 是否生效(供 HUD 显示) */
    bool infiniteEnergy;    /* "我是奶龙" 作弊:能量无限(渲染端读,黄色显示) */
    int wallHitFrames;      /* 撞空气墙红边反馈剩余帧(渲染端读) */
    int wallHitSide;        /* -1=撞左墙 +1=撞右墙 0=无 */
    int mapIndex;
    float roadWidth;
    int menuControlMode;        /* 操作选择:0=Original 1=Gyro 2=Test */
    int voiceState;             /* 小智/control_center 状态,取值同 racing_voice_state() */
    char voiceText[64];         /* 最近一条 UI IPC 文本,主菜单显示用 */
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
