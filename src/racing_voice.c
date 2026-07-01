#include "racing_voice.h"

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef CONFIG_EXAMPLES_RACING_VOICE
#define CONFIG_EXAMPLES_RACING_VOICE 0
#endif

#ifndef CONFIG_EXAMPLES_RACING_VOICE_LOCAL_PORT
#define CONFIG_EXAMPLES_RACING_VOICE_LOCAL_PORT 5679
#endif

#ifndef CONFIG_EXAMPLES_RACING_VOICE_REMOTE_PORT
#define CONFIG_EXAMPLES_RACING_VOICE_REMOTE_PORT 5678
#endif

typedef enum VoiceDeviceState
{
  VOICE_STATE_UNKNOWN = 0,
  VOICE_STATE_STARTING,
  VOICE_STATE_WIFI_CONFIGURING,
  VOICE_STATE_IDLE,
  VOICE_STATE_CONNECTING,
  VOICE_STATE_LISTENING,
  VOICE_STATE_SPEAKING,
  VOICE_STATE_UPGRADING,
  VOICE_STATE_ACTIVATING,
  VOICE_STATE_FATAL_ERROR
} VoiceDeviceState;

static int g_voice_fd = -1;
static struct sockaddr_in g_voice_remote;
static RacingInput g_voice_input;
static char g_last_text[128];
static int g_voice_state = VOICE_STATE_UNKNOWN;
static long g_next_accept_ms;

static long voice_monotonic_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void voice_send_state(const char *state)
{
#if CONFIG_EXAMPLES_RACING_VOICE
  if (g_voice_fd >= 0 && state != NULL)
    {
      sendto(g_voice_fd, state, strlen(state) + 1, 0,
             (struct sockaddr *)&g_voice_remote, sizeof(g_voice_remote));
    }
#else
  (void)state;
#endif
}

static bool contains_any(const char *text, const char *const *words, int count)
{
  int i;

  if (text == NULL)
    {
      return false;
    }

  for (i = 0; i < count; i++)
    {
      if (strstr(text, words[i]) != NULL)
        {
          return true;
        }
    }

  return false;
}

