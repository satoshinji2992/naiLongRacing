/****************************************************************************
 * Racing 输入处理(fb 版,无 LVGL)
 *
 * 触摸:open /dev/input0,非阻塞读 struct touch_sample_s,按屏幕分区映射。
 * GPIO:直接 open /dev/gpioN + GPIOC_READ(板载物理按键)。
 ****************************************************************************/

#include "racing_input.h"
#include "config.h"

#include <nuttx/config.h>
#include <nuttx/input/touchscreen.h>
#include <nuttx/ioexpander/gpio.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef CONFIG_EXAMPLES_RACING_INPUT_DEVPATH
#define CONFIG_EXAMPLES_RACING_INPUT_DEVPATH "/dev/input0"
#endif

#ifndef CONFIG_EXAMPLES_RACING_BUTTON1_DEVPATH
#define CONFIG_EXAMPLES_RACING_BUTTON1_DEVPATH "/dev/gpio1"
#endif

#ifndef CONFIG_EXAMPLES_RACING_BUTTON2_DEVPATH
#define CONFIG_EXAMPLES_RACING_BUTTON2_DEVPATH "/dev/gpio3"
#endif

#ifndef CONFIG_EXAMPLES_RACING_BUTTON3_DEVPATH
#define CONFIG_EXAMPLES_RACING_BUTTON3_DEVPATH "/dev/gpio4"
#endif

#define RACING_TOUCH_MAX_POINTS 5
#define RACING_TOUCH_BOOST_HOLD_MS 100

typedef struct {
    int x1;
    int y1;
    int x2;
    int y2;
} Rect;

typedef struct {
    Rect accelerate;
    Rect brake;
    Rect left;
    Rect right;
    Rect boost;
    Rect fly;
    Rect pause;
    Rect start;
} TouchZones;

static TouchZones g_zones;
static RacingInput g_input;
static RacingMenuSelection g_menu_selection;
static int g_touch_fd = -1;
static int g_button_fds[3] = { -1, -1, -1 };
static bool g_button_prev[3];
static bool g_gpio_ready;
static bool g_touch_boost_down;
static bool g_touch_boost;
static long g_touch_boost_since_ms;

static long touch_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static bool point_in_rect(int x, int y, const Rect *r)
{
    return x >= r->x1 && x <= r->x2 && y >= r->y1 && y <= r->y2;
}

static RacingMenuSelection menu_selection_from_point(int x, int y)
{
    int button_w = WIN_WIDTH - 160;
    int button_h = 54;
    int x1 = (WIN_WIDTH - button_w) / 2;
    int x2 = x1 + button_w;
    int y1 = 88;

    if (button_w < 220) {
        button_w = WIN_WIDTH - 40;
        x1 = 20;
        x2 = WIN_WIDTH - 20;
    }

    /* 顶部区域(模式按钮上方)触摸 = 循环切换地图。 */
    if (y < y1) {
        return RACING_MENU_MAP_CYCLE;
    }

    if (x < x1 || x > x2) {
        return RACING_MENU_NONE;
    }

    if (y >= y1 && y <= y1 + button_h) {
        return RACING_MENU_ORIGINAL;
    }
    y1 += button_h + 18;
    if (y >= y1 && y <= y1 + button_h) {
        return RACING_MENU_GYRO;
    }
    y1 += button_h + 18;
    if (y >= y1 && y <= y1 + button_h) {
        return RACING_MENU_TEST;
    }

    return RACING_MENU_NONE;
}

/* 触摸分区:左半屏左转/右半屏右转 + 中央开始/暂停。
 * Gyro Mode 的 boost 由上层把触摸长按状态映射过去。 */
static void init_touch_zones(int width, int height)
{
    int hw = width / 2;
    int hh = height / 2;

    /* 转向:左半屏=左转,右半屏=右转(固定速度自动前进,无前进/后退)。 */
    g_zones.left.x1 = 0;       g_zones.left.y1 = 0;        g_zones.left.x2 = hw;     g_zones.left.y2 = height;
    g_zones.right.x1 = hw;     g_zones.right.y1 = 0;       g_zones.right.x2 = width; g_zones.right.y2 = height;

    /* 中央矩形 = 开始/暂停(边沿触发) */
    g_zones.start.x1 = hw - 60; g_zones.start.y1 = hh - 40;
    g_zones.start.x2 = hw + 60; g_zones.start.y2 = hh + 40;
    g_zones.pause = g_zones.start;

    /* brake/accelerate 不用;boost 默认走 GPIO,触摸长按状态另行提供给上层。 */
    g_zones.brake.x1 = -1; g_zones.brake.y1 = -1; g_zones.brake.x2 = -1; g_zones.brake.y2 = -1;
    g_zones.accelerate = g_zones.brake;
    g_zones.boost = g_zones.brake;
    g_zones.fly = g_zones.brake;
}

