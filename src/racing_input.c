/****************************************************************************
 * Racing 输入处理(fb 版,无 LVGL)
 *
 * 触摸(open /dev/input0):
 *  - 菜单屏(START / MAP_SELECT / CONTROL_SELECT):按屏分区映射
 *      主菜单:上半=关卡选择,下半=操作选择;
 *      二级菜单:左 < / 右 > / 中间确认。(返回走按钮,不用触摸)
 *  - 游戏中·普通模式:左 1/3 左转、右 1/3 右转、中间 1/3 点一下切换 boost 开/关。
 *  - 游戏中·体感模式:不触摸转向(方向靠 JY60),按住屏幕任意处=boost,松开=关。
 *  - 车始终自动前进(accelerate 由主循环强制);触摸这里控制的是 boost(能量加速)。
 * GPIO 三按键(/dev/gpioN):① start/pause ② back(回主菜单)③ restart。
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

static RacingInput g_input;
static int g_menu_screen;     /* 当前菜单屏(RACING_MODE_*);0=游戏中/暂停 */
static int g_drive_mode;      /* 游戏中操控:0=Original 1=Gyro */
static bool g_boost_on;       /* Original:boost 开关(中间 1/3 点一下切换) */
static bool g_touch_down;     /* 当前是否按住屏幕(Gyro:按住=boost) */
static int g_touch_fd = -1;
static int g_button_fds[3] = { -1, -1, -1 };
static bool g_button_prev[3];
static bool g_gpio_ready;

/* 菜单触摸:按当前菜单屏把落点映射成对应一次性输入(返回走按钮,不用触摸)。 */
static void apply_menu_touch(int x, int y)
{
    if (g_menu_screen == RACING_MODE_START)
    {
        if (y < WIN_HEIGHT / 2)
        {
            g_input.mapSelect = true;       /* 上半:关卡选择 */
        }
        else
        {
            g_input.controlSelect = true;   /* 下半:操作选择 */
        }
    }
    else if (g_menu_screen == RACING_MODE_MAP_SELECT)
    {
        /* 关卡选择:左 < / 右 > / 中间确认(浏览+预览式)。 */
        if (x < WIN_WIDTH / 3)
        {
            g_input.cyclePrev = true;
        }
        else if (x > (WIN_WIDTH * 2) / 3)
        {
            g_input.cycleNext = true;
        }
        else
        {
            g_input.start = true;
        }
    }
    else if (g_menu_screen == RACING_MODE_CONTROL_SELECT)
    {
        /* 操作选择:点哪个按钮选哪个(Original/Gyro/Test 竖排,按 y 分三段)。 */
        if (y < 112)
        {
            g_input.ctrl1 = true;
        }
        else if (y < 176)
        {
            g_input.ctrl2 = true;
        }
        else
        {
            g_input.ctrl3 = true;
        }
    }
}

/* 游戏中触摸:按操控模式映射(控制的是 boost,车始终自动前进)。edge=按下瞬间
 * (普通模式中间 1/3 点一下切换 boost)。 */
static void apply_drive_touch(int x, int y, bool edge)
{
    (void)y;
    if (g_drive_mode == 1)
    {
        /* 体感模式:触摸只管 boost(由 g_touch_down 驱动),不转向。 */
        return;
    }
    /* 普通模式:左/右 1/3 转向,中间 1/3 切换 boost。 */
    g_input.left = (x < WIN_WIDTH / 3);
    g_input.right = (x > (WIN_WIDTH * 2) / 3);
    if (edge && x >= WIN_WIDTH / 3 && x <= (WIN_WIDTH * 2) / 3)
    {
        g_boost_on = !g_boost_on;
    }
}

static void clear_drive_axes(void)
{
    g_input.left = false;
    g_input.right = false;
    g_input.brake = false;
    g_input.fly = false;
}

/****************************************************************************
 * GPIO 按键(板载物理按键):start/pause、back(回主菜单)、restart。
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

    /* 按键1:start/pause(边沿);按键2:back(回主菜单,边沿);按键3:restart(边沿)。 */
    if (button[0] && !g_button_prev[0]) {
        g_input.start = true;
        g_input.pause = true;
    }
    if (button[1] && !g_button_prev[1]) {
        g_input.back = true;
        g_input.toMenu = true;
    }
    if (button[2] && !g_button_prev[2]) {
        g_input.restart = true;
    }

    for (int i = 0; i < 3; i++) {
        g_button_prev[i] = button[i];
    }
}

