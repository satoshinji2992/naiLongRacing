#include "render_sdl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL_image.h>
#include <SDL_ttf.h>

#ifndef ASSET_DIR
#define ASSET_DIR "assets"
#endif

typedef struct ProjectedRoad
{
    float x;
    float y;
    float w;
    float tz;
} ProjectedRoad;

typedef struct ProjectedPoint
{
    float x;
    float y;
} ProjectedPoint;

struct RacingSdlRenderer
{
    SDL_Renderer *renderer;
    SDL_Texture *clouds;
    SDL_Texture *mountainFar;
    SDL_Texture *mountainNear;
    SDL_Texture *car;
    SDL_Texture *nailong;
    SDL_Texture *nailongHead;
    SDL_Texture *nailongBupt;   /* 地图“一路邮你”的收集物：北邮校徽 */
    SDL_Texture *bgBupt;        /* 地图“一路邮你”的背景：北邮校门 */
    TTF_Font *font;
    TTF_Font *titleFont;   /* 大号字体，菜单标题用 */
    bool imageReady;
    bool ttfReady;
};

static SDL_Texture *load_texture(SDL_Renderer *renderer, const char *path)
{
    SDL_Texture *texture = IMG_LoadTexture(renderer, path);
    if (texture == NULL)
    {
        fprintf(stderr, "Could not load %s: %s\n", path, IMG_GetError());
    }
    return texture;
}

/* 释放纹理时统一走这个小函数，避免散落的空指针处理。 */
static void destroy_texture(SDL_Texture **texture)
{
    if (*texture != NULL)
    {
        SDL_DestroyTexture(*texture);
        *texture = NULL;
    }
}

/* 字体文字走单独的贴图路径，方便 HUD 和提示文本复用。 */
static void draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color)
{
    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_Rect dst;

    if (font == NULL || text == NULL || text[0] == '\0')
    {
        return;
    }

    surface = TTF_RenderUTF8_Blended(font, text, color);
    if (surface == NULL)
    {
        return;
    }

    texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == NULL)
    {
        SDL_FreeSurface(surface);
        return;
    }

    dst.x = x;
    dst.y = y;
    dst.w = surface->w;
    dst.h = surface->h;
    SDL_RenderCopy(renderer, texture, NULL, &dst);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

/* 右下角能量条，空槽和已满槽分开画，便于低成本更新。 */
static void draw_energy(SDL_Renderer *renderer, int energy)
{
    enum
    {
        BAR_COUNT = 10,
        BAR_W = 10,
        BAR_H = 15,
        BAR_GAP = 3
    };
    int bars = (energy + 99) / 100;
    int panelW = BAR_W + 12;
    int panelH = BAR_COUNT * BAR_H + (BAR_COUNT - 1) * BAR_GAP + 12;
    int panelX = WIN_WIDTH - panelW - 10;
    int panelY = WIN_HEIGHT - panelH - 10;

    if (bars < 0)
    {
        bars = 0;
    }
    if (bars > BAR_COUNT)
    {
        bars = BAR_COUNT;
    }

    {
        SDL_Rect panel = {panelX, panelY, panelW, panelH};
        SDL_SetRenderDrawColor(renderer, 13, 23, 48, 220);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 127, 151, 199, 180);
        SDL_RenderDrawRect(renderer, &panel);
    }

    for (int i = 0; i < BAR_COUNT; i++)
    {
        int y = panelY + 6 + (BAR_COUNT - 1 - i) * (BAR_H + BAR_GAP);
        SDL_Rect slot = {panelX + 6, y, BAR_W, BAR_H};
        SDL_SetRenderDrawColor(renderer, 23, 48, 90, 255);
        SDL_RenderFillRect(renderer, &slot);
    }

    for (int i = 0; i < bars; i++)
    {
        int y = panelY + 6 + (BAR_COUNT - 1 - i) * (BAR_H + BAR_GAP);
        SDL_Rect bar = {panelX + 6, y, BAR_W, BAR_H};
        SDL_SetRenderDrawColor(renderer, 29, 99, 255, 255);
        SDL_RenderFillRect(renderer, &bar);
    }
}

/* 中央信息面板，开始/暂停/结束页都复用。 */
static void draw_center_panel(SDL_Renderer *renderer, int width, int height, SDL_Color fill, SDL_Color border)
{
    SDL_Rect panel = {
        (WIN_WIDTH - width) / 2,
        (WIN_HEIGHT - height) / 2,
        width,
        height
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &panel);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

/* 统一绘制开始/暂停/结束时的操作提示。 */
/* 水平居中绘制一行文字(cy 为文字顶部 y)。 */
static void draw_text_centered(SDL_Renderer *r, TTF_Font *font, const char *text, int cy, SDL_Color col)
{
    int tw = 0;
    int th = 0;
    if (font != NULL && text != NULL)
    {
        TTF_SizeUTF8(font, text, &tw, &th);
    }
    draw_text(r, font, text, (WIN_WIDTH - tw) / 2, cy, col);
}

/* 难度指示:三格,填充数 = 难度(简单 1 / 中等 2 / 困难 3),颜色随难度。 */
static void draw_difficulty(SDL_Renderer *r, int cx, int cy, int mapIndex)
{
    const int n = 3;
    const int gap = 8;
    const int sz = 10;
    const Uint8 col[3][3] = {{80, 200, 110}, {240, 200, 70}, {230, 90, 80}};
    int totalW = n * sz + (n - 1) * gap;
    int x0 = cx - totalW / 2;
    int filled = mapIndex + 1;
    int i;
    for (i = 0; i < n; i++)
    {
        SDL_Rect d = {x0 + i * (sz + gap), cy, sz, sz};
        if (i < filled)
        {
            SDL_SetRenderDrawColor(r, col[mapIndex][0], col[mapIndex][1], col[mapIndex][2], 255);
        }
        else
        {
            SDL_SetRenderDrawColor(r, 70, 80, 100, 255);
        }
        SDL_RenderFillRect(r, &d);
    }
}

/* 简单按钮:填充 + 边框 + 居中文字,selected 时高亮。 */
static void draw_button(SDL_Renderer *r, int x, int y, int w, int h,
                        const char *label, TTF_Font *font, SDL_Color textCol, bool selected)
{
    SDL_Rect box = {x, y, w, h};
    int tw = 0;
    int th = 0;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, selected ? 40 : 24, selected ? 74 : 40, selected ? 122 : 84, 235);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, selected ? 255 : 127, selected ? 210 : 151, selected ? 90 : 199, 255);
    SDL_RenderDrawRect(r, &box);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    if (font != NULL && label != NULL)
    {
        TTF_SizeUTF8(font, label, &tw, &th);
    }
    draw_text(r, font, label, x + (w - tw) / 2, y + (h - th) / 2, textCol);
}

