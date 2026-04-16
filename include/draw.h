#ifndef RACING_DRAW_H
#define RACING_DRAW_H

#include <SFML/Graphics.hpp>

#include "collectible.h"

void draw_nailong(sf::RenderWindow *window, sf::Texture *texture, const Nailong *nailong);
void draw_trapezoid(sf::RenderWindow *window, sf::Color color, int x1, int y1, int w1, int x2, int y2, int w2, float angle);
void draw_energy(sf::RenderWindow *window, int count);

#endif
