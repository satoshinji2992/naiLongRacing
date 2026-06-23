#ifndef RACING_RENDER_FB_H
#define RACING_RENDER_FB_H

#include "game.h"

/*
 * Framebuffer 直绘渲染层。
 *
 * 不依赖 LVGL：直接 open("/dev/fb0") + mmap，把场景按几何图元
 * （矩形 / 三角形）写进 framebuffer。本层只负责绘制，帧节拍和
 * 状态推进交给上层主循环。
 */

typedef struct RacingFbView RacingFbView;

/* 打开 fb 设备、mmap、探测双缓冲。失败返回 NULL。 */
RacingFbView *racing_fb_create(RacingGame *game);

/* munmap、关闭设备、释放资源。 */
void racing_fb_delete(RacingFbView *view);

/* 读取 game 状态画一帧（天空 + 路面 + 奶龙），并翻页/刷新到屏幕。 */
void racing_fb_render(RacingFbView *view);

#endif