/* 赛道完整缩略图(斜放,起点左下→终点右上),关卡选择中间展示。
 * SDL 无多边形,用沿对角线垂直方向偏移多条平行折线凑出路面带宽。 */
static void draw_track_thumbnail(SDL_Renderer *r, const RacingGame *game, int x0, int y0, int w, int h)
{
    enum { STEP = 10 };
    SDL_Point pts[ROAD_COUNT / STEP + 2];
    SDL_Point bp[ROAD_COUNT / STEP + 2];
    float xmin = 1e30f;
    float xmax = -1e30f;
    float xmid;
    float half;
    int inset;
    float sx, sy, fx, fy, dxs, dys, dlen, dnx, dny, pnx, pny, lat;
    int bandHalf;
    int n = 0;
    int i;

    for (i = 0; i < ROAD_COUNT; i++)
    {
        float xv = game->roads[i].x;
        if (xv < xmin) xmin = xv;
        if (xv > xmax) xmax = xv;
    }
    xmid = (xmin + xmax) * 0.5f;
    half = (xmax - xmin) * 0.5f;
    if (half < 1.0f) half = 1.0f;

    inset = (w < h ? w : h) / 10 + 4;
    sx = (float)(x0 + inset);
    sy = (float)(y0 + h - inset);
    fx = (float)(x0 + w - inset);
    fy = (float)(y0 + inset);
    dxs = fx - sx;
    dys = fy - sy;
    dlen = sqrtf(dxs * dxs + dys * dys);
    if (dlen < 1.0f) dlen = 1.0f;
    dnx = dxs / dlen;
    dny = dys / dlen;
    pnx = -dny;
    pny = dnx;
    lat = (w < h ? w : h) * 0.18f;
    bandHalf = (int)(game->roadWidth / (float)ROAD_WIDTH * 7.0f + 0.5f);
    if (bandHalf < 2) bandHalf = 2;
    if (bandHalf > 10) bandHalf = 10;

    for (i = 0; i < ROAD_COUNT; i += STEP)
    {
        float u = (float)i / (float)(ROAD_COUNT - 1);
        float vv = (game->roads[i].x - xmid) / half;
        float bx, by, cx, cy;
        if (vv < -1.0f) vv = -1.0f;
        if (vv > 1.0f) vv = 1.0f;
        bx = sx + u * dxs;
        by = sy + u * dys;
        cx = bx + vv * lat * pnx;
        cy = by + vv * lat * pny;
        pts[n].x = (int)cx;
        pts[n].y = (int)cy;
        n++;
    }

    SDL_SetRenderDrawColor(r, 75, 111, 214, 255);
    for (int o = -bandHalf; o <= bandHalf; o++)
    {
        for (int j = 0; j < n; j++)
        {
            bp[j].x = pts[j].x + (int)(o * pnx);
            bp[j].y = pts[j].y + (int)(o * pny);
        }
        SDL_RenderDrawLines(r, bp, n);
    }

    {
        SDL_Rect s = {(int)sx - 4, (int)sy - 4, 8, 8};
        SDL_Rect f = {(int)fx - 4, (int)fy - 4, 8, 8};
        SDL_SetRenderDrawColor(r, 51, 194, 77, 255);
        SDL_RenderFillRect(r, &s);
        SDL_SetRenderDrawColor(r, 224, 65, 60, 255);
        SDL_RenderFillRect(r, &f);
    }
}

