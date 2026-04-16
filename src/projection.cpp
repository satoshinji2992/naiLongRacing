#include "projection.h"

#include <cmath>

#include "config.h"

Point make_point(float x, float y, float z)
{
    Point point;
    point.x = x;
    point.y = y;
    point.z = z;
    point.X = 0.0f;
    point.Y = 0.0f;
    point.scale = 0.0f;
    point.tx = 0.0f;
    point.tz = 0.0f;
    return point;
}

void project_point(Point *point, int camX, int camY, int camZ, float angle)
{
    point->tx = (point->x - camX) * std::cos(angle) + (point->z - camZ) * std::sin(angle);
    point->tz = -(point->x - camX) * std::sin(angle) + (point->z - camZ) * std::cos(angle);

    if (point->tz < 0.1f) {
        point->tz = 0.1f;
    }

    point->scale = 1.0f / point->tz;
    point->X = (1.0f + point->scale * point->tx) * WIN_WIDTH / 2.0f;
    point->Y = (1.0f - point->scale * (point->y - camY)) * WIN_HEIGHT / 2.0f;
}
