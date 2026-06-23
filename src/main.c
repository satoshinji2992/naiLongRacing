#include "game.h"

#include <stdio.h>

int main(void)
{
    RacingGame game;
    RacingInput input = {0};

    /* 这是一个最小烟测入口，只确认核心逻辑能跑，不负责真正游玩。 */
    printf("Smoke test only. To play, run: ./build/racing_desktop\n");

    racing_game_init(&game, 1U);
    input.start = true;
    racing_game_update(&game, &input, 16);
    input.start = false;

    for (int i = 0; i < 60; i++) {
        input.accelerate = true;
        racing_game_update(&game, &input, 16);
    }

    return 0;
}