/* 统一绘制菜单(主菜单/关卡选择/操作选择)/暂停/结束界面。 */
static void draw_mode_overlay(RacingSdlRenderer *view, const RacingGame *game)
{
    SDL_Renderer *r = view->renderer;
    TTF_Font *font = view->font;
    SDL_Color titleCol = {255, 220, 120, 255};
    SDL_Color body = {232, 238, 255, 255};
    SDL_Color hint = {175, 193, 230, 255};
    char buf[64];

    if (game->mode == RACING_MODE_PLAYING)
    {
        return;
    }

    if (game->mode == RACING_MODE_START)
    {
        /* 主菜单:标题「奶龙赛车」+ 副标题 + 关卡/操作两个按钮。 */
        draw_center_panel(r, 372, 252, (SDL_Color){13, 23, 48, 230}, (SDL_Color){127, 151, 199, 220});
        if (view->nailong != NULL)
        {
            SDL_Rect m = {WIN_WIDTH - 92, 28, 66, 66};
            SDL_RenderCopy(r, view->nailong, NULL, &m);
        }
        draw_text_centered(r, view->titleFont, "奶龙赛车", 38, titleCol);
        draw_text_centered(r, font, "NAILONG  RACING", 86, body);
        snprintf(buf, sizeof(buf), "操控 Control: %s",
                 game->menuControlMode == 1 ? "Gyro (JY60)" : "Original (Touch)");
        draw_text_centered(r, font, buf, 110, (SDL_Color){120, 230, 140, 255});
        draw_button(r, WIN_WIDTH / 2 - 132, 138, 264, 46, "1   关卡选择  Track", font, body, false);
        draw_button(r, WIN_WIDTH / 2 - 132, 198, 264, 46, "2   操作选择  Control", font, body, false);
        draw_text_centered(r, font, "1 Track       2 Control", WIN_HEIGHT - 34, hint);
        return;
    }

    if (game->mode == RACING_MODE_MAP_SELECT)
    {
        /* 关卡选择:< 缩略图 > + 难度等级。 */
        int tw = 244;
        int th = 176;
        int tx = (WIN_WIDTH - tw) / 2;
        int ty = 40;
        int sideY = ty + th / 2 - 22;
        SDL_Rect box;
        draw_center_panel(r, WIN_WIDTH - 24, WIN_HEIGHT - 24, (SDL_Color){13, 23, 48, 230}, (SDL_Color){127, 151, 199, 220});
        draw_text_centered(r, font, "关卡选择 — Select Track", 12, body);
        box = (SDL_Rect){tx - 4, ty - 4, tw + 8, th + 8};
        SDL_SetRenderDrawColor(r, 28, 42, 70, 255);
        SDL_RenderFillRect(r, &box);
        draw_track_thumbnail(r, game, tx, ty, tw, th);
        draw_button(r, 20, sideY, 56, 44, "<", font, body, true);
        draw_button(r, WIN_WIDTH - 20 - 56, sideY, 56, 44, ">", font, body, true);
        snprintf(buf, sizeof(buf), "%s   %s", racing_game_map_name(game->mapIndex),
                 racing_game_map_name_ascii(game->mapIndex));
        draw_text_centered(r, font, buf, ty + th + 12, body);
        draw_difficulty(r, WIN_WIDTH / 2, ty + th + 38, game->mapIndex);
        draw_text_centered(r, font, "<- -> Switch   Enter Start   Esc Back", WIN_HEIGHT - 24, hint);
        return;
    }

    if (game->mode == RACING_MODE_CONTROL_SELECT)
    {
        /* 操作选择:Original / Gyro / Test,当前高亮。 */
        const char *names[3] = {"Original  (Touch)", "Gyro  (JY60)", "Test  (JY60 Data)"};
        int k;
        draw_center_panel(r, 372, 268, (SDL_Color){13, 23, 48, 230}, (SDL_Color){127, 151, 199, 220});
        draw_text_centered(r, font, "操作选择 — Control", 16, body);
        for (k = 0; k < 3; k++)
        {
            draw_button(r, WIN_WIDTH / 2 - 142, 54 + k * 56, 284, 46, names[k], font, body,
                        k == game->menuControlMode);
        }
        draw_text_centered(r, font, "<- -> Switch   Enter Confirm   Esc Back", WIN_HEIGHT - 24, hint);
        return;
    }

    if (game->mode == RACING_MODE_PAUSED)
    {
        draw_center_panel(r, 300, 168, (SDL_Color){13, 23, 48, 225}, (SDL_Color){127, 151, 199, 220});
        draw_text_centered(r, view->titleFont, "暂停", 48, titleCol);
        draw_text_centered(r, font, "Enter to resume", 100, body);
        draw_text_centered(r, font, "R to restart", 122, hint);
        draw_text_centered(r, font, "M main menu", 144, (SDL_Color){255, 210, 60, 255});
        return;
    }

    if (game->mode == RACING_MODE_WIN)
    {
        snprintf(buf, sizeof(buf), "Finish in %ds", game->finalSeconds);
        draw_center_panel(r, 300, 168, (SDL_Color){13, 23, 48, 225}, (SDL_Color){127, 151, 199, 220});
        draw_text_centered(r, view->titleFont, buf, 48, titleCol);
        draw_text_centered(r, font, "Enter to restart", 100, body);
        draw_text_centered(r, font, "R to restart", 122, hint);
        draw_text_centered(r, font, "M main menu", 144, (SDL_Color){255, 210, 60, 255});
        return;
    }
}

/* 把世界坐标投到屏幕坐标，供赛道、旗门和道具复用。 */
static ProjectedPoint project_world_point(float x, float y, float z, int camX, int camY, int camZ, float angle)
{
    float sinAngle = sinf(angle);
    float cosAngle = cosf(angle);
    float tx = (x - camX) * cosAngle + (z - camZ) * sinAngle;
    float tz = -(x - camX) * sinAngle + (z - camZ) * cosAngle;
    float scale;
    ProjectedPoint projected;

    if (tz < 0.1f)
    {
        tz = 0.1f;
    }

    scale = 1.0f / tz;
    projected.x = (1.0f + scale * tx) * WIN_WIDTH / 2.0f;
    projected.y = (1.0f - scale * (y - camY)) * WIN_HEIGHT / 2.0f;
    return projected;
}