static bool voice_mark_command(const char *text)
{
  static const char *const start_words[] =
    { "开始", "出发", "继续", "启动", "开跑", "发车" };
  static const char *const pause_words[] =
    { "暂停", "停一下", "等一下", "先停" };
  static const char *const restart_words[] =
    { "重来", "重新开始", "再来一局", "重新跑" };
  static const char *const menu_words[] =
    { "回菜单", "主菜单", "返回菜单", "回主页" };
  static const char *const back_words[] =
    { "返回", "回去", "上一页" };
  static const char *const map_select_words[] =
    { "选地图", "选择地图", "地图选择", "关卡选择", "换地图" };
  static const char *const control_select_words[] =
    { "选操作", "选择操作", "操作选择", "选择模式", "控制方式",
      "操控方式" };
  static const char *const network_select_words[] =
    { "网络", "联网", "连接网络", "网络设置", "小智", "语音设置",
      "启动语音" };
  static const char *const prev_words[] =
    { "上一个", "上个", "上一项", "上一张" };
  static const char *const next_words[] =
    { "下一个", "下个", "下一项", "下一张" };
  static const char *const easy_words[] =
    { "简单", "容易", "第一张", "地图一", "一号地图" };
  static const char *const medium_words[] =
    { "中等", "普通", "第二张", "地图二", "二号地图" };
  static const char *const hard_words[] =
    { "困难", "难", "第三张", "地图三", "三号地图" };
  static const char *const original_words[] =
    { "原始模式", "普通模式", "触摸模式", "手动模式", "普通操作" };
  static const char *const gyro_words[] =
    { "陀螺仪", "体感", "体感模式", "重力感应", "陀螺仪模式" };
  static const char *const test_words[] =
    { "测试模式", "传感器测试", "陀螺仪测试" };
  static const char *const boost_words[] =
    { "加速", "冲刺", "氮气", "boost" };
  static const char *const fly_words[] =
    { "飞", "起飞", "飞行", "跳" };
  bool matched = false;

  if (text == NULL || text[0] == '\0')
    {
      return false;
    }

  if (contains_any(text, menu_words, sizeof(menu_words) / sizeof(menu_words[0])))
    {
      g_voice_input.toMenu = true;
      g_voice_input.back = true;
      matched = true;
    }
  else if (contains_any(text, back_words, sizeof(back_words) / sizeof(back_words[0])))
    {
      g_voice_input.back = true;
      matched = true;
    }
  if (contains_any(text, restart_words, sizeof(restart_words) / sizeof(restart_words[0])))
    {
      g_voice_input.restart = true;
      matched = true;
    }
  if (contains_any(text, pause_words, sizeof(pause_words) / sizeof(pause_words[0])))
    {
      g_voice_input.pause = true;
      matched = true;
    }
  if (contains_any(text, start_words, sizeof(start_words) / sizeof(start_words[0])))
    {
      g_voice_input.start = true;
      matched = true;
    }

  if (contains_any(text, map_select_words,
                   sizeof(map_select_words) / sizeof(map_select_words[0])))
    {
      g_voice_input.mapSelect = true;
      matched = true;
    }
  if (contains_any(text, control_select_words,
                   sizeof(control_select_words) / sizeof(control_select_words[0])))
    {
      g_voice_input.controlSelect = true;
      matched = true;
    }
  if (contains_any(text, network_select_words,
                   sizeof(network_select_words) / sizeof(network_select_words[0])))
    {
      g_voice_input.networkSelect = true;
      matched = true;
    }
  if (contains_any(text, prev_words, sizeof(prev_words) / sizeof(prev_words[0])))
    {
      g_voice_input.cyclePrev = true;
      matched = true;
    }
  if (contains_any(text, next_words, sizeof(next_words) / sizeof(next_words[0])))
    {
      g_voice_input.cycleNext = true;
      matched = true;
    }

  if (contains_any(text, easy_words, sizeof(easy_words) / sizeof(easy_words[0])))
    {
      g_voice_input.map1 = true;
      matched = true;
    }
  else if (contains_any(text, medium_words, sizeof(medium_words) / sizeof(medium_words[0])))
    {
      g_voice_input.map2 = true;
      matched = true;
    }
  else if (contains_any(text, hard_words, sizeof(hard_words) / sizeof(hard_words[0])))
    {
      g_voice_input.map3 = true;
      matched = true;
    }

  if (contains_any(text, original_words, sizeof(original_words) / sizeof(original_words[0])))
    {
      g_voice_input.controlSelect = true;
      g_voice_input.ctrl1 = true;
      matched = true;
    }
  else if (contains_any(text, gyro_words, sizeof(gyro_words) / sizeof(gyro_words[0])))
    {
      g_voice_input.controlSelect = true;
      g_voice_input.ctrl2 = true;
      matched = true;
    }
  else if (contains_any(text, test_words, sizeof(test_words) / sizeof(test_words[0])))
    {
      g_voice_input.controlSelect = true;
      g_voice_input.ctrl3 = true;
      matched = true;
    }

  if (contains_any(text, boost_words, sizeof(boost_words) / sizeof(boost_words[0])))
    {
      g_voice_input.boost = true;
      matched = true;
    }
  if (contains_any(text, fly_words, sizeof(fly_words) / sizeof(fly_words[0])))
    {
      g_voice_input.fly = true;
      matched = true;
    }

  return matched;
}

static int json_get_string(const char *json, const char *key, char *out, size_t outlen)
{
  char pattern[32];
  const char *p;
  const char *q;
  size_t n = 0;

  if (json == NULL || key == NULL || out == NULL || outlen == 0)
    {
      return -1;
    }

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  p = strstr(json, pattern);
  if (p == NULL)
    {
      return -1;
    }

  p = strchr(p + strlen(pattern), ':');
  if (p == NULL)
    {
      return -1;
    }
  p++;
  while (*p == ' ' || *p == '\t')
    {
      p++;
    }
  if (*p != '"')
    {
      return -1;
    }
  p++;
  q = p;
  while (*q != '\0' && *q != '"')
    {
      if (*q == '\\' && q[1] != '\0')
        {
          q++;
        }
      if (n + 1 < outlen)
        {
          out[n++] = *q;
        }
      q++;
    }

  out[n] = '\0';
  return *q == '"' ? 0 : -1;
}

static int json_get_int(const char *json, const char *key, int *value)
{
  char pattern[32];
  const char *p;

  if (json == NULL || key == NULL || value == NULL)
    {
      return -1;
    }

  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  p = strstr(json, pattern);
  if (p == NULL)
    {
      return -1;
    }

  p = strchr(p + strlen(pattern), ':');
  if (p == NULL)
    {
      return -1;
    }

  *value = atoi(p + 1);
  return 0;
}

