#include "render_lvgl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <lvgl/src/draw/lv_draw_image.h>
#include <lvgl/src/draw/lv_draw_triangle.h>

#ifndef ASSET_DIR
#define ASSET_DIR "assets"
#endif

#ifdef LV_IMG_CF_TRUE_COLOR
#define RACING_LV_CANVAS_FORMAT LV_IMG_CF_TRUE_COLOR
#else
#define RACING_LV_CANVAS_FORMAT LV_COLOR_FORMAT_NATIVE
#endif

typedef struct ProjectionContext {
    int camX;
    int camY;
    int camZ;
    float sinAngle;
    float cosAngle;
} ProjectionContext;

struct RacingLvglView {
    RacingGame *game;
    RacingInput input;
    lv_obj_t *canvas;
    lv_timer_t *timer;
    lv_color_t *buffer;
    bool ownsBuffer;
    uint32_t lastTick;
};

static ProjectionContext make_projection_context(int camX, int camY, int camZ, float angle)
{
    ProjectionContext context;
    context.camX = camX;
    context.camY = camY;
    context.camZ = camZ;
    context.sinAngle = sinf(angle);
    context.cosAngle = cosf(angle);
    return context;
}

static void project_road(Road *road, const ProjectionContext *context)
{
    road->tx = (road->x - context->camX) * context->cosAngle + (road->z - context->camZ) * context->sinAngle;
    road->tz = -(road->x - context->camX) * context->sinAngle + (road->z - context->camZ) * context->cosAngle;

    if (road->tz < 0.1f) {
        road->tz = 0.1f;
    }

    road->scale = 1.0f / road->tz;
    road->X = (1.0f + road->scale * road->tx) * WIN_WIDTH / 2.0f;
    road->Y = (1.0f - road->scale * (road->y - context->camY)) * WIN_HEIGHT / 2.0f;
    road->W = road->scale * ROAD_WIDTH * WIN_WIDTH / 2.0f;
}

/* 简单夹取坐标，防止越界区域把绘制面积拉飞。 */
static lv_coord_t clamp_coord(lv_coord_t value, lv_coord_t min, lv_coord_t max)
{
    if (value < min) {
        return min;
    }

    if (value > max) {
        return max;
    }

    return value;
}

/* LVGL 的文字绘制封装，HUD 和提示信息都走这里。 */
static void draw_text(lv_layer_t *layer, const char *text, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_draw_label_dsc_t dsc;
    lv_area_t area;

    lv_draw_label_dsc_init(&dsc);
    dsc.color = color;
    dsc.text = text;
    area.x1 = x;
    area.y1 = y;
    area.x2 = WIN_WIDTH - 1;
    area.y2 = y + 48;
    lv_draw_label(layer, &dsc, &area);
}

/* 中央状态面板，开始/暂停/结束页统一用这一块。 */
static void draw_center_panel(lv_layer_t *layer, int width, int height, lv_color_t fill, lv_color_t border)
{
    lv_draw_rect_dsc_t dsc;
    lv_area_t area;

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = fill;
    dsc.bg_opa = LV_OPA_80;
    dsc.border_color = border;
    dsc.border_opa = LV_OPA_COVER;
    dsc.border_width = 1;
    dsc.radius = 4;
    area.x1 = (WIN_WIDTH - width) / 2;
    area.y1 = (WIN_HEIGHT - height) / 2;
    area.x2 = area.x1 + width - 1;
    area.y2 = area.y1 + height - 1;
    lv_draw_rect(layer, &dsc, &area);
}