/* 把一个世界矩形拆成四个投影点，再送进 SDL 的几何接口。 */
static void draw_world_rect(SDL_Renderer *renderer, SDL_Color color, float x1, float y1, float x2, float y2, float z, int camX, int camY, int camZ, float angle)
{
    SDL_Vertex vertices[4];
    int indices[6] = {0, 1, 2, 0, 2, 3};
    ProjectedPoint a = project_world_point(x1, y1, z, camX, camY, camZ, angle);
    ProjectedPoint b = project_world_point(x2, y1, z, camX, camY, camZ, angle);
    ProjectedPoint c = project_world_point(x2, y2, z, camX, camY, camZ, angle);
    ProjectedPoint d = project_world_point(x1, y2, z, camX, camY, camZ, angle);

    vertices[0].position.x = a.x;
    vertices[0].position.y = a.y;
    vertices[1].position.x = b.x;
    vertices[1].position.y = b.y;
    vertices[2].position.x = c.x;
    vertices[2].position.y = c.y;
    vertices[3].position.x = d.x;
    vertices[3].position.y = d.y;

    for (int i = 0; i < 4; i++)
    {
        vertices[i].color = color;
        vertices[i].tex_coord.x = 0.0f;
        vertices[i].tex_coord.y = 0.0f;
    }

    SDL_RenderGeometry(renderer, NULL, vertices, 4, indices, 6);
}

/* 终点旗门和两侧装饰，宽度直接跟路面宽度对齐。 */
static void draw_finish_flag(SDL_Renderer *renderer, const Road *road, int camX, int camY, int camZ, float angle, float roadWidth)
{
    float z = road->z;
    /* 旗门再放宽一些，让它和赛道视觉宽度更接近。 */
    float halfWidth = roadWidth * 0.78f;
    float leftX = road->x - halfWidth;
    float rightX = road->x + halfWidth;
    float poleWidth = 140.0f;
    float poleTopY = road->y + 3700.0f;
    float flagTopY = road->y + 3560.0f;
    float flagBottomY = road->y + 2460.0f;
    float flagLeftX = leftX + poleWidth;
    float flagRightX = rightX - poleWidth;
    float cellWidth = (flagRightX - flagLeftX) / 6.0f;
    float cellHeight = (flagTopY - flagBottomY) / 3.0f;

    draw_world_rect(renderer, (SDL_Color){235, 45, 24, 255}, leftX, road->y, leftX + poleWidth, poleTopY, z, camX, camY, camZ, angle);
    draw_world_rect(renderer, (SDL_Color){235, 45, 24, 255}, rightX - poleWidth, road->y, rightX, poleTopY, z, camX, camY, camZ, angle);

    for (int row = 0; row < 3; row++)
    {
        for (int column = 0; column < 6; column++)
        {
            float x1 = flagLeftX + cellWidth * column;
            float x2 = flagLeftX + cellWidth * (column + 1);
            float y1 = flagTopY - cellHeight * row;
            float y2 = flagTopY - cellHeight * (row + 1);
            SDL_Color color = (row + column) % 2 == 0 ? (SDL_Color){255, 255, 255, 255} : (SDL_Color){0, 0, 0, 255};

            draw_world_rect(renderer, color, x1, y1, x2, y2, z, camX, camY, camZ, angle);
        }
    }

    draw_world_rect(renderer, (SDL_Color){247, 169, 25, 255}, leftX - 340.0f, road->y + 1300.0f, leftX + 480.0f, road->y + 2100.0f, z, camX, camY, camZ, angle);
    draw_world_rect(renderer, (SDL_Color){247, 169, 25, 255}, rightX - 480.0f, road->y + 1300.0f, rightX + 340.0f, road->y + 2100.0f, z, camX, camY, camZ, angle);
    draw_world_rect(renderer, (SDL_Color){71, 88, 151, 255}, leftX - 300.0f, road->y + 40.0f, leftX + 440.0f, road->y + 180.0f, z, camX, camY, camZ, angle);
    draw_world_rect(renderer, (SDL_Color){71, 88, 151, 255}, rightX - 440.0f, road->y + 40.0f, rightX + 300.0f, road->y + 180.0f, z, camX, camY, camZ, angle);
}

/* 山体和云层只做横向滚动，不参与赛道透视。 */
static void draw_texture_strip(SDL_Renderer *renderer, SDL_Texture *texture, int y, int h, int camX, float factor)
{
    int tw = 0;
    int th = 0;
    int offset;

    if (texture == NULL)
    {
        return;
    }

    SDL_QueryTexture(texture, NULL, NULL, &tw, &th);
    if (tw <= 0 || th <= 0)
    {
        return;
    }

    offset = (int)(camX * factor) % tw;
    if (offset < 0)
    {
        offset += tw;
    }

    for (int x = -offset; x < WIN_WIDTH; x += tw)
    {
        SDL_Rect dst = {x, y, tw, h};
        SDL_RenderCopy(renderer, texture, NULL, &dst);
    }
}

/* 路段投影结果包含位置、宽度和深度，后面画草地/路面都要用。 */
static ProjectedRoad project_road_desktop(const Road *road, int camX, int camY, int camZ, float angle, float roadWidth)
{
    float sinAngle = sinf(angle);
    float cosAngle = cosf(angle);
    float tx = (road->x - camX) * cosAngle + (road->z - camZ) * sinAngle;
    float tz = -(road->x - camX) * sinAngle + (road->z - camZ) * cosAngle;
    float scale;
    ProjectedRoad projected;

    if (tz < 0.1f)
    {
        tz = 0.1f;
    }

    scale = 1.0f / tz;
    projected.x = (1.0f + scale * tx) * WIN_WIDTH / 2.0f;
    projected.y = (1.0f - scale * (road->y - camY)) * WIN_HEIGHT / 2.0f;
    projected.w = scale * roadWidth * WIN_WIDTH / 2.0f;
    projected.tz = tz;
    return projected;
}

