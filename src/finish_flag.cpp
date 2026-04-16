#include "finish_flag.h"

#include "projection.h"

static Point projected_world_point(float x, float y, float z, int camX, int camY, int camZ, float angle)
{
    Point point = make_point(x, y, z);
    project_point(&point, camX, camY, camZ, angle);
    return point;
}

static void draw_projected_quad(sf::RenderWindow *window, sf::Color color, Point a, Point b, Point c, Point d)
{
    sf::ConvexShape quad(4);

    quad.setFillColor(color);
    quad.setPoint(0, sf::Vector2f(a.X, a.Y));
    quad.setPoint(1, sf::Vector2f(b.X, b.Y));
    quad.setPoint(2, sf::Vector2f(c.X, c.Y));
    quad.setPoint(3, sf::Vector2f(d.X, d.Y));
    window->draw(quad);
}

static void draw_world_rect(sf::RenderWindow *window, sf::Color color, float x1, float y1, float x2, float y2, float z, int camX, int camY, int camZ, float angle)
{
    draw_projected_quad(
        window,
        color,
        projected_world_point(x1, y1, z, camX, camY, camZ, angle),
        projected_world_point(x2, y1, z, camX, camY, camZ, angle),
        projected_world_point(x2, y2, z, camX, camY, camZ, angle),
        projected_world_point(x1, y2, z, camX, camY, camZ, angle));
}

void draw_finish_flag(sf::RenderWindow *window, const Road *road, int camX, int camY, int camZ, float angle)
{
    float z = road->z;
    float leftX = road->x - 2200.0f;
    float rightX = road->x + 2200.0f;
    float poleWidth = 140.0f;
    float poleTopY = road->y + 3700.0f;
    float flagTopY = road->y + 3560.0f;
    float flagBottomY = road->y + 2460.0f;
    float flagLeftX = leftX + poleWidth;
    float flagRightX = rightX - poleWidth;
    float cellWidth = (flagRightX - flagLeftX) / 6.0f;
    float cellHeight = (flagTopY - flagBottomY) / 3.0f;
    int columns = 6;
    int rows = 3;

    draw_world_rect(window, sf::Color(235, 45, 24), leftX, road->y, leftX + poleWidth, poleTopY, z, camX, camY, camZ, angle);
    draw_world_rect(window, sf::Color(235, 45, 24), rightX - poleWidth, road->y, rightX, poleTopY, z, camX, camY, camZ, angle);

    for (int row = 0; row < rows; row++)
    {
        for (int column = 0; column < columns; column++)
        {
            float x1 = flagLeftX + cellWidth * column;
            float x2 = flagLeftX + cellWidth * (column + 1);
            float y1 = flagTopY - cellHeight * row;
            float y2 = flagTopY - cellHeight * (row + 1);
            sf::Color color = (row + column) % 2 == 0 ? sf::Color::White : sf::Color::Black;

            draw_world_rect(window, color, x1, y1, x2, y2, z, camX, camY, camZ, angle);
        }
    }

    draw_world_rect(window, sf::Color(247, 169, 25), leftX - 340.0f, road->y + 1300.0f, leftX + 480.0f, road->y + 2100.0f, z, camX, camY, camZ, angle);
    draw_world_rect(window, sf::Color(247, 169, 25), rightX - 480.0f, road->y + 1300.0f, rightX + 340.0f, road->y + 2100.0f, z, camX, camY, camZ, angle);
    draw_world_rect(window, sf::Color(71, 88, 151), leftX - 300.0f, road->y + 40.0f, leftX + 440.0f, road->y + 180.0f, z, camX, camY, camZ, angle);
    draw_world_rect(window, sf::Color(71, 88, 151), rightX - 440.0f, road->y + 40.0f, rightX + 300.0f, road->y + 180.0f, z, camX, camY, camZ, angle);
}