/* 开始 / 暂停 / 结束时的操作提示，和 SDL 保持同一套文案。 */
static void draw_mode_overlay(lv_layer_t *layer, const RacingGame *game)
{
    char text[80];

    if (game->mode == RACING_MODE_PLAYING) {
        return;
    }

    draw_center_panel(layer, 280, 156, lv_color_hex(0x0d1730), lv_color_hex(0x7f97c7));

    if (game->mode == RACING_MODE_START) {
        draw_text(layer, "Racing", WIN_WIDTH / 2 - 54, 60, lv_color_white());
        draw_text(layer, "Enter to start", WIN_WIDTH / 2 - 74, 100, lv_color_hex(0xe6ecff));
        draw_text(layer, "R to restart", WIN_WIDTH / 2 - 64, 124, lv_color_hex(0xadbfe6));
        draw_text(layer, "P / Esc pause", WIN_WIDTH / 2 - 68, 148, lv_color_hex(0xadbfe6));
    } else if (game->mode == RACING_MODE_PAUSED) {
        draw_text(layer, "Paused", WIN_WIDTH / 2 - 42, 60, lv_color_white());
        draw_text(layer, "Enter to resume", WIN_WIDTH / 2 - 76, 100, lv_color_hex(0xe6ecff));
        draw_text(layer, "R to restart", WIN_WIDTH / 2 - 64, 124, lv_color_hex(0xadbfe6));
    } else if (game->mode == RACING_MODE_WIN) {
        snprintf(text, sizeof(text), "Finish in %ds", game->finalSeconds);
        draw_text(layer, text, WIN_WIDTH / 2 - 70, 60, lv_color_white());
        draw_text(layer, "Enter to restart", WIN_WIDTH / 2 - 80, 100, lv_color_hex(0xe6ecff));
        draw_text(layer, "R to restart", WIN_WIDTH / 2 - 64, 124, lv_color_hex(0xadbfe6));
    }
}

/* 右下角能量条，结构和 SDL 保持一致，方便移植对照。 */
static void draw_energy(lv_layer_t *layer, int energy)
{
    enum { BAR_COUNT = 10, BAR_W = 10, BAR_H = 15, BAR_GAP = 3 };
    int bars = (energy + 99) / 100;
    int panelW = BAR_W + 12;
    int panelH = BAR_COUNT * BAR_H + (BAR_COUNT - 1) * BAR_GAP + 12;
    int panelX = WIN_WIDTH - panelW - 10;
    int panelY = WIN_HEIGHT - panelH - 10;
    lv_draw_rect_dsc_t panel;
    lv_draw_rect_dsc_t slot;
    lv_area_t panelArea;
    lv_area_t barArea;

    if (bars < 0) {
        bars = 0;
    }
    if (bars > BAR_COUNT) {
        bars = BAR_COUNT;
    }

    lv_draw_rect_dsc_init(&panel);
    panel.bg_color = lv_color_hex(0x0d1730);
    panel.bg_opa = LV_OPA_80;
    panel.border_color = lv_color_hex(0x7f97c7);
    panel.border_opa = LV_OPA_70;
    panel.border_width = 1;
    panel.radius = 2;
    panelArea.x1 = panelX;
    panelArea.y1 = panelY;
    panelArea.x2 = panelX + panelW - 1;
    panelArea.y2 = panelY + panelH - 1;
    lv_draw_rect(layer, &panel, &panelArea);

    lv_draw_rect_dsc_init(&slot);
    slot.border_width = 0;
    slot.radius = 1;

    lv_draw_rect_dsc_t dsc;

    slot.bg_color = lv_color_hex(0x17305a);
    slot.bg_opa = LV_OPA_COVER;
    for (int i = 0; i < BAR_COUNT; i++) {
        int y = panelY + 6 + (BAR_COUNT - 1 - i) * (BAR_H + BAR_GAP);
        barArea.x1 = panelX + 6;
        barArea.y1 = y;
        barArea.x2 = barArea.x1 + BAR_W - 1;
        barArea.y2 = y + BAR_H - 1;
        lv_draw_rect(layer, &slot, &barArea);
    }

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(0x1d63ff);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = 1;

    for (int i = 0; i < bars; i++) {
        int y = panelY + 6 + (BAR_COUNT - 1 - i) * (BAR_H + BAR_GAP);
        barArea.x1 = panelX + 6;
        barArea.y1 = y;
        barArea.x2 = barArea.x1 + BAR_W - 1;
        barArea.y2 = y + BAR_H - 1;
        lv_draw_rect(layer, &dsc, &barArea);
    }
}