/* 简单的浮点夹取，避免四边形越界后把整屏几何拉爆。 */
static float clamp_float(float value, float min, float max)
{
    if (value < min)
    {
        return min;
    }
    if (value > max)
    {
        return max;
    }
    return value;
}

/* 透视四边形是 SDL 这边最核心的绘制单元，草地和路面都靠它。 */
static void draw_trapezoid(SDL_Renderer *renderer, ProjectedRoad farRoad, ProjectedRoad nearRoad, float widthScale, float bankAngle, SDL_Color color)
{
    float minX = -WIN_WIDTH;
    float maxX = WIN_WIDTH * 2.0f;
    float cosBank = cosf(bankAngle);
    float sinBank = sinf(bankAngle);
    SDL_Vertex vertices[4];
    int indices[6] = {0, 1, 2, 0, 2, 3};

    vertices[0].position.x = clamp_float(farRoad.x - farRoad.w * widthScale * cosBank, minX, maxX);
    vertices[0].position.y = clamp_float(farRoad.y - farRoad.w * widthScale * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);
    vertices[1].position.x = clamp_float(farRoad.x + farRoad.w * widthScale * cosBank, minX, maxX);
    vertices[1].position.y = clamp_float(farRoad.y + farRoad.w * widthScale * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);
    vertices[2].position.x = clamp_float(nearRoad.x + nearRoad.w * widthScale * cosBank, minX, maxX);
    vertices[2].position.y = clamp_float(nearRoad.y + nearRoad.w * widthScale * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);
    vertices[3].position.x = clamp_float(nearRoad.x - nearRoad.w * widthScale * cosBank, minX, maxX);
    vertices[3].position.y = clamp_float(nearRoad.y - nearRoad.w * widthScale * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);

    for (int i = 0; i < 4; i++)
    {
        vertices[i].color = color;
        vertices[i].tex_coord.x = 0.0f;
        vertices[i].tex_coord.y = 0.0f;
    }

    SDL_RenderGeometry(renderer, NULL, vertices, 4, indices, 6);
}

/* 草地拆成左右两条带子，避免转弯时把路心也盖住。 */
static void draw_ground_band(SDL_Renderer *renderer, ProjectedRoad farRoad, ProjectedRoad nearRoad, float bankAngle, bool darkGrass)
{
    float cosBank = cosf(bankAngle);
    float sinBank = sinf(bankAngle);
    SDL_Vertex vertices[4];
    int indices[6] = {0, 1, 2, 0, 2, 3};

    vertices[0].position.x = 0.0f;
    vertices[0].position.y = clamp_float(farRoad.y - farRoad.w * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);
    vertices[1].position.x = clamp_float(farRoad.x - farRoad.w * cosBank, -WIN_WIDTH, WIN_WIDTH * 2.0f);
    vertices[1].position.y = clamp_float(farRoad.y - farRoad.w * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);
    vertices[2].position.x = clamp_float(nearRoad.x - nearRoad.w * cosBank, -WIN_WIDTH, WIN_WIDTH * 2.0f);
    vertices[2].position.y = clamp_float(nearRoad.y - nearRoad.w * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);
    vertices[3].position.x = 0.0f;
    vertices[3].position.y = clamp_float(nearRoad.y - nearRoad.w * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);

    for (int i = 0; i < 4; i++)
    {
        vertices[i].color = darkGrass ? (SDL_Color){46, 51, 56, 255} : (SDL_Color){0, 199, 0, 255};
        vertices[i].tex_coord.x = 0.0f;
        vertices[i].tex_coord.y = 0.0f;
    }
    SDL_RenderGeometry(renderer, NULL, vertices, 4, indices, 6);

    vertices[0].position.x = clamp_float(farRoad.x + farRoad.w * cosBank, -WIN_WIDTH, WIN_WIDTH * 2.0f);
    vertices[0].position.y = clamp_float(farRoad.y + farRoad.w * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);
    vertices[1].position.x = (float)WIN_WIDTH;
    vertices[1].position.y = clamp_float(farRoad.y + farRoad.w * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);
    vertices[2].position.x = (float)WIN_WIDTH;
    vertices[2].position.y = clamp_float(nearRoad.y + nearRoad.w * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);
    vertices[3].position.x = clamp_float(nearRoad.x + nearRoad.w * cosBank, -WIN_WIDTH, WIN_WIDTH * 2.0f);
    vertices[3].position.y = clamp_float(nearRoad.y + nearRoad.w * sinBank, -WIN_HEIGHT, WIN_HEIGHT * 2.0f);

    for (int i = 0; i < 4; i++)
    {
        vertices[i].color = darkGrass ? (SDL_Color){46, 51, 56, 255} : (SDL_Color){0, 199, 0, 255};
        vertices[i].tex_coord.x = 0.0f;
        vertices[i].tex_coord.y = 0.0f;
    }
    SDL_RenderGeometry(renderer, NULL, vertices, 4, indices, 6);
}

/* 计算某段离起点的距离，决定它是否落在视距内。 */
static int segment_distance_from_start(int start, int segment)
{
    int distance = segment - start;
    if (distance < 0)
    {
        distance += ROAD_COUNT;
    }
    return distance;
}

