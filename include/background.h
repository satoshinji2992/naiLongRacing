#ifndef RACING_BACKGROUND_H
#define RACING_BACKGROUND_H

#include <SFML/Graphics.hpp>

void draw_sky(sf::RenderWindow *window);
void draw_parallax_layer(sf::RenderWindow *window, const sf::Texture *texture, int camX, int camY, float baseY, float xFactor, float yFactor, float xOffset, float scale);

#endif
