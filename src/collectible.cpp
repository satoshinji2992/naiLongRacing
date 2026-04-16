#include "collectible.h"

#include <cstdlib>

#include "config.h"

Nailong make_nailong(float x, float y, float z)
{
    Nailong nailong;
    nailong.p[0] = make_point(x, y, z);
    nailong.p[1] = make_point(x + 1200.0f, y, z);
    nailong.p[2] = make_point(x + 1200.0f, y + 1800.0f, z);
    nailong.p[3] = make_point(x, y + 1800.0f, z);
    nailong.eaten = false;
    return nailong;
}

void project_nailong(Nailong *nailong, int camX, int camY, int camZ, float angle)
{
    for (int i = 0; i < 4; i++) {
        project_point(&nailong->p[i], camX, camY, camZ, angle);
    }
}

void reset_nailong(Nailong *nailong, const Road *road)
{
    float x = road->x - ROAD_WIDTH / 2.0f + (float)(std::rand() % 1500);
    *nailong = make_nailong(x, road->y, road->z);
}

bool collect_nailong(Nailong *nailong, int camX, int camZ)
{
    if (nailong->eaten) {
        return false;
    }

    if (camZ < nailong->p[0].z + 100.0f &&
        camZ > nailong->p[0].z - 100.0f &&
        camX > nailong->p[0].x - 180.0f &&
        camX < nailong->p[1].x + 180.0f) {
        nailong->eaten = true;
        return true;
    }

    return false;
}

void reset_collectibles(Nailong nailongs[3], const std::vector<Road> *roads, int positions[3])
{
    positions[0] = std::rand() % 500 + 200;
    positions[1] = std::rand() % 500 + 700;
    positions[2] = std::rand() % 500 + 1200;

    for (int i = 0; i < 3; i++) {
        reset_nailong(&nailongs[i], &(*roads)[positions[i]]);
    }
}
