#ifndef RACING_RENDER_SDL_H
#define RACING_RENDER_SDL_H

#include <SDL.h>

#include "game.h"

typedef struct RacingSdlRenderer RacingSdlRenderer;

RacingSdlRenderer *racing_sdl_renderer_create(SDL_Renderer *renderer);
void racing_sdl_renderer_delete(RacingSdlRenderer *view);
void racing_sdl_render(RacingSdlRenderer *view, const RacingGame *game);

#endif
