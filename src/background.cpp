#include "background.h"

#include <cmath>

#include "config.h"

static float wrap_offset(float value, float size)
{
    float offset = std::fmod(value, size);

    if (offset > 0.0f)
    {
        offset -= size;
    }

    return offset;
}

void draw_sky(sf::RenderWindow *window)
{
    sf::RectangleShape sky(sf::Vector2f((float)WIN_WIDTH, (float)WIN_HEIGHT));

    sky.setFillColor(sf::Color(95, 184, 232));
    window->draw(sky);
}

void draw_parallax_layer(sf::RenderWindow *window, const sf::Texture *texture, int camX, int camY, float baseY, float xFactor, float yFactor, float xOffset, float scale)
{
    sf::Sprite sprite(*texture);
    float width = (float)texture->getSize().x * scale;
    float x = wrap_offset(xOffset - camX * xFactor, width);
    float y = baseY + (2000.0f - camY) * yFactor;

    sprite.setScale(scale, scale);

    for (int i = 0; i < 3; i++)
    {
        sprite.setPosition(x + width * i, y);
        window->draw(sprite);
    }
}
