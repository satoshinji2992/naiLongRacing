#ifndef RACING_GAME_H
#define RACING_GAME_H

typedef struct Game Game;

Game *game_create(void);
bool game_init(Game *game);
bool game_is_running(const Game *game);
void game_step(Game *game);
void game_destroy(Game *game);

#endif