/* 矩形裁剪后直接填充，避免超出可视区域的像素浪费。 */
static void draw_rect_clipped(lv_layer_t *layer, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2, lv_color_t color)
{
    lv_draw_rect_dsc_t dsc;
    lv_area_t area;

    if (x2 < x1) {
        lv_coord_t swap = x1;
        x1 = x2;
        x2 = swap;
    }

    if (y2 < y1) {
        lv_coord_t swap = y1;
        y1 = y2;
        y2 = swap;
    }

    if (x2 < 0 || x1 >= WIN_WIDTH || y2 < 0 || y1 >= WIN_HEIGHT) {
        return;
    }

    area.x1 = clamp_coord(x1, 0, WIN_WIDTH - 1);
    area.x2 = clamp_coord(x2, 0, WIN_WIDTH - 1);
    area.y1 = clamp_coord(y1, 0, WIN_HEIGHT - 1);
    area.y2 = clamp_coord(y2, 0, WIN_HEIGHT - 1);

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    lv_draw_rect(layer, &dsc, &area);
}

/* 路面和草地都用同一套透视带来画，保持转弯时的倾斜一致。 */
static void draw_road_band(lv_layer_t *layer, const Road *farRoad, const Road *nearRoad, float widthScale, float bankAngle, lv_color_t color);
static void draw_quad(lv_layer_t *layer, lv_point_precise_t p0, lv_point_precise_t p1, lv_point_precise_t p2, lv_point_precise_t p3, lv_color_t color)
{
    lv_draw_triangle_dsc_t tri;

    lv_draw_triangle_dsc_init(&tri);
    tri.opa = LV_OPA_COVER;
    tri.color = color;
    tri.p[0] = p0;
    tri.p[1] = p1;
    tri.p[2] = p2;
    lv_draw_triangle(layer, &tri);

    tri.p[0] = p0;
    tri.p[1] = p2;
    tri.p[2] = p3;
    lv_draw_triangle(layer, &tri);
}

/* 草地拆成左右两条，避免转弯时把中间路面盖掉。 */
static void draw_ground_band(lv_layer_t *layer, const Road *farRoad, const Road *nearRoad, float bankAngle)
{
    float cosBank = cosf(bankAngle);
    float sinBank = sinf(bankAngle);
    lv_point_precise_t farLeftOuter = {0.0f, farRoad->Y - farRoad->W * sinBank};
    lv_point_precise_t farLeftInner = {farRoad->X - farRoad->W * cosBank, farRoad->Y - farRoad->W * sinBank};
    lv_point_precise_t nearLeftInner = {nearRoad->X - nearRoad->W * cosBank, nearRoad->Y - nearRoad->W * sinBank};
    lv_point_precise_t nearLeftOuter = {0.0f, nearRoad->Y - nearRoad->W * sinBank};
    lv_point_precise_t farRightInner = {farRoad->X + farRoad->W * cosBank, farRoad->Y + farRoad->W * sinBank};
    lv_point_precise_t farRightOuter = {WIN_WIDTH, farRoad->Y + farRoad->W * sinBank};
    lv_point_precise_t nearRightOuter = {WIN_WIDTH, nearRoad->Y + nearRoad->W * sinBank};
    lv_point_precise_t nearRightInner = {nearRoad->X + nearRoad->W * cosBank, nearRoad->Y + nearRoad->W * sinBank};

    draw_quad(layer, farLeftOuter, farLeftInner, nearLeftInner, nearLeftOuter, lv_color_hex(0x00c700));
    draw_quad(layer, farRightInner, farRightOuter, nearRightOuter, nearRightInner, lv_color_hex(0x00c700));
}

/* 世界坐标投影到屏幕坐标，旗门和装饰块都用这套。 */
static void project_world_point(float x, float y, float z, const ProjectionContext *context, lv_point_precise_t *point)
{
    float tx = (x - context->camX) * context->cosAngle + (z - context->camZ) * context->sinAngle;
    float tz = -(x - context->camX) * context->sinAngle + (z - context->camZ) * context->cosAngle;
    float scale;

    if (tz < 0.1f) {
        tz = 0.1f;
    }

    scale = 1.0f / tz;
    point->x = (1.0f + scale * tx) * WIN_WIDTH / 2.0f;
    point->y = (1.0f - scale * (y - context->camY)) * WIN_HEIGHT / 2.0f;
}

