#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif

#include "game.h"
#include "render_sdl.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

static RacingInput read_desktop_input(bool *running)
{
    int keyCount = 0;
    const Uint8 *keys;
    RacingInput input = {0};
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            *running = false;
        } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
            if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE || event.key.keysym.scancode == SDL_SCANCODE_P) {
                input.pause = true;
            } else if (event.key.keysym.scancode == SDL_SCANCODE_RETURN || event.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                input.start = true;
            } else if (event.key.keysym.scancode == SDL_SCANCODE_R) {
                input.restart = true;
            } else if (event.key.keysym.scancode == SDL_SCANCODE_F) {
                input.fly = true;
            } else if (event.key.keysym.scancode == SDL_SCANCODE_1) {
                input.mapSelect = true;     /* 主菜单 → 关卡选择 */
            } else if (event.key.keysym.scancode == SDL_SCANCODE_2) {
                input.controlSelect = true; /* 主菜单 → 操作选择 */
            } else if (event.key.keysym.scancode == SDL_SCANCODE_LEFT) {
                input.cyclePrev = true;     /* 二级菜单:上一项 */
            } else if (event.key.keysym.scancode == SDL_SCANCODE_RIGHT) {
                input.cycleNext = true;     /* 二级菜单:下一项 */
            } else if (event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) {
                input.back = true;          /* 二级菜单:返回 */
            } else if (event.key.keysym.scancode == SDL_SCANCODE_M) {
                input.toMenu = true;        /* 暂停/胜利:回主菜单 */
            }
        }
    }

    keys = SDL_GetKeyboardState(&keyCount);
    if (keys == NULL) {
        return input;
    }

    input.accelerate = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
    input.brake = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
    input.left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
    input.right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
    input.boost = keys[SDL_SCANCODE_SPACE];
    (void)keyCount;
    return input;
}

int main(void)
{
    RacingGame game;
    RacingSdlRenderer *view;
    SDL_Window *window;
    SDL_Renderer *renderer;
    bool running = true;
    uint32_t lastTick;

#ifndef _WIN32
    setenv("DBUS_FATAL_WARNINGS", "0", 1);
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("Racing SDL", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_WIDTH, WIN_HEIGHT, SDL_WINDOW_SHOWN);
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }

    if (renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    view = racing_sdl_renderer_create(renderer);
    if (view == NULL) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    racing_game_init(&game, (unsigned int)SDL_GetTicks());
    lastTick = SDL_GetTicks();

    printf("Racing SDL started. 1/2/3 select map, Enter starts or resumes, R restarts, W/A/S/D drive, Space boosts, Esc or P pauses.\n");

    while (running) {
        /* 桌面版主循环：读输入、推进游戏状态、绘制，然后补足到目标帧率。 */
        uint32_t frameStart = SDL_GetTicks();
        uint32_t now = SDL_GetTicks();
        uint32_t delta = now - lastTick;
        RacingInput input = read_desktop_input(&running);
        lastTick = now;

        racing_game_update(&game, &input, (int)delta);
        racing_sdl_render(view, &game);

        {
            uint32_t elapsed = SDL_GetTicks() - frameStart;
            if (elapsed < RACING_TARGET_FRAME_MS) {
                SDL_Delay(RACING_TARGET_FRAME_MS - elapsed);
            }
        }
    }

    racing_sdl_renderer_delete(view);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
