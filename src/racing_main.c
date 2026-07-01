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
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game.h"
#include "jy60.h"
#include "render_fb.h"
#include "racing_input.h"
#include "racing_voice.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#undef NEED_BOARDINIT

#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

#ifndef CONFIG_EXAMPLES_RACING_VOICE_START_SCRIPT
#  define CONFIG_EXAMPLES_RACING_VOICE_START_SCRIPT ""
#endif

#ifndef CONFIG_SYSTEM_SYSTEM
#  define CONFIG_SYSTEM_SYSTEM 0
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

static void launch_voice_script(void)
{
#if defined(CONFIG_EXAMPLES_RACING_VOICE) && CONFIG_EXAMPLES_RACING_VOICE
#  if CONFIG_SYSTEM_SYSTEM
  int ret;

  if (CONFIG_EXAMPLES_RACING_VOICE_START_SCRIPT[0] == '\0')
    {
      printf("[RACING] voice bridge should be started before racing.\n");
      return;
    }

  printf("[RACING] launch XiaoZhi script: %s\n",
         CONFIG_EXAMPLES_RACING_VOICE_START_SCRIPT);
  ret = system(CONFIG_EXAMPLES_RACING_VOICE_START_SCRIPT);
  printf("[RACING] XiaoZhi script exit=%d\n", ret);
#  else
  printf("[RACING] CONFIG_SYSTEM_SYSTEM is disabled; run the script from NSH.\n");
#  endif
#else
  printf("[RACING] voice disabled; enable CONFIG_EXAMPLES_RACING_VOICE first.\n");
#endif
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
  racing_voice_init();
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

      /* 菜单屏:按当前 game.mode 映射触摸;游戏/暂停传 -1(驾驶按模式驱动)。
       * 不能传 0:RACING_MODE_START==0,会和主菜单冲突导致游戏中触摸失效。 */
      racing_input_set_menu_screen(app_mode == RACING_APP_MENU ? (int)game.mode : -1);
      racing_input_set_drive_mode(app_mode == RACING_APP_GYRO);
      racing_input_poll();
      racing_voice_poll();
      jy60_poll();
      jy60_get_sample(&jy60_sample);
      input = racing_input_get();
      racing_voice_apply_input(&input);
      game.voiceState = racing_voice_state();
      snprintf(game.voiceText, sizeof(game.voiceText), "%s",
               racing_voice_last_text());

      if (app_mode == RACING_APP_MENU)
        {
          /* 主菜单按"返回"(按键②) -> 退出赛车,回 NSH(替代 Ctrl-C)。 */
          if (game.mode == RACING_MODE_START && input.back)
            {
              g_should_exit = 1;
              printf("[RACING] quit to NSH (main-menu back).\n");
            }
          /* 操作选择:点哪个按钮选哪个(ctrl1/2/3 由触摸按 y 给出)。 */
          else if (game.mode == RACING_MODE_CONTROL_SELECT && input.ctrl1)
            {
              game.menuControlMode = 0;
              game.mode = RACING_MODE_START;
              racing_input_reset();
              printf("[RACING] control -> Original.\n");
            }
          else if (game.mode == RACING_MODE_CONTROL_SELECT && input.ctrl2)
            {
              game.menuControlMode = 1;
              game.mode = RACING_MODE_START;
              racing_input_reset();
              printf("[RACING] control -> Gyro.\n");
            }
          else if (game.mode == RACING_MODE_CONTROL_SELECT && input.ctrl3)
            {
              game.menuControlMode = 2;
              app_mode = RACING_APP_TEST;
              game.mode = RACING_MODE_START;
              racing_input_reset();
              printf("[RACING] Test mode (from control select).\n");
            }
          else if (game.mode == RACING_MODE_NETWORK_SELECT && input.start)
            {
              launch_voice_script();
              racing_input_reset();
              racing_fb_render_menu(view);
            }
          else if ((game.mode == RACING_MODE_START ||
                    game.mode == RACING_MODE_CONTROL_SELECT) && input.ctrl1)
            {
              game.menuControlMode = 0;
              game.mode = RACING_MODE_START;
              racing_input_reset();
              printf("[RACING] control -> Original.\n");
            }
          else if ((game.mode == RACING_MODE_START ||
                    game.mode == RACING_MODE_CONTROL_SELECT) && input.ctrl2)
            {
              game.menuControlMode = 1;
              game.mode = RACING_MODE_START;
              racing_input_reset();
              printf("[RACING] control -> Gyro.\n");
            }
          else if ((game.mode == RACING_MODE_START ||
                    game.mode == RACING_MODE_CONTROL_SELECT) && input.ctrl3)
            {
              game.menuControlMode = 2;
              app_mode = RACING_APP_TEST;
              game.mode = RACING_MODE_START;
              racing_input_reset();
              printf("[RACING] Test mode.\n");
            }
          else
            {
              racing_game_update(&game, &input, (int)delta);
              if (game.mode == RACING_MODE_PLAYING)
                {
                  /* 主菜单快捷开始或关卡选择里按开始 -> 用当前操作模式发车。 */
                  app_mode = (game.menuControlMode == 1) ? RACING_APP_GYRO
                                                          : RACING_APP_ORIGINAL;
                  if (app_mode == RACING_APP_GYRO)
                    {
                      jy60_control_calibrate(&jy60_control, &jy60_sample);
                    }
                  racing_input_reset();
                  printf("[RACING] start %s mode, map %d (%s).\n",
                         app_mode == RACING_APP_GYRO ? "Gyro" : "Original",
                         game.mapIndex, racing_game_map_name_ascii(game.mapIndex));
                }
              else
                {
                  racing_fb_render_menu(view);
                }
            }
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
          /* 车始终自动前进;转向靠触摸(普通)或 JY60(体感);
           * boost 靠触摸按模式驱动(已由 input 层填好 input.boost)。 */
          input.accelerate = true;
          input.brake = false;
          input.boost = input.boost || input.voiceBoost;

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
            }

          racing_game_update(&game, &input, (int)delta);
          if (game.mode == RACING_MODE_START)
            {
              /* 暂停/胜利界面选"回主菜单" -> 回主菜单。 */
              app_mode = RACING_APP_MENU;
              racing_input_reset();
              printf("[RACING] Back to menu.\n");
            }
          else
            {
              racing_fb_render(view);
            }
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
  racing_voice_deinit();
  racing_input_deinit();
  racing_fb_delete(view);
  return 0;
}
