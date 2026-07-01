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
static char g_pending_command[sizeof(g_last_text)];
static int g_voice_state = VOICE_STATE_UNKNOWN;
static long g_next_accept_ms;
static long g_voice_boost_until_ms;

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

static bool is_nailong_phrase(const char *text)
{
  if (text == NULL)
    {
      return false;
    }

  return strcmp(text, "我是奶龙") == 0 ||
         strcmp(text, "我是奶龙。") == 0 ||
         strcmp(text, "我是奶龙！") == 0 ||
         strcmp(text, "我是奶龙!") == 0;
}

static bool is_boost_phrase(const char *text)
{
  if (text == NULL)
    {
      return false;
    }

  return strcmp(text, "加速") == 0 ||
         strcmp(text, "加速。") == 0 ||
         strcmp(text, "加速！") == 0 ||
         strcmp(text, "加速!") == 0;
}

static bool voice_mark_command(const char *text)
{
  if (text == NULL || text[0] == '\0')
    {
      return false;
    }

  /* 唯一识别词:"我是奶龙" -> 开启无限能量作弊(充满十格 + 黄色显示)。 */
  if (is_nailong_phrase(text))
    {
      g_voice_input.nailongCheat = true;
      snprintf(g_pending_command, sizeof(g_pending_command), "%s", text);
      return true;
    }

  if (is_boost_phrase(text))
    {
      long now = voice_monotonic_ms();

      g_voice_input.voiceBoost = true;
      g_voice_boost_until_ms = now + 2500;
      snprintf(g_pending_command, sizeof(g_pending_command), "%s", text);
      return true;
    }

  return false;
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
  char type[16];
  int state;
  long now = voice_monotonic_ms();
  bool is_stt = false;
  bool has_type = false;

  if (json_get_int(buffer, "state", &state) == 0)
    {
      g_voice_state = state;
    }

  if (json_get_string(buffer, "type", type, sizeof(type)) == 0)
    {
      has_type = true;
      is_stt = strcmp(type, "stt") == 0;
    }

  if (json_get_string(buffer, "text", text, sizeof(text)) == 0)
    {
      snprintf(g_last_text, sizeof(g_last_text), "%s", text);
      if (!has_type)
        {
          printf("[RACING-VOICE] legacy text ignored: %s\n", text);
        }
      else if (!is_stt)
        {
          printf("[RACING-VOICE] %s text: %s\n", type, text);
        }
      else if (now >= g_next_accept_ms)
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
  bool new_command;
  bool boost_active;

  if (input == NULL)
    {
      return;
    }

  new_command = g_voice_input.nailongCheat || g_voice_input.voiceBoost;
  boost_active = g_voice_boost_until_ms > voice_monotonic_ms();

  if (boost_active)
    {
      g_voice_input.voiceBoost = true;
    }

  /* 语音只处理命令词,其它输入都由触摸/按键产生。 */
  if (new_command)
    {
      printf("[RACING-VOICE] apply command: %s\n", g_pending_command);
    }
  input->nailongCheat = input->nailongCheat || g_voice_input.nailongCheat;
  input->voiceBoost = input->voiceBoost || g_voice_input.voiceBoost;

  memset(&g_voice_input, 0, sizeof(g_voice_input));
  if (!boost_active)
    {
      g_pending_command[0] = '\0';
    }
}

const char *racing_voice_last_text(void)
{
  return g_last_text;
}

int racing_voice_state(void)
{
  return g_voice_state;
}
