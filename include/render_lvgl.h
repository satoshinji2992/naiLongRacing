#ifndef RACING_RENDER_LVGL_H
#define RACING_RENDER_LVGL_H

#include <stddef.h>

#include <lvgl.h>

#include "game.h"

typedef struct RacingLvglView RacingLvglView;

RacingLvglView *racing_lvgl_create(lv_obj_t *parent, RacingGame *game);
RacingLvglView *racing_lvgl_create_with_buffer(lv_obj_t *parent, RacingGame *game, lv_color_t *buffer, size_t bufferPixels);
void racing_lvgl_delete(RacingLvglView *view);
void racing_lvgl_set_input(RacingLvglView *view, RacingInput input);
lv_obj_t *racing_lvgl_object(RacingLvglView *view);

#endif
