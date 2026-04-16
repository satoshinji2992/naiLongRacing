#ifndef RACING_COLLECTIBLE_H
#define RACING_COLLECTIBLE_H

#include <vector>

#include "projection.h"
#include "track.h"

typedef struct Nailong {
    Point p[4];
    bool eaten;
} Nailong;

Nailong make_nailong(float x, float y, float z);
void project_nailong(Nailong *nailong, int camX, int camY, int camZ, float angle);
void reset_nailong(Nailong *nailong, const Road *road);
bool collect_nailong(Nailong *nailong, int camX, int camZ);
void reset_collectibles(Nailong nailongs[3], const std::vector<Road> *roads, int positions[3]);

#endif
