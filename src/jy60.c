#include "jy60.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#ifndef CONFIG_EXAMPLES_RACING_JY60_DEVPATH
#define CONFIG_EXAMPLES_RACING_JY60_DEVPATH "/dev/uart1"
#endif

#ifndef CONFIG_EXAMPLES_RACING_JY60_BAUD
#define CONFIG_EXAMPLES_RACING_JY60_BAUD 9600
#endif

#define JY60_FRAME_HEAD 0x55u
#define JY60_FRAME_LEN 11
static int g_jy60_fd = -1;
static Jy60Sample g_sample;
static uint8_t g_frame[JY60_FRAME_LEN];
static int g_frame_pos;

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return B9600;
    }
}

static int16_t le_i16(uint8_t lo, uint8_t hi)
{
    return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
}

static bool jy60_checksum_ok(const uint8_t frame[JY60_FRAME_LEN])
{
    uint8_t sum = 0;

    for (int i = 0; i < JY60_FRAME_LEN - 1; i++) {
        sum = (uint8_t)(sum + frame[i]);
    }

    return sum == frame[JY60_FRAME_LEN - 1];
}

static void jy60_apply_frame(const uint8_t frame[JY60_FRAME_LEN])
{
    if (!jy60_checksum_ok(frame)) {
        return;
    }

    if (frame[1] == 0x51) {
        g_sample.acc_x_g = (float)le_i16(frame[2], frame[3]) / 32768.0f * 16.0f;
        g_sample.acc_y_g = (float)le_i16(frame[4], frame[5]) / 32768.0f * 16.0f;
        g_sample.acc_z_g = (float)le_i16(frame[6], frame[7]) / 32768.0f * 16.0f;
    } else if (frame[1] == 0x52) {
        g_sample.gyro_x_dps = (float)le_i16(frame[2], frame[3]) / 32768.0f * 2000.0f;
        g_sample.gyro_y_dps = (float)le_i16(frame[4], frame[5]) / 32768.0f * 2000.0f;
        g_sample.gyro_z_dps = (float)le_i16(frame[6], frame[7]) / 32768.0f * 2000.0f;
    } else if (frame[1] == 0x53) {
        g_sample.roll_deg = (float)le_i16(frame[2], frame[3]) / 32768.0f * 180.0f;
        g_sample.pitch_deg = (float)le_i16(frame[4], frame[5]) / 32768.0f * 180.0f;
        g_sample.yaw_deg = (float)le_i16(frame[6], frame[7]) / 32768.0f * 180.0f;
    } else {
        return;
    }

    g_sample.valid = true;
    g_sample.frame_count++;
}

static int configure_uart(int fd)
{
    struct termios tio;
    speed_t speed = baud_to_speed(CONFIG_EXAMPLES_RACING_JY60_BAUD);

    if (tcgetattr(fd, &tio) < 0) {
        return -1;
    }

    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
    tio.c_cflag &= ~CRTSCTS;
#endif
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        return -1;
    }

    tcflush(fd, TCIFLUSH);
    return 0;
}

void jy60_init(void)
{
    memset(&g_sample, 0, sizeof(g_sample));
    memset(g_frame, 0, sizeof(g_frame));
    g_frame_pos = 0;

    if (g_jy60_fd >= 0) {
        return;
    }

    g_jy60_fd = open(CONFIG_EXAMPLES_RACING_JY60_DEVPATH, O_RDWR | O_NONBLOCK);
    if (g_jy60_fd < 0) {
        printf("[RACING-JY60] open failed: %s errno=%d\n",
               CONFIG_EXAMPLES_RACING_JY60_DEVPATH, errno);
        return;
    }

    if (configure_uart(g_jy60_fd) < 0) {
        printf("[RACING-JY60] configure failed: %s errno=%d\n",
               CONFIG_EXAMPLES_RACING_JY60_DEVPATH, errno);
        close(g_jy60_fd);
        g_jy60_fd = -1;
        return;
    }

    printf("[RACING-JY60] opened %s baud=%d\n",
           CONFIG_EXAMPLES_RACING_JY60_DEVPATH, CONFIG_EXAMPLES_RACING_JY60_BAUD);
}

void jy60_deinit(void)
{
    if (g_jy60_fd >= 0) {
        close(g_jy60_fd);
        g_jy60_fd = -1;
    }
    memset(&g_sample, 0, sizeof(g_sample));
    memset(g_frame, 0, sizeof(g_frame));
    g_frame_pos = 0;
}

void jy60_poll(void)
{
    uint8_t buf[64];
    ssize_t nread;

    if (g_jy60_fd < 0) {
        return;
    }

    while ((nread = read(g_jy60_fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < nread; i++) {
            uint8_t b = buf[i];

            if (g_frame_pos == 0 && b != JY60_FRAME_HEAD) {
                continue;
            }

            g_frame[g_frame_pos++] = b;
            if (g_frame_pos == 2 &&
                g_frame[1] != 0x51 && g_frame[1] != 0x52 && g_frame[1] != 0x53) {
                g_frame_pos = b == JY60_FRAME_HEAD ? 1 : 0;
                continue;
            }

            if (g_frame_pos == JY60_FRAME_LEN) {
                jy60_apply_frame(g_frame);
                g_frame_pos = 0;
            }
        }
    }
}

bool jy60_get_sample(Jy60Sample *sample)
{
    if (sample == NULL) {
        return false;
    }
    *sample = g_sample;
    return g_sample.valid;
}

void jy60_control_calibrate(Jy60Control *control, const Jy60Sample *sample)
{
    if (control == NULL) {
        return;
    }

    memset(control, 0, sizeof(*control));
    if (sample != NULL && sample->valid) {
        control->roll_zero = sample->roll_deg;
        control->pitch_zero = sample->pitch_deg;
        control->yaw_zero = sample->yaw_deg;
        control->ready = true;
    }
}

void jy60_apply_control(const Jy60Control *control, const Jy60Sample *sample,
                        RacingInput *input)
{
    float roll;

    if (control == NULL || sample == NULL || input == NULL ||
        !control->ready || !sample->valid) {
        return;
    }

    roll = sample->roll_deg - control->roll_zero;
    input->left = false;
    input->right = false;

    if (roll < -10.0f) {
        input->left = true;
    } else if (roll > 10.0f) {
        input->right = true;
    }

}
