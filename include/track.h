#ifndef RACING_TRACK_H
#define RACING_TRACK_H

#include <vector>

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

Road make_road(float x, float y, float z);
void project_road(Road *road, int camX, int camY, int camZ, float angle);
void build_track(std::vector<Road> *roads);

#endif
