#ifndef RACING_JY60_H
#define RACING_JY60_H

#include <stdbool.h>

#include "game.h"

typedef struct Jy60Sample {
    bool valid;
    unsigned int frame_count;
    float acc_x_g;
    float acc_y_g;
    float acc_z_g;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
} Jy60Sample;

typedef struct Jy60Control {
    bool ready;
    float roll_zero;
    float pitch_zero;
    float yaw_zero;
} Jy60Control;

void jy60_init(void);
void jy60_deinit(void);
void jy60_poll(void);
bool jy60_get_sample(Jy60Sample *sample);
void jy60_control_calibrate(Jy60Control *control, const Jy60Sample *sample);
void jy60_apply_control(const Jy60Control *control, const Jy60Sample *sample,
                        RacingInput *input);

#endif