/* 世界中的长方体拆成两个三角形，给 LVGL 画实心面。 */
static void draw_world_rect(lv_layer_t *layer, lv_color_t color, float x1, float y1, float x2, float y2, float z, const ProjectionContext *context)
{
    lv_draw_triangle_dsc_t tri;
    lv_point_precise_t a;
    lv_point_precise_t b;
    lv_point_precise_t c;
    lv_point_precise_t d;

    project_world_point(x1, y1, z, context, &a);
    project_world_point(x2, y1, z, context, &b);
    project_world_point(x2, y2, z, context, &c);
    project_world_point(x1, y2, z, context, &d);

    lv_draw_triangle_dsc_init(&tri);
    tri.opa = LV_OPA_COVER;
    tri.color = color;
    tri.p[0] = a;
    tri.p[1] = b;
    tri.p[2] = c;
    lv_draw_triangle(layer, &tri);

    tri.p[0] = a;
    tri.p[1] = c;
    tri.p[2] = d;
    lv_draw_triangle(layer, &tri);
}

/* 终点旗门宽度直接收齐到路宽，避免视觉上比赛道更宽。 */
static void draw_finish_flag(lv_layer_t *layer, const Road *road, const ProjectionContext *context)
{
    float z = road->z;
    /* 旗门略宽一点，和赛道视觉边缘对齐。 */
    float halfWidth = ROAD_WIDTH * 0.78f;
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

    draw_world_rect(layer, lv_color_hex(0xeb2d18), leftX, road->y, leftX + poleWidth, poleTopY, z, context);
    draw_world_rect(layer, lv_color_hex(0xeb2d18), rightX - poleWidth, road->y, rightX, poleTopY, z, context);

    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 6; column++) {
            float x1 = flagLeftX + cellWidth * column;
            float x2 = flagLeftX + cellWidth * (column + 1);
            float y1 = flagTopY - cellHeight * row;
            float y2 = flagTopY - cellHeight * (row + 1);
            lv_color_t color = (row + column) % 2 == 0 ? lv_color_white() : lv_color_black();

            draw_world_rect(layer, color, x1, y1, x2, y2, z, context);
        }
    }

    draw_world_rect(layer, lv_color_hex(0xf7a919), leftX - 340.0f, road->y + 1300.0f, leftX + 480.0f, road->y + 2100.0f, z, context);
    draw_world_rect(layer, lv_color_hex(0xf7a919), rightX - 480.0f, road->y + 1300.0f, rightX + 340.0f, road->y + 2100.0f, z, context);
    draw_world_rect(layer, lv_color_hex(0x475891), leftX - 300.0f, road->y + 40.0f, leftX + 440.0f, road->y + 180.0f, z, context);
    draw_world_rect(layer, lv_color_hex(0x475891), rightX - 440.0f, road->y + 40.0f, rightX + 300.0f, road->y + 180.0f, z, context);
}

/* 奶龙跳脸层：命中后把整张头图铺到最上层，持续一小段时间。 */
static void draw_hit_overlay(lv_layer_t *layer, int hitFrames)
{
    lv_draw_image_dsc_t dsc;
    lv_area_t area;

    if (hitFrames <= 0) {
        return;
    }

    lv_draw_image_dsc_init(&dsc);
    dsc.src = ASSET_DIR "/images/nailong_head.png";
    dsc.opa = LV_OPA_COVER;
    area.x1 = 0;
    area.y1 = 0;
    area.x2 = WIN_WIDTH - 1;
    area.y2 = WIN_HEIGHT - 1;
    lv_draw_image(layer, &dsc, &area);
}

/* 赛道按远到近分段绘制，草地先画，路面后压住。 */
static void draw_road_band(lv_layer_t *layer, const Road *farRoad, const Road *nearRoad, float widthScale, float bankAngle, lv_color_t color)
{
    float farW = farRoad->W * widthScale;
    float nearW = nearRoad->W * widthScale;
    float cosBank = cosf(bankAngle);
    float sinBank = sinf(bankAngle);
    lv_coord_t x1 = (lv_coord_t)fminf(farRoad->X - farW * cosBank, nearRoad->X - nearW * cosBank);
    lv_coord_t x2 = (lv_coord_t)fmaxf(farRoad->X + farW * cosBank, nearRoad->X + nearW * cosBank);
    lv_coord_t y1 = (lv_coord_t)(farRoad->Y - farW * sinBank);
    lv_coord_t y2 = (lv_coord_t)(nearRoad->Y + nearW * sinBank);

    draw_rect_clipped(layer, x1, y1, x2, y2, color);
}