static void clear_touch_axes(void)
{
    g_input.accelerate = false;
    g_input.brake = false;
    g_input.left = false;
    g_input.right = false;
    g_input.fly = false;
}

static void clear_touch_boost(void)
{
    g_touch_boost_down = false;
    g_touch_boost = false;
    g_touch_boost_since_ms = 0;
}

static void update_touch_boost_state(bool touch_down)
{
    if (touch_down) {
        if (!g_touch_boost_down) {
            g_touch_boost_since_ms = touch_now_ms();
        }
        g_touch_boost_down = true;
    } else {
        g_touch_boost_down = false;
        g_touch_boost_since_ms = 0;
        g_touch_boost = false;
    }
}

static void refresh_touch_boost(void)
{
    if (g_touch_boost_down &&
        touch_now_ms() - g_touch_boost_since_ms >= RACING_TOUCH_BOOST_HOLD_MS) {
        g_touch_boost = true;
    } else {
        g_touch_boost = false;
    }
}

/* 把一个触摸点映射到分区输入。edge=true 表示按下瞬间(触发 start/pause)。 */
static void apply_touch_point(int x, int y, bool edge)
{
    /* 中央优先:开始/暂停,且不当作移动。 */
    if (point_in_rect(x, y, &g_zones.start)) {
        clear_touch_axes();
        if (edge) {
            g_input.start = true;
            g_input.pause = true;
        }
        return;
    }

    g_input.left = point_in_rect(x, y, &g_zones.left);
    g_input.right = point_in_rect(x, y, &g_zones.right);
    /* 前进由主循环强制;boost 默认走 GPIO,触摸长按状态另行提供给上层。 */
    g_input.accelerate = false;
    g_input.brake = false;
    g_input.fly = false;
}

/****************************************************************************
 * GPIO 按键(板载物理按键)。
 ****************************************************************************/

static int open_gpio_button(const char *devpath)
{
    int fd;

    if (devpath == NULL || devpath[0] == '\0') {
        return -1;
    }
    fd = open(devpath, O_RDWR);
    if (fd < 0) {
        printf("[RACING] GPIO button not available: %s\n", devpath);
        return -1;
    }
    if (ioctl(fd, GPIOC_SETPINTYPE, GPIO_INPUT_PIN_PULLDOWN) < 0) {
        printf("[RACING] Failed to configure GPIO button: %s\n", devpath);
        close(fd);
        return -1;
    }
    printf("[RACING] GPIO button opened: %s\n", devpath);
    return fd;
}

static bool read_gpio_button(int fd)
{
    bool invalue = false;
    if (fd < 0) {
        return false;
    }
    if (ioctl(fd, GPIOC_READ, (unsigned long)((uintptr_t)&invalue)) < 0) {
        return false;
    }
    return invalue;
}

static void init_gpio_buttons(void)
{
    if (g_gpio_ready) {
        return;
    }
    g_button_fds[0] = open_gpio_button(CONFIG_EXAMPLES_RACING_BUTTON1_DEVPATH);
    g_button_fds[1] = open_gpio_button(CONFIG_EXAMPLES_RACING_BUTTON2_DEVPATH);
    g_button_fds[2] = open_gpio_button(CONFIG_EXAMPLES_RACING_BUTTON3_DEVPATH);
    g_gpio_ready = true;
}

static void update_gpio_buttons(void)
{
    bool button[3];

    if (!g_gpio_ready) {
        init_gpio_buttons();
    }
    for (int i = 0; i < 3; i++) {
        button[i] = read_gpio_button(g_button_fds[i]);
    }

    /* 按键1:边沿触发 start/pause。 */
    if (button[0] && !g_button_prev[0]) {
        g_input.start = true;
        g_input.pause = true;
    }
    /* 按键2:原始模式 boost。按键3:fly。 */
    g_input.boost = button[1];
    g_input.fly = g_input.fly || button[2];

    for (int i = 0; i < 3; i++) {
        g_button_prev[i] = button[i];
    }
}

/****************************************************************************
 * Public API
 ****************************************************************************/

