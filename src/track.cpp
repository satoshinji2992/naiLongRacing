#include "track.h"

#include <cmath>

#include "config.h"

Road make_road(float x, float y, float z)
{
    Road road;
    road.x = x;
    road.y = y;
    road.z = z;
    road.X = 0.0f;
    road.Y = 0.0f;
    road.W = 0.0f;
    road.scale = 0.0f;
    road.tz = 0.0f;
    road.tx = 0.0f;
    return road;
}

void project_road(Road *road, int camX, int camY, int camZ, float angle)
{
    road->tx = (road->x - camX) * std::cos(angle) + (road->z - camZ) * std::sin(angle);
    road->tz = -(road->x - camX) * std::sin(angle) + (road->z - camZ) * std::cos(angle);

    if (road->tz < 0.1f) {
        road->tz = 0.1f;
    }

    road->scale = 1.0f / road->tz;
    road->X = (1.0f + road->scale * road->tx) * WIN_WIDTH / 2.0f;
    road->Y = (1.0f - road->scale * (road->y - camY)) * WIN_HEIGHT / 2.0f;
    road->W = road->scale * ROAD_WIDTH * WIN_WIDTH / 2.0f;
}

void build_track(std::vector<Road> *roads)
{
    float x = 0.0f;

    roads->clear();
    roads->reserve(ROAD_COUNT);

    for (int i = 0; i < ROAD_COUNT; i++) {
        float curve;
        int y = 0;

        if (i <= 123 || (456 < i && i <= 789) || (1234 < i && i <= 1721)) {
            curve = 0.5f;
        } else {
            curve = -0.5f;
        }

        x += curve;

        if (i > 300 && i < 1240) {
            y = (int)(1600.0f * std::sin(i / 30.0f - 10.0f));
        }

        roads->push_back(make_road(x * 45.0f, (float)y, (float)((1 + i) * SEG_LENGTH)));
    }
}
