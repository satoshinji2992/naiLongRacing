#include "draw.h"

#include <cmath>

#include "config.h"

void draw_nailong(sf::RenderWindow *window, sf::Texture *texture, const Nailong *nailong)
{
    sf::ConvexShape polygon(4);
    polygon.setTexture(texture);
    polygon.setPoint(0, sf::Vector2f(nailong->p[0].X, nailong->p[0].Y));
    polygon.setPoint(1, sf::Vector2f(nailong->p[1].X, nailong->p[1].Y));
    polygon.setPoint(2, sf::Vector2f(nailong->p[2].X, nailong->p[2].Y));
    polygon.setPoint(3, sf::Vector2f(nailong->p[3].X, nailong->p[3].Y));
    window->draw(polygon);
}

void draw_trapezoid(sf::RenderWindow *window, sf::Color color, int x1, int y1, int w1, int x2, int y2, int w2, float angle)
{
    sf::ConvexShape polygon(4);
    polygon.setFillColor(color);
    polygon.setPoint(0, sf::Vector2f(x1 - w1 * std::cos(angle), y1 - w1 * std::sin(angle)));
    polygon.setPoint(1, sf::Vector2f(x2 - w2 * std::cos(angle), y2 - w2 * std::sin(angle)));
    polygon.setPoint(2, sf::Vector2f(x2 + w2 * std::cos(angle), y2 + w2 * std::sin(angle)));
    polygon.setPoint(3, sf::Vector2f(x1 + w1 * std::cos(angle), y1 + w1 * std::sin(angle)));
    window->draw(polygon);
}

void draw_energy(sf::RenderWindow *window, int count)
{
    for (int i = 0; i < count; i++) {
        sf::RectangleShape energy(sf::Vector2f(20.0f, 50.0f));
        energy.setFillColor(sf::Color::Blue);
        energy.setPosition(670.0f + 30.0f * i, 700.0f);
        window->draw(energy);
    }
}