/* 只渲染前方视距内的奶龙，避免背后或太远的道具乱入。 */
static bool is_collectible_visible(const RacingGame *game, int collectibleIndex)
{
    int offset = segment_distance_from_start(racing_game_start_segment(game), game->collectiblePositions[collectibleIndex]);
    return offset > 0 && offset <= VIEW_DISTANCE;
}

/* 赛道按远到近依次画，草地先铺，再画边缘和路面。 */
static void render_track(SDL_Renderer *renderer, const RacingGame *game)
{
    int start = racing_game_start_segment(game);
    float bankAngle = game->turnLeft ? -0.1f : game->turnRight ? 0.1f
                                                               : 0.0f;

    for (int offset = VIEW_DISTANCE; offset > 0; offset--)
    {
        int farSegment = start + offset;
        int nearSegment = farSegment - 1;
        int farIndex = farSegment % ROAD_COUNT;
        int nearIndex = nearSegment % ROAD_COUNT;
        int farCamZ = game->camZ - (farSegment >= ROAD_COUNT ? TRACK_LENGTH : 0);
        int nearCamZ = game->camZ - (nearSegment >= ROAD_COUNT ? TRACK_LENGTH : 0);
        ProjectedRoad farRoad = project_road_desktop(&game->roads[farIndex], game->camX, game->camY, farCamZ, game->angle, game->roadWidth);
        ProjectedRoad nearRoad = project_road_desktop(&game->roads[nearIndex], game->camX, game->camY, nearCamZ, game->angle, game->roadWidth);
        SDL_Color edge = farSegment % 2 ? (SDL_Color){20, 20, 20, 255} : (SDL_Color){245, 245, 245, 255};
        SDL_Color road = farSegment % 2 ? (SDL_Color){105, 105, 105, 255} : (SDL_Color){101, 101, 101, 255};

        if (farRoad.tz <= 0.1f || nearRoad.tz <= 0.1f ||
            (farRoad.y >= WIN_HEIGHT && nearRoad.y >= WIN_HEIGHT) ||
            (farRoad.y < -WIN_HEIGHT && nearRoad.y < -WIN_HEIGHT))
        {
            continue;
        }

        draw_ground_band(renderer, farRoad, nearRoad, bankAngle, game->mapIndex == 1);
        draw_trapezoid(renderer, farRoad, nearRoad, 1.3f, bankAngle, edge);
        draw_trapezoid(renderer, farRoad, nearRoad, 1.0f, bankAngle, road);

        if (farSegment % ROAD_COUNT == ROAD_COUNT - 1)
        {
            draw_finish_flag(renderer, &game->roads[ROAD_COUNT - 1], game->camX, game->camY, game->camZ, game->angle, game->roadWidth);
        }
    }
}

/* 奶龙用贴图渲染，投影尺寸由游戏逻辑预先算好。 */
static void render_collectibles(SDL_Renderer *renderer, const RacingGame *game, SDL_Texture *texture)
{
    for (int i = 0; i < COLLECTIBLE_COUNT; i++)
    {
        const Nailong *item = &game->collectibles[i];
        int w = (int)fabsf(item->p[1].X - item->p[0].X);
        int h = (int)fabsf(item->p[0].Y - item->p[2].Y);
        SDL_Rect dst = {
            (int)item->p[0].X,
            (int)item->p[2].Y,
            w > 8 ? w : 24,
            h > 8 ? h : 28};

        if (item->eaten || !is_collectible_visible(game, i) ||
            item->p[0].tz <= 0.1f || item->p[1].tz <= 0.1f ||
            dst.x >= WIN_WIDTH || dst.y >= WIN_HEIGHT || dst.x + dst.w < 0 || dst.y + dst.h < 0)
        {
            continue;
        }

        if (texture != NULL)
        {
            SDL_RenderCopy(renderer, texture, NULL, &dst);
        }
        else
        {
            SDL_SetRenderDrawColor(renderer, 255, 212, 71, 255);
            SDL_RenderFillRect(renderer, &dst);
        }
    }
}

/* 底部车体是固定前景，不参与赛道投影。 */
static void render_car(SDL_Renderer *renderer, SDL_Texture *texture)
{
    SDL_Rect dst = {0, WIN_HEIGHT - 112, WIN_WIDTH, 112};

    if (texture != NULL)
    {
        SDL_RenderCopy(renderer, texture, NULL, &dst);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, 246, 208, 77, 255);
        SDL_RenderFillRect(renderer, &dst);
    }
}

/* 奶龙跳脸层，只有命中后的短暂时间才会显示。 */
static void render_hit_overlay(SDL_Renderer *renderer, SDL_Texture *texture, int hitFrames)
{
    SDL_Rect dst;

    if (hitFrames <= 0) {
        return;
    }

    dst.x = 0;
    dst.y = 0;
    dst.w = WIN_WIDTH;
    dst.h = WIN_HEIGHT;

    if (texture != NULL) {
        SDL_RenderCopy(renderer, texture, NULL, &dst);
    }
}