/****************************************************************************
 * Public API
 ****************************************************************************/

void racing_input_init(void)
{
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
            continue;
        }

        if (sample->npoints > RACING_TOUCH_MAX_POINTS) {
            printf("[RACING-TOUCH] unsupported points=%d\n", sample->npoints);
            continue;
        }

        if (sample->npoints > 1) {
            ssize_t remain = (ssize_t)((sample->npoints - 1) *
                                       sizeof(struct touch_point_s));
            ssize_t got = read(g_touch_fd, &sample->point[1], (size_t)remain);

            if (got != remain) {
                printf("[RACING-TOUCH] short multi-touch read: points=%d got=%zd need=%zd\n",
                       sample->npoints, got, remain);
                continue;
            }
        }

        max_points = sample->npoints;
        for (int i = 0; i < max_points; i++) {
            if ((sample->point[i].flags & TOUCH_UP) == 0) {
                active_points++;
            }
        }

        flags = sample->point[0].flags;
        x = sample->point[0].x;
        y = sample->point[0].y;

        /* 调试日志:只在 DOWN/UP(边沿)打印坐标,MOVE 不打印以免串口刷屏拖慢帧。 */
        if (flags & TOUCH_UP) {
            printf("[RACING-TOUCH] UP   x=%d y=%d points=%d\n", x, y, active_points);
            if (active_points == 0) {
                g_touch_down = false;
                clear_drive_axes();          /* 松手:停止转向(Gyro 加速由 g_touch_down 停) */
            }
        } else if (flags & TOUCH_DOWN) {
            g_touch_down = true;
            printf("[RACING-TOUCH] DOWN x=%d y=%d points=%d screen=%d mode=%d\n",
                   x, y, active_points, g_menu_screen, g_drive_mode);
            if (g_menu_screen == RACING_MODE_START ||
                g_menu_screen == RACING_MODE_MAP_SELECT ||
                g_menu_screen == RACING_MODE_CONTROL_SELECT)
            {
                apply_menu_touch(x, y);
            }
            else
            {
                apply_drive_touch(x, y, true);   /* 游戏中:按模式映射 */
            }
        } else if (flags & TOUCH_MOVE) {
            if (!(g_menu_screen == RACING_MODE_START ||
                  g_menu_screen == RACING_MODE_MAP_SELECT ||
                  g_menu_screen == RACING_MODE_CONTROL_SELECT))
            {
                apply_drive_touch(x, y, false);  /* 游戏中 MOVE:普通模式实时更新转向 */
            }
        }
    }
}

RacingInput racing_input_get(void)
{
    RacingInput input;

    update_gpio_buttons();
    input = g_input;
    /* 游戏中(g_menu_screen<0)boost 按模式决定(车自动前进,accelerate 由主循环强制):
     * 普通模式 = 中间 1/3 切换的开关;体感模式 = 按住屏幕。
     * 注意:不能用 ==0 判驾驶——RACING_MODE_START 恰好==0,会跟主菜单冲突。 */
    if (g_menu_screen < 0)
    {
        input.boost = (g_drive_mode == 1) ? g_touch_down : g_boost_on;
    }
    /* 一次性标志取走即清;转向是持续态,由触摸 UP 清。 */
    g_input.start = false;
    g_input.pause = false;
    g_input.restart = false;
    g_input.mapSelect = false;
    g_input.controlSelect = false;
    g_input.back = false;
    g_input.cyclePrev = false;
    g_input.cycleNext = false;
    g_input.toMenu = false;
    g_input.ctrl1 = false;
    g_input.ctrl2 = false;
    g_input.ctrl3 = false;
    return input;
}

void racing_input_set_menu_screen(int mode)
{
    g_menu_screen = mode;
}

void racing_input_set_drive_mode(int gyro)
{
    g_drive_mode = gyro ? 1 : 0;
}

void racing_input_reset(void)
{
    memset(&g_input, 0, sizeof(g_input));
    g_boost_on = false;
    g_touch_down = false;
}