void racing_input_init(void)
{
    init_touch_zones(WIN_WIDTH, WIN_HEIGHT);
    g_touch_fd = open(CONFIG_EXAMPLES_RACING_INPUT_DEVPATH, O_RDWR | O_NONBLOCK);
    if (g_touch_fd < 0) {
        printf("[RACING] touchscreen not available: %s (%d)\n",
               CONFIG_EXAMPLES_RACING_INPUT_DEVPATH, g_touch_fd);
        g_touch_fd = -1;
    } else {
        printf("[RACING] touchscreen opened: %s\n",
               CONFIG_EXAMPLES_RACING_INPUT_DEVPATH);
    }
    init_gpio_buttons();
    racing_input_reset();
}

void racing_input_deinit(void)
{
    if (g_touch_fd >= 0) {
        close(g_touch_fd);
        g_touch_fd = -1;
    }
    for (int i = 0; i < 3; i++) {
        if (g_button_fds[i] >= 0) {
            close(g_button_fds[i]);
            g_button_fds[i] = -1;
        }
        g_button_prev[i] = false;
    }
    g_gpio_ready = false;
    racing_input_reset();
}

void racing_input_poll(void)
{
    union {
        struct touch_sample_s sample;
        uint8_t raw[SIZEOF_TOUCH_SAMPLE_S(RACING_TOUCH_MAX_POINTS)];
    } touch;
    ssize_t nread;

    if (g_touch_fd < 0) {
        return;
    }

    /* 非阻塞把缓冲里的采样全部读完,只保留最后一条输入状态。 */
    while ((nread = read(g_touch_fd, &touch, SIZEOF_TOUCH_SAMPLE_S(1))) ==
           (ssize_t)SIZEOF_TOUCH_SAMPLE_S(1)) {
        struct touch_sample_s *sample = &touch.sample;
        int max_points;
        int active_points = 0;
        int flags;
        int x;
        int y;

        if (sample->npoints <= 0) {
            clear_touch_boost();
            continue;
        }

        if (sample->npoints > RACING_TOUCH_MAX_POINTS) {
            printf("[RACING-TOUCH] unsupported points=%d\n", sample->npoints);
            clear_touch_boost();
            continue;
        }

        if (sample->npoints > 1) {
            ssize_t remain = (ssize_t)((sample->npoints - 1) *
                                       sizeof(struct touch_point_s));
            ssize_t got = read(g_touch_fd, &sample->point[1], (size_t)remain);

            if (got != remain) {
                printf("[RACING-TOUCH] short multi-touch read: points=%d got=%zd need=%zd\n",
                       sample->npoints, got, remain);
                clear_touch_boost();
                continue;
            }
        }

        max_points = sample->npoints;
        for (int i = 0; i < max_points; i++) {
            if ((sample->point[i].flags & TOUCH_UP) == 0) {
                active_points++;
            }
        }

        update_touch_boost_state(active_points >= 1);

        flags = sample->point[0].flags;
        x = sample->point[0].x;
        y = sample->point[0].y;

        /* 调试日志:只在 DOWN/UP(边沿)打印坐标,MOVE 不打印以免串口刷屏拖慢帧。 */
        if (flags & TOUCH_UP) {
            printf("[RACING-TOUCH] UP   x=%d y=%d points=%d\n", x, y, active_points);
            if (active_points == 0) {
                clear_touch_axes();
            }
        } else if (flags & TOUCH_DOWN) {
            printf("[RACING-TOUCH] DOWN x=%d y=%d points=%d\n", x, y, active_points);
            g_menu_selection = menu_selection_from_point(x, y);
            apply_touch_point(x, y, true);
        } else if (flags & TOUCH_MOVE) {
            apply_touch_point(x, y, false);
        }
    }
}

RacingInput racing_input_get(void)
{
    RacingInput input;
    update_gpio_buttons();
    input = g_input;
    /* 一次性标志取走即清。 */
    g_input.start = false;
    g_input.pause = false;
    g_input.restart = false;
    return input;
}

RacingMenuSelection racing_input_get_menu_selection(void)
{
    RacingMenuSelection selection = g_menu_selection;
    g_menu_selection = RACING_MENU_NONE;
    return selection;
}

bool racing_input_get_touch_boost(void)
{
    refresh_touch_boost();
    return g_touch_boost;
}

void racing_input_reset(void)
{
    memset(&g_input, 0, sizeof(g_input));
    g_menu_selection = RACING_MENU_NONE;
    clear_touch_boost();
}
