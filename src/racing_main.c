/****************************************************************************
 * apps/examples/Racing/src/racing_main.c
 *
 * Racing Game Demo for OpenVela —— framebuffer 直绘版(无 LVGL)
 *
 * 完整可玩主循环:poll 触摸/GPIO 输入 -> 定步长推进游戏 -> 渲染一帧 ->
 * 帧率限制。SIGINT/SIGTERM 干净退出。
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <unistd.h>
#include <sys/boardctl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "game.h"
#include "jy60.h"
#include "render_fb.h"
#include "racing_input.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#undef NEED_BOARDINIT

#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile sig_atomic_t g_should_exit;

typedef enum RacingAppMode {
  RACING_APP_MENU = 0,
  RACING_APP_ORIGINAL,
  RACING_APP_GYRO,
  RACING_APP_TEST
} RacingAppMode;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void exit_signal_handler(int signo)
{
  (void)signo;
  g_should_exit = 1;
}

static long monotonic_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  RacingGame game;
  RacingFbView *view;
  RacingAppMode app_mode = RACING_APP_MENU;
  Jy60Sample jy60_sample;
  Jy60Control jy60_control;
  long last_ms;
  long seed;

  (void)argc;
  (void)argv;

  /* 用 sigaction 可靠捕获 Ctrl-C/SIGTERM,确保走正常 cleanup(关 fd、释放 fb)。 */
  {
    struct sigaction sa;
    sa.sa_handler = exit_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
  }

  /* g_should_exit 是静态变量,上一次运行退出时被置 1;重新进入 main 必须清零,
   * 否则循环一次都不进(表现为"退出后无法重新运行")。 */
  g_should_exit = 0;

#ifdef NEED_BOARDINIT
  boardctl(BOARDIOC_INIT, 0);
#endif

  /* 用启动时刻做随机种子,每局不同。 */
  seed = monotonic_ms();
  racing_game_init(&game, (unsigned int)seed);
  memset(&jy60_sample, 0, sizeof(jy60_sample));
  memset(&jy60_control, 0, sizeof(jy60_control));

  view = racing_fb_create(&game);
  if (view == NULL)
    {
      fprintf(stderr, "[RACING] failed to open framebuffer\n");
      return 1;
    }

  racing_input_init();
  jy60_init();

  printf("[RACING] running. Select Original / Gyro / Test mode on screen.\n");

  last_ms = monotonic_ms();
  int frames = 0;
  long stat_last = last_ms;

  while (!g_should_exit)
    {
      long now;
      long delta;
      long elapsed;
      long remain;
      RacingInput input;
      RacingMenuSelection menu_selection;
      bool touch_boost;

      now = monotonic_ms();
      delta = now - last_ms;
      last_ms = now;
      if (delta < 1)
        {
          delta = 1;
        }
      if (delta > 100)
        {
          delta = 100;
        }

      racing_input_poll();
      jy60_poll();
      jy60_get_sample(&jy60_sample);
      input = racing_input_get();
      menu_selection = racing_input_get_menu_selection();
      touch_boost = racing_input_get_touch_boost();

      if (app_mode == RACING_APP_MENU)
        {
          if (menu_selection == RACING_MENU_ORIGINAL)
            {
              app_mode = RACING_APP_ORIGINAL;
              racing_game_start(&game);
              racing_input_reset();
              printf("[RACING] Original mode selected.\n");
            }
          else if (menu_selection == RACING_MENU_GYRO)
            {
              app_mode = RACING_APP_GYRO;
              racing_game_start(&game);
              jy60_control_calibrate(&jy60_control, &jy60_sample);
              racing_input_reset();
              printf("[RACING] Gyro mode selected. Calibration valid=%d "
                     "roll=%.2f pitch=%.2f yaw=%.2f\n",
                     (int)jy60_control.ready, jy60_control.roll_zero,
                     jy60_control.pitch_zero, jy60_control.yaw_zero);
            }
          else if (menu_selection == RACING_MENU_TEST)
            {
              app_mode = RACING_APP_TEST;
              racing_input_reset();
              printf("[RACING] Test mode selected.\n");
            }
          else if (menu_selection == RACING_MENU_MAP_CYCLE)
            {
              /* 触摸菜单顶部：循环切换地图（环形 → 一路邮你 → Z/S → 环形）。 */
              int next_map = (game.mapIndex + 1) % RACING_MAP_COUNT;
              racing_game_set_map(&game, next_map);
              racing_input_reset();
              printf("[RACING] map -> %d (%s)\n", next_map,
                     racing_game_map_name_ascii(next_map));
            }

          racing_fb_render_menu(view);
        }
      else if (app_mode == RACING_APP_TEST)
        {
          if (input.start || input.pause)
            {
              app_mode = RACING_APP_MENU;
              racing_input_reset();
              printf("[RACING] Back to menu.\n");
            }
          else
            {
              racing_fb_render_gyro_test(view, &jy60_sample);
            }
        }
      else
        {
          input.accelerate = true;   /* 固定速度自动前进(删前进/后退键) */
          input.brake = false;

          if (app_mode == RACING_APP_GYRO)
            {
              if (!jy60_control.ready && jy60_sample.valid)
                {
                  jy60_control_calibrate(&jy60_control, &jy60_sample);
                  printf("[RACING-JY60] calibrated roll=%.2f pitch=%.2f yaw=%.2f\n",
                         jy60_control.roll_zero, jy60_control.pitch_zero,
                         jy60_control.yaw_zero);
                }
              jy60_apply_control(&jy60_control, &jy60_sample, &input);
              input.boost = touch_boost && game.energy > 0;
            }

          racing_game_update(&game, &input, (int)delta);
          racing_fb_render(view);
        }

      /* 每秒打印一次:帧率、平均帧耗时、游戏状态、上一帧输入轴。
       * 用来量化卡顿 + 确认触摸是否到达游戏层。 */
      frames++;
      {
        long stat_now = monotonic_ms();
        long ms = stat_now - stat_last;
        if (ms >= 1000)
          {
            printf("[RACING] %dfps avg=%ldms mode=%d lap=%d camZ=%d energy=%d "
                   "app=%d in:acc=%d brk=%d L=%d R=%d bst=%d fly=%d "
                   "jy60:ok=%d r=%.1f p=%.1f y=%.1f gz=%.1f\n",
                   frames * 1000 / (int)ms, ms / (frames > 0 ? frames : 1),
                   game.mode, game.lap, game.camZ, game.energy,
                   app_mode,
                   (int)input.accelerate, (int)input.brake, (int)input.left,
                   (int)input.right, (int)input.boost, (int)input.fly,
                   (int)jy60_sample.valid, jy60_sample.roll_deg,
                   jy60_sample.pitch_deg, jy60_sample.yaw_deg,
                   jy60_sample.gyro_z_dps);
            frames = 0;
            stat_last = stat_now;
          }
      }

      /* 帧率限制到 RACING_TARGET_FRAME_MS(~30FPS),让出 CPU。 */
      elapsed = monotonic_ms() - now;
      remain = RACING_TARGET_FRAME_MS - elapsed;
      if (remain > 0)
        {
          usleep((useconds_t)remain * 1000);
        }
      else
        {
          usleep(1000);
        }
    }

  printf("[RACING] exiting.\n");
  jy60_deinit();
  racing_input_deinit();
  racing_fb_delete(view);
  return 0;
}
