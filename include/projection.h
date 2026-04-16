#ifndef RACING_PROJECTION_H
#define RACING_PROJECTION_H

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

Point make_point(float x, float y, float z);
void project_point(Point *point, int camX, int camY, int camZ, float angle);

#endif