/* LVGL 版的整帧绘制：背景、赛道、前景车体、HUD 和跳脸。 */
static void render_track(RacingLvglView *view, lv_layer_t *layer)
{
    RacingGame *game = view->game;
    int start = racing_game_start_segment(game);
    int farIndex = (start + VIEW_DISTANCE) % ROAD_COUNT;
    int nearIndex = farIndex > 0 ? farIndex - 1 : ROAD_COUNT - 1;
    ProjectionContext context = make_projection_context(game->camX, game->camY, game->camZ, game->angle);
    Road farRoad = game->roads[farIndex];
    int farWrap = start + VIEW_DISTANCE >= ROAD_COUNT ? TRACK_LENGTH : 0;
    lv_color_t roadA = lv_color_hex(0x696969);
    lv_color_t roadB = lv_color_hex(0x656565);
    float bankAngle = game->turnLeft ? -0.1f : game->turnRight ? 0.1f : 0.0f;

    context.camZ = game->camZ - farWrap;
    project_road(&farRoad, &context);

    for (int offset = VIEW_DISTANCE; offset > 0; offset--) {
        int segment = start + offset;
        int segmentIndex = segment >= ROAD_COUNT ? segment - ROAD_COUNT : segment;
        Road nearRoad = game->roads[nearIndex];
        int nearWrap = segment - 1 >= ROAD_COUNT ? TRACK_LENGTH : 0;
        lv_color_t edge = segment % 2 ? lv_color_black() : lv_color_white();
        lv_color_t road = segment % 2 ? roadA : roadB;

        context.camZ = game->camZ - nearWrap;
        project_road(&nearRoad, &context);

        if ((farRoad.Y >= WIN_HEIGHT && nearRoad.Y >= WIN_HEIGHT) ||
            (farRoad.Y < -WIN_HEIGHT && nearRoad.Y < -WIN_HEIGHT)) {
            farRoad = nearRoad;
            nearIndex = nearIndex > 0 ? nearIndex - 1 : ROAD_COUNT - 1;
            continue;
        }

        draw_ground_band(layer, &farRoad, &nearRoad, bankAngle);
        draw_road_band(layer, &farRoad, &nearRoad, 1.3f, bankAngle, edge);
        draw_road_band(layer, &farRoad, &nearRoad, 1.0f, bankAngle, road);

        if (segmentIndex == ROAD_COUNT - 1) {
            draw_finish_flag(layer, &game->roads[ROAD_COUNT - 1], &context);
        }

        for (int j = 0; j < COLLECTIBLE_COUNT; j++) {
            if (segmentIndex == game->collectiblePositions[j] && !game->collectibles[j].eaten) {
                lv_coord_t x1 = (lv_coord_t)game->collectibles[j].p[0].X;
                lv_coord_t y1 = (lv_coord_t)game->collectibles[j].p[2].Y;
                lv_coord_t x2 = (lv_coord_t)game->collectibles[j].p[1].X;
                lv_coord_t y2 = (lv_coord_t)game->collectibles[j].p[0].Y;
                draw_rect_clipped(layer, x1, y1, x2, y2, lv_color_hex(0xffd447));
            }
        }

        farRoad = nearRoad;
        nearIndex = nearIndex > 0 ? nearIndex - 1 : ROAD_COUNT - 1;
    }
}

/* LVGL 版的整帧绘制：背景、赛道、前景车体、HUD 和跳脸。 */
static void render_view(RacingLvglView *view)
{
    char text[64];
    lv_draw_rect_dsc_t car;
    lv_layer_t layer;
    lv_area_t carArea;

    lv_canvas_fill_bg(view->canvas, lv_color_hex(0x5fb8e8), LV_OPA_COVER);
    lv_canvas_init_layer(view->canvas, &layer);
    render_track(view, &layer);

    lv_draw_rect_dsc_init(&car);
    car.bg_color = lv_color_hex(0xf6d04d);
    car.bg_opa = LV_OPA_COVER;
    car.border_color = lv_color_black();
    car.border_width = 2;
    carArea.x1 = 0;
    carArea.y1 = WIN_HEIGHT - 112;
    carArea.x2 = WIN_WIDTH - 1;
    carArea.y2 = WIN_HEIGHT - 1;
    lv_draw_rect(&layer, &car, &carArea);

    snprintf(text, sizeof(text), "Lap %d/3", view->game->lap);
    draw_text(&layer, text, 8, 8, lv_color_hex(0xff2020));
    snprintf(text, sizeof(text), "%ds", view->game->elapsedMs / 1000);
    draw_text(&layer, text, 8, 48, lv_color_hex(0x00ffff));
    draw_energy(&layer, view->game->energy);

    if (view->game->mode == RACING_MODE_WIN) {
        snprintf(text, sizeof(text), "Win %ds", view->game->finalSeconds);
        draw_text(&layer, text, 8, 68, lv_color_hex(0xffd447));
    }

    draw_mode_overlay(&layer, view->game);

    draw_hit_overlay(&layer, view->game->hitFrames);

    lv_canvas_finish_layer(view->canvas, &layer);
    lv_obj_invalidate(view->canvas);
}

