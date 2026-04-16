#ifndef RACING_FINISH_FLAG_H
#define RACING_FINISH_FLAG_H

#include <SFML/Graphics.hpp>

#include "track.h"

void draw_finish_flag(sf::RenderWindow *window, const Road *road, int camX, int camY, int camZ, float angle);

#endif