/* 初始化 SDL 资源：图片、字体、纹理都在这里集中加载。 */
RacingSdlRenderer *racing_sdl_renderer_create(SDL_Renderer *renderer)
{
    RacingSdlRenderer *view = (RacingSdlRenderer *)calloc(1, sizeof(*view));
    if (view == NULL || renderer == NULL)
    {
        free(view);
        return NULL;
    }

    view->renderer = renderer;
    view->imageReady = (IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) != 0;
    if (!view->imageReady)
    {
        fprintf(stderr, "IMG_Init PNG failed: %s\n", IMG_GetError());
    }

    view->ttfReady = TTF_Init() == 0;
    if (!view->ttfReady)
    {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
    }

    view->clouds = load_texture(renderer, ASSET_DIR "/images/cloud_layer.png");
    view->mountainFar = load_texture(renderer, ASSET_DIR "/images/mountain_far.png");
    view->mountainNear = load_texture(renderer, ASSET_DIR "/images/mountain_near.png");
    view->car = load_texture(renderer, ASSET_DIR "/images/car.png");
    view->nailong = load_texture(renderer, ASSET_DIR "/images/nailong.png");
    view->nailongHead = load_texture(renderer, ASSET_DIR "/images/nailong_head.png");
    view->nailongBupt = load_texture(renderer, ASSET_DIR "/images/nailong_bupt.png");
    view->bgBupt = load_texture(renderer, ASSET_DIR "/images/bg_bupt.jpg");
    if (view->ttfReady)
    {
        /* CJK 字体（含中文，如地图名“一路邮你”）。逐个尝试并记录到底用了哪个。 */
        view->font = TTF_OpenFont(ASSET_DIR "/fonts/noto.ttc", 18);
        fprintf(stderr, "font noto.ttc: %s\n", view->font ? "OK" : TTF_GetError());
        if (view->font == NULL)
        {
            view->font = TTF_OpenFont(ASSET_DIR "/fonts/cjk.ttf", 18);
            fprintf(stderr, "font cjk.ttf: %s\n", view->font ? "OK" : TTF_GetError());
        }
        if (view->font == NULL)
        {
            view->font = TTF_OpenFont(ASSET_DIR "/fonts/Arial.ttf", 18);
            fprintf(stderr, "font Arial.ttf: %s\n", view->font ? "OK" : TTF_GetError());
        }
        /* 大号标题字体（同字体，36px）。 */
        view->titleFont = TTF_OpenFont(ASSET_DIR "/fonts/noto.ttc", 36);
        if (view->titleFont == NULL)
        {
            view->titleFont = TTF_OpenFont(ASSET_DIR "/fonts/cjk.ttf", 36);
        }
        if (view->titleFont == NULL)
        {
            view->titleFont = TTF_OpenFont(ASSET_DIR "/fonts/Arial.ttf", 36);
        }
    }

    printf("Racing SDL assets loaded from %s.\n", ASSET_DIR);
    return view;
}

/* 统一销毁纹理和字体，避免资源释放散落到主循环里。 */
void racing_sdl_renderer_delete(RacingSdlRenderer *view)
{
    if (view == NULL)
    {
        return;
    }

    destroy_texture(&view->clouds);
    destroy_texture(&view->mountainFar);
    destroy_texture(&view->mountainNear);
    destroy_texture(&view->car);
    destroy_texture(&view->nailong);
    destroy_texture(&view->nailongHead);
    destroy_texture(&view->nailongBupt);
    destroy_texture(&view->bgBupt);
    if (view->font != NULL)
    {
        TTF_CloseFont(view->font);
        view->font = NULL;
    }
    if (view->titleFont != NULL)
    {
        TTF_CloseFont(view->titleFont);
        view->titleFont = NULL;
    }

    if (view->ttfReady)
    {
        TTF_Quit();
    }

    if (view->imageReady)
    {
        IMG_Quit();
    }

    free(view);
}

/* 左上角小地图:以当前 camZ 为中心的局部滚动窗口——前方多看、后方少看,路面
 * 随前进向上滚动,黄点标车当前位置。路宽按当前 roadWidth 画成一条带(越难
 * 越窄)。尺寸约为屏幕的 1/4 × 1/4。 */
