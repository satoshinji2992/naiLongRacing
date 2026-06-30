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
#include <time.h>

#include "game.h"
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
  RacingFbView *view;
  RacingGame *game;
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

  /* RacingGame ~105KB(roads/trees/houses 等),放堆上,别把任务栈(320KB)撑爆。 */
  game = (RacingGame *)calloc(1, sizeof(*game));
  if (game == NULL)
    {
      fprintf(stderr, "[RACING] out of memory\n");
      return 1;
    }
  racing_game_init(game, (unsigned int)seed);

  view = racing_fb_create(game);
  if (view == NULL)
    {
      fprintf(stderr, "[RACING] failed to open framebuffer\n");
      return 1;
    }

  racing_input_init();

  printf("[RACING] running. Tap center / press button1 to start.\n");

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

      racing_input_poll();
      input = racing_input_get();
      input.accelerate = true;   /* 固定速度自动前进(删前进/后退键) */
      input.brake = false;
      racing_game_update(game, &input, (int)delta);
      racing_fb_render(view);

      /* 每秒打印一次:帧率、平均帧耗时、游戏状态、上一帧输入轴。
       * 用来量化卡顿 + 确认触摸是否到达游戏层。 */
      frames++;
      {
        long stat_now = monotonic_ms();
        long ms = stat_now - stat_last;
        if (ms >= 1000)
          {
            printf("[RACING] %dfps avg=%ldms mode=%d lap=%d camZ=%d energy=%d "
                   "in:acc=%d brk=%d L=%d R=%d bst=%d fly=%d\n",
                   frames * 1000 / (int)ms, ms / (frames > 0 ? frames : 1),
                   game->mode, game->lap, game->camZ, game->energy,
                   (int)input.accelerate, (int)input.brake, (int)input.left,
                   (int)input.right, (int)input.boost, (int)input.fly);
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
  racing_input_deinit();
  racing_fb_delete(view);
  free(game);
  return 0;
}
