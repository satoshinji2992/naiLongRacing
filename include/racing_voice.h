#ifndef RACING_VOICE_H
#define RACING_VOICE_H

#include "game.h"

/* XiaoZhi/control_center voice bridge.  It listens on the same UI IPC port
 * used by lvgldemo and converts recognized text into one-shot RacingInput. */
void racing_voice_init(void);
void racing_voice_deinit(void);
void racing_voice_poll(void);
void racing_voice_apply_input(RacingInput *input);
const char *racing_voice_last_text(void);
int racing_voice_state(void);

#endif