/* 定时器回调里同时推进游戏状态和刷新画面。 */
static void timer_cb(lv_timer_t *timer)
{
    RacingLvglView *view = (RacingLvglView *)lv_timer_get_user_data(timer);
    uint32_t now = lv_tick_get();
    uint32_t delta = now - view->lastTick;
#if RACING_PRINT_FRAME_TIME
    uint32_t workStart = now;
#endif
    view->lastTick = now;

    racing_game_update(view->game, &view->input, (int)delta);
    render_view(view);

#if RACING_PRINT_FRAME_TIME
    {
        uint32_t workMs = lv_tick_elaps(workStart);
        float fps = delta > 0 ? 1000.0f / (float)delta : 0.0f;
        printf("frame dt=%u ms work=%u ms fps=%.1f\n", (unsigned int)delta, (unsigned int)workMs, fps);
        fflush(stdout);
    }
#endif
}

RacingLvglView *racing_lvgl_create(lv_obj_t *parent, RacingGame *game)
{
    size_t bufferPixels = (size_t)WIN_WIDTH * WIN_HEIGHT;
    RacingLvglView *view = (RacingLvglView *)calloc(1, sizeof(*view));
    if (view == NULL || game == NULL) {
        free(view);
        return NULL;
    }

    view->buffer = (lv_color_t *)malloc(bufferPixels * sizeof(lv_color_t));
    if (view->buffer == NULL) {
        free(view);
        return NULL;
    }

    view->ownsBuffer = true;
    view->game = game;
    view->canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(view->canvas, view->buffer, WIN_WIDTH, WIN_HEIGHT, RACING_LV_CANVAS_FORMAT);
    lv_obj_set_size(view->canvas, WIN_WIDTH, WIN_HEIGHT);
    view->lastTick = lv_tick_get();
    view->timer = lv_timer_create(timer_cb, RACING_LVGL_FRAME_MS, view);
    render_view(view);
    return view;
}

RacingLvglView *racing_lvgl_create_with_buffer(lv_obj_t *parent, RacingGame *game, lv_color_t *buffer, size_t bufferPixels)
{
    RacingLvglView *view;

    if (game == NULL || buffer == NULL || bufferPixels < (size_t)WIN_WIDTH * WIN_HEIGHT) {
        return NULL;
    }

    view = (RacingLvglView *)calloc(1, sizeof(*view));
    if (view == NULL) {
        return NULL;
    }

    view->game = game;
    view->buffer = buffer;
    view->ownsBuffer = false;
    view->canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(view->canvas, view->buffer, WIN_WIDTH, WIN_HEIGHT, RACING_LV_CANVAS_FORMAT);
    lv_obj_set_size(view->canvas, WIN_WIDTH, WIN_HEIGHT);
    view->lastTick = lv_tick_get();
    view->timer = lv_timer_create(timer_cb, RACING_LVGL_FRAME_MS, view);
    render_view(view);
    return view;
}

void racing_lvgl_delete(RacingLvglView *view)
{
    if (view == NULL) {
        return;
    }

    if (view->timer != NULL) {
        lv_timer_del(view->timer);
    }

    if (view->canvas != NULL) {
        lv_obj_del(view->canvas);
    }

    if (view->ownsBuffer) {
        free(view->buffer);
    }
    free(view);
}

void racing_lvgl_set_input(RacingLvglView *view, RacingInput input)
{
    if (view != NULL) {
        view->input = input;
    }
}

lv_obj_t *racing_lvgl_object(RacingLvglView *view)
{
    return view != NULL ? view->canvas : NULL;
}
