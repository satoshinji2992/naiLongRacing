#include "game.h"

int main(void)
{
    Game *game = game_create();

    if (game == nullptr) {
        return 1;
    }

    if (!game_init(game)) {
        game_destroy(game);
        return 1;
    }

    while (game_is_running(game)) {
        game_step(game);
    }

    game_destroy(game);
    return 0;
}