static void voice_process_json(char *buffer)
{
  char text[sizeof(g_last_text)];
  int state;
  long now = voice_monotonic_ms();

  if (json_get_int(buffer, "state", &state) == 0)
    {
      g_voice_state = state;
    }

  if (json_get_string(buffer, "text", text, sizeof(text)) == 0)
    {
      snprintf(g_last_text, sizeof(g_last_text), "%s", text);
      if (now >= g_next_accept_ms)
        {
          if (voice_mark_command(text))
            {
              g_next_accept_ms = now + 1800;
              printf("[RACING-VOICE] command: %s\n", text);
            }
          else
            {
              printf("[RACING-VOICE] text: %s\n", text);
            }
        }
      else
        {
          printf("[RACING-VOICE] cooldown text: %s\n", text);
        }
    }
}

void racing_voice_init(void)
{
#if CONFIG_EXAMPLES_RACING_VOICE
  struct sockaddr_in local;
  int flags;

  if (g_voice_fd >= 0)
    {
      return;
    }

  g_voice_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (g_voice_fd < 0)
    {
      printf("[RACING-VOICE] socket failed errno=%d\n", errno);
      return;
    }

  memset(&local, 0, sizeof(local));
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  local.sin_port = htons(CONFIG_EXAMPLES_RACING_VOICE_LOCAL_PORT);

  if (bind(g_voice_fd, (struct sockaddr *)&local, sizeof(local)) < 0)
    {
      printf("[RACING-VOICE] bind 127.0.0.1:%d failed errno=%d\n",
             CONFIG_EXAMPLES_RACING_VOICE_LOCAL_PORT, errno);
      close(g_voice_fd);
      g_voice_fd = -1;
      return;
    }

  flags = fcntl(g_voice_fd, F_GETFL, 0);
  if (flags >= 0)
    {
      fcntl(g_voice_fd, F_SETFL, flags | O_NONBLOCK);
    }

  memset(&g_voice_remote, 0, sizeof(g_voice_remote));
  g_voice_remote.sin_family = AF_INET;
  g_voice_remote.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  g_voice_remote.sin_port = htons(CONFIG_EXAMPLES_RACING_VOICE_REMOTE_PORT);

  printf("[RACING-VOICE] listening 127.0.0.1:%d, control_center=%d\n",
         CONFIG_EXAMPLES_RACING_VOICE_LOCAL_PORT,
         CONFIG_EXAMPLES_RACING_VOICE_REMOTE_PORT);
  voice_send_state("listening");
#endif
}

void racing_voice_deinit(void)
{
#if CONFIG_EXAMPLES_RACING_VOICE
  if (g_voice_fd >= 0)
    {
      voice_send_state("standby");
      close(g_voice_fd);
      g_voice_fd = -1;
    }
#endif
  memset(&g_voice_input, 0, sizeof(g_voice_input));
}

void racing_voice_poll(void)
{
#if CONFIG_EXAMPLES_RACING_VOICE
  char buffer[256];
  ssize_t nread;

  if (g_voice_fd < 0)
    {
      return;
    }

  while ((nread = recv(g_voice_fd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
      buffer[nread] = '\0';
      voice_process_json(buffer);
    }
#endif
}

void racing_voice_apply_input(RacingInput *input)
{
  if (input == NULL)
    {
      return;
    }

  input->boost = input->boost || g_voice_input.boost;
  input->fly = input->fly || g_voice_input.fly;
  input->start = input->start || g_voice_input.start;
  input->restart = input->restart || g_voice_input.restart;
  input->pause = input->pause || g_voice_input.pause;
  input->map1 = input->map1 || g_voice_input.map1;
  input->map2 = input->map2 || g_voice_input.map2;
  input->map3 = input->map3 || g_voice_input.map3;
  input->mapSelect = input->mapSelect || g_voice_input.mapSelect;
  input->controlSelect = input->controlSelect || g_voice_input.controlSelect;
  input->networkSelect = input->networkSelect || g_voice_input.networkSelect;
  input->back = input->back || g_voice_input.back;
  input->cyclePrev = input->cyclePrev || g_voice_input.cyclePrev;
  input->cycleNext = input->cycleNext || g_voice_input.cycleNext;
  input->toMenu = input->toMenu || g_voice_input.toMenu;
  input->ctrl1 = input->ctrl1 || g_voice_input.ctrl1;
  input->ctrl2 = input->ctrl2 || g_voice_input.ctrl2;
  input->ctrl3 = input->ctrl3 || g_voice_input.ctrl3;

  memset(&g_voice_input, 0, sizeof(g_voice_input));
}

const char *racing_voice_last_text(void)
{
  return g_last_text;
}

int racing_voice_state(void)
{
  return g_voice_state;
}
