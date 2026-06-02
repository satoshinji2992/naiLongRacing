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
} RacingGame;

void racing_game_init(RacingGame *game, unsigned int seed);
void racing_game_reset_to_start(RacingGame *game);
void racing_game_start(RacingGame *game);
void racing_game_update(RacingGame *game, const RacingInput *input, int deltaMs);
int racing_game_start_segment(const RacingGame *game);

#endif