static void draw_minimap(SDL_Renderer *renderer, const RacingGame *game)
{
    enum
    {
        W = WIN_WIDTH / 4,
        H = WIN_HEIGHT / 4,
        X0 = 4,
        Y0 = 4,
        AHEAD = 60,
        BEHIND = 12,
        WIN = AHEAD + BEHIND + 1,
        LSTEP = 2
    };
    SDL_Point pts[WIN];
    SDL_Point bp[WIN];
    int seg0 = (int)(game->camZ / SEG_LENGTH);
    int cx = X0 + W / 2;
    int m = (W < H ? W : H) / 10 + 2;
    int yCur = Y0 + (int)(H * 0.72f);
    int topY = Y0 + m;
    int botY = Y0 + H - m;
    float vsA = (float)(yCur - topY) / (float)AHEAD;
    float vsB = (float)(botY - yCur) / (float)BEHIND;
    float vs = (vsA < vsB ? vsA : vsB);
    float halfw = (float)(W / 2 - m);
    int bandHalf;
    float xmin = 1e30f;
    float xmax = -1e30f;
    float xmid;
    float half;
    int n = 0;
    int d;

    bandHalf = (int)(game->roadWidth / (float)ROAD_WIDTH * 6.0f + 0.5f);
    if (bandHalf < 2)
    {
        bandHalf = 2;
    }
    if (bandHalf > 8)
    {
        bandHalf = 8;
    }

    if (seg0 < 0)
    {
        seg0 = 0;
    }
    if (seg0 >= ROAD_COUNT)
    {
        seg0 = ROAD_COUNT - 1;
    }

    /* 求窗口横向范围,自适应铺满宽度。 */
    for (d = 0; d < WIN; d++)
    {
        int seg = ((seg0 - BEHIND + d) % ROAD_COUNT + ROAD_COUNT) % ROAD_COUNT;
        float xv = game->roads[seg].x;
        if (xv < xmin)
        {
            xmin = xv;
        }
        if (xv > xmax)
        {
            xmax = xv;
        }
    }
    xmid = (xmin + xmax) * 0.5f;
    half = (xmax - xmin) * 0.5f;
    if (half < 1.0f)
    {
        half = 1.0f;
    }

    /* 半透明底板 + 边框。 */
    {
        SDL_Rect box = {X0, Y0, W, H};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 13, 23, 48, 170);
        SDL_RenderFillRect(renderer, &box);
        SDL_SetRenderDrawColor(renderer, 127, 151, 199, 230);
        SDL_RenderDrawRect(renderer, &box);
    }

    /* 中心线采样:横向 x→屏幕水平,段偏移 dd→垂直(前方在上)。 */
    for (d = 0; d < WIN; d += LSTEP)
    {
        int seg = ((seg0 - BEHIND + d) % ROAD_COUNT + ROAD_COUNT) % ROAD_COUNT;
        float vv = (game->roads[seg].x - xmid) / half;
        int dd = d - BEHIND;                 /* 负=后方,正=前方 */
        pts[n].x = (int)((float)cx + vv * halfw);
        pts[n].y = (int)((float)yCur - dd * vs);
        n++;
    }

    /* 路带:横向偏移多画几条平行折线凑出宽度。 */
    SDL_SetRenderDrawColor(renderer, 75, 111, 214, 255);
    for (int o = -bandHalf; o <= bandHalf; o++)
    {
        for (int j = 0; j < n; j++)
        {
            bp[j].x = pts[j].x + o;
            bp[j].y = pts[j].y;
        }
        SDL_RenderDrawLines(renderer, bp, n);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    /* 当前位置黄点(dd=0,即 yCur 处)。 */
    {
        int seg = seg0 % ROAD_COUNT;
        float vv = (game->roads[seg].x - xmid) / half;
        int bx = (int)((float)cx + vv * halfw);
        SDL_Rect dot = {bx - 3, yCur - 3, 6, 6};
        SDL_SetRenderDrawColor(renderer, 255, 210, 60, 255);
        SDL_RenderFillRect(renderer, &dot);
    }
}

/* SDL 桌面版的完整一帧：背景、赛道、道具、车体、HUD 和跳脸。 */
void racing_sdl_render(RacingSdlRenderer *view, const RacingGame *game)
{
    SDL_Renderer *renderer;

    if (view == NULL || game == NULL)
    {
        return;
    }

    renderer = view->renderer;

    /* 背景：地图2(中等)用北邮校门照(带视差，随 camX 缓慢横滚)，其余用天空 + 云/山条带。 */
    if (game->mapIndex == 1 && view->bgBupt != NULL)
    {
        SDL_SetRenderDrawColor(renderer, 40, 44, 50, 255);
        SDL_RenderClear(renderer);
        draw_texture_strip(renderer, view->bgBupt, 0, WIN_HEIGHT, game->camX, 0.012f);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, 95, 184, 232, 255);
        SDL_RenderClear(renderer);
        draw_texture_strip(renderer, view->clouds, 0, WIN_HEIGHT, game->camX, 0.01f);
        draw_texture_strip(renderer, view->mountainFar, 18, WIN_HEIGHT, game->camX, 0.018f);
        draw_texture_strip(renderer, view->mountainNear, 32, WIN_HEIGHT, game->camX, 0.028f);
    }

    render_track(renderer, game);

    /* 收集物：地图“一路邮你”用北邮校徽，其余用奶龙。 */
    {
        SDL_Texture *collectible = (game->mapIndex == 1 && view->nailongBupt != NULL)
                                       ? view->nailongBupt
                                       : view->nailong;
        render_collectibles(renderer, game, collectible);
    }
    render_car(renderer, view->car);

    if (view->font != NULL)
    {
        char text[64];
        SDL_Color red = {255, 32, 32, 255};
        SDL_Color cyan = {0, 255, 255, 255};

        /* HUD 文字挪到顶部居中，给左上角缩略图让位。 */
        snprintf(text, sizeof(text), "Lap %d/3", game->lap);
        draw_text(renderer, view->font, text, WIN_WIDTH / 2 - 28, 6, red);
        snprintf(text, sizeof(text), "%ds", game->elapsedMs / 1000);
        draw_text(renderer, view->font, text, WIN_WIDTH / 2 - 14, 28, cyan);
        draw_energy(renderer, game->energy);
        if (game->boosting)
        {
            draw_text(renderer, view->font, "BOOST", WIN_WIDTH - 76, 6,
                      (SDL_Color){255, 136, 0, 255});
        }

        if (game->mode == RACING_MODE_WIN)
        {
            snprintf(text, sizeof(text), "Win %ds", game->finalSeconds);
            draw_text(renderer, view->font, text, WIN_WIDTH / 2 - 30, 50, (SDL_Color){255, 214, 71, 255});
        }
    }

    /* 左上角缩略图：赛道轮廓 + 当前位置，游戏中显示。 */
    if (game->mode == RACING_MODE_PLAYING || game->mode == RACING_MODE_PAUSED)
    {
        draw_minimap(renderer, game);
    }

    draw_mode_overlay(view, game);

    /* 跳脸：地图“一路邮你”用校徽，其余用奶龙头。 */
    {
        SDL_Texture *face = (game->mapIndex == 1 && view->nailongBupt != NULL)
                                ? view->nailongBupt
                                : view->nailongHead;
        render_hit_overlay(renderer, face, game->hitFrames);
    }
    SDL_RenderPresent(renderer);
}
