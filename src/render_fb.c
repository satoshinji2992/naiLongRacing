#include "render_fb.h"
#include "font.h"

#include <nuttx/config.h>
#include <nuttx/video/fb.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <math.h>
#include <time.h>

#ifndef CONFIG_EXAMPLES_RACING_FB_DEVPATH
#define CONFIG_EXAMPLES_RACING_FB_DEVPATH "/dev/fb0"
#endif

#ifndef CONFIG_EXAMPLES_RACING_DATA_ROOT
#define CONFIG_EXAMPLES_RACING_DATA_ROOT "/data"
#endif

/* 板端资源目录(usrdata 分区挂载点 /data)。 */
#define RACING_RES_ROOT CONFIG_EXAMPLES_RACING_DATA_ROOT "/res/racing"

/* 自定义 raw 贴图格式头(小端)。 */
#define RACING_IMG_MAGIC 0x52414742u

typedef struct {
    uint16_t w;
    uint16_t h;
    uint8_t *px;          /* RGBA，w*h*4 字节，逐行自上而下 */
} RacingImg;

/* 场景使用的几种颜色（RGB888）。 */
#define COL_SKY      0x5fb8e8u
#define COL_GRASS       0x00c700u   /* 默认草地(绿);地图2(bg_bupt)用深灰 */
#define COL_GRASS_DARK  0x2e3338u   /* 地图2(bg_bupt)草地:深灰 */
#define COL_ROAD_DK  0x696969u
#define COL_ROAD_LT  0x656565u
#define COL_POLE     0xeb2d18u

typedef struct {
    float x;
    float y;
} FbPoint;

typedef struct {
    float x;
    float y;
    float u;
    float v;
} FbTexPoint;

/* 投影上下文，与 game.c 里那套摄像机投影一一对应。 */
typedef struct ProjectionContext {
    int camX;
    int camY;
    int camZ;
    float sinAngle;
    float cosAngle;
} ProjectionContext;

struct RacingFbView {
    RacingGame *game;
    int fd;
    struct fb_videoinfo_s vinfo;
    struct fb_planeinfo_s pinfo;
    void *fbmem;
    void *fbmem2;        /* 双缓冲时的第二块，否则 NULL */
    void *act_fbmem;     /* 当前绘制目标 */
    uint32_t mem2_yoffset;
    int double_buffered;
    int draw_w;          /* 实际可绘制宽度 = min(WIN_WIDTH, xres) */
    int draw_h;          /* 实际可绘制高度 = min(WIN_HEIGHT, yres) */
    RacingImg *img_nailong;
    RacingImg *img_car;
    RacingImg *img_head;
    RacingImg *img_mtn_far;
    RacingImg *img_mtn_near;
    RacingImg *img_cloud;
    RacingImg *img_nailong_bupt;   /* 地图“一路邮你”：北邮校徽收集物 */
    RacingImg *img_bg_bupt;        /* 地图“一路邮你”：北邮校门背景 */
    RacingImg *img_tree;           /* 路边树 */
    RacingImg *img_house_a;        /* 房子 A 面：垂直于路 */
    RacingImg *img_house_b;        /* 房子 B 面：平行于路 */
};

/****************************************************************************
 * fb 后端：探测 planeinfo / 建立第二缓冲 / 翻页。逻辑移植自 fb_main.c。
 ****************************************************************************/

static int fbdev_get_pinfo(int fd, struct fb_planeinfo_s *pinfo)
{
    if (ioctl(fd, FBIOGET_PLANEINFO, (unsigned long)((uintptr_t)pinfo)) < 0) {
        fprintf(stderr, "[RACING] ioctl(FBIOGET_PLANEINFO) failed: %d\n", errno);
        return -1;
    }

    printf("[RACING] PlaneInfo: fblen=%zu stride=%u bpp=%u display=%u\n",
           (size_t)pinfo->fblen, pinfo->stride, pinfo->bpp, pinfo->display);

    if (pinfo->bpp != 32 && pinfo->bpp != 24 &&
        pinfo->bpp != 16 && pinfo->bpp != 8 && pinfo->bpp != 1) {
        fprintf(stderr, "[RACING] unsupported bpp=%u\n", pinfo->bpp);
        return -1;
    }
    return 0;
}

static int fb_init_mem2(RacingFbView *v)
{
    struct fb_planeinfo_s pinfo;
    uintptr_t buf_offset;

    memset(&pinfo, 0, sizeof(pinfo));
    pinfo.display = v->pinfo.display + 1;

    if (fbdev_get_pinfo(v->fd, &pinfo) < 0) {
        return -1;
    }

    if (pinfo.bpp != v->pinfo.bpp) {
        fprintf(stderr, "[RACING] fbmem2 bpp mismatch\n");
        return -1;
    }

    buf_offset = (uintptr_t)pinfo.fbmem - (uintptr_t)v->fbmem;
    if ((buf_offset % v->pinfo.stride) != 0) {
        fprintf(stderr, "[RACING] buf_offset not divisible by stride\n");
    }

    if (buf_offset == 0) {
        /* 连续的第二缓冲：紧接在第一块后面。 */
        v->mem2_yoffset = v->vinfo.yres;
        v->fbmem2 = (uint8_t *)pinfo.fbmem + v->mem2_yoffset * pinfo.stride;
    } else {
        v->mem2_yoffset = buf_offset / v->pinfo.stride;
        v->fbmem2 = pinfo.fbmem;
    }

    printf("[RACING] fbmem2=%p yoffset=%" PRIu32 "\n", v->fbmem2, v->mem2_yoffset);
    return 0;
}

/* 把当前绘制缓冲翻到屏幕上。SPI LCD 走 FBIO_UPDATE，双缓冲走 PAN。 */
static void fb_present(RacingFbView *v)
{
#ifdef CONFIG_FB_UPDATE
    struct fb_area_s area;
    area.x = 0;
    area.y = (v->act_fbmem == v->fbmem) ? 0 : v->mem2_yoffset;
    area.w = v->draw_w;
    area.h = v->draw_h;
    ioctl(v->fd, FBIO_UPDATE, (unsigned long)((uintptr_t)&area));
#endif

    if (v->double_buffered) {
        struct pollfd pfd;
        pfd.fd = v->fd;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, 0) > 0) {
            v->pinfo.yoffset = (v->act_fbmem == v->fbmem) ? 0 : v->mem2_yoffset;
            ioctl(v->fd, FBIOPAN_DISPLAY, (unsigned long)((uintptr_t)&v->pinfo));
            v->act_fbmem = (v->act_fbmem == v->fbmem) ? v->fbmem2 : v->fbmem;
        }
    }
}

/****************************************************************************
 * 像素原语：按 fb 的 bpp 写入。内部色值统一用 RGB888 (0xRRGGBB)。
 ****************************************************************************/

static uint16_t rgb_to_565(uint32_t rgb)
{
    uint8_t r = (rgb >> 16) & 0xff;
    uint8_t g = (rgb >> 8) & 0xff;
    uint8_t b = rgb & 0xff;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* 填充一行 [x0,x1]，inclusive。fast-path 走 32/16/24，8/1 回退逐像素。 */
static void fb_fill_row(RacingFbView *v, int y, int x0, int x1, uint32_t rgb)
{
    uint8_t *row = (uint8_t *)v->act_fbmem + v->pinfo.stride * (uint32_t)y;

    switch (v->pinfo.bpp) {
    case 32: {
        uint32_t *dp = (uint32_t *)row + x0;
        uint32_t c = rgb | 0xff000000u;
        for (int x = x0; x <= x1; x++) {
            *dp++ = c;
        }
        break;
    }
    case 24: {
        uint8_t *dp = row + x0 * 3;
        for (int x = x0; x <= x1; x++) {
            *dp++ = (uint8_t)(rgb);
            *dp++ = (uint8_t)(rgb >> 8);
            *dp++ = (uint8_t)(rgb >> 16);
        }
        break;
    }
    case 16: {
        uint16_t *dp = (uint16_t *)row + x0;
        uint16_t c = rgb_to_565(rgb);
        for (int x = x0; x <= x1; x++) {
            *dp++ = c;
        }
        break;
    }
    case 8: {
        uint8_t lum = (uint8_t)((((rgb >> 16) & 0xff) * 30 +
                                 ((rgb >> 8) & 0xff) * 59 +
                                 (rgb & 0xff) * 11) / 100);
        memset(row + x0, lum, (size_t)(x1 - x0 + 1));
        break;
    }
    case 1: {
        uint8_t lum = (uint8_t)((((rgb >> 16) & 0xff) +
                                 ((rgb >> 8) & 0xff) +
                                 (rgb & 0xff)) / 3);
        uint8_t fill = lum > 127 ? 0xff : 0x00;
        for (int x = x0; x <= x1; x++) {
            uint8_t *byte = row + (x >> 3);
            uint8_t mask = (uint8_t)(0x80 >> (x & 7));
            if (fill) {
                *byte |= mask;
            } else {
                *byte &= (uint8_t)~mask;
            }
        }
        break;
    }
    default:
        break;
    }
}

/* 整块 fb 清屏（按真实分辨率，覆盖可能超出 WIN 的边框，避免残留）。 */
static void fb_clear(RacingFbView *v, uint32_t rgb)
{
    int h = (int)v->vinfo.yres;
    for (int y = 0; y < h; y++) {
        fb_fill_row(v, y, 0, (int)v->vinfo.xres - 1, rgb);
    }
}

/* 轴对齐矩形 (x0,y0)-(x1,y1)，inclusive，裁剪到绘制区。 */
static void fb_fill_rect(RacingFbView *v, int x0, int y0, int x1, int y1, uint32_t rgb)
{
    int tmp;
    if (x1 < x0) { tmp = x0; x0 = x1; x1 = tmp; }
    if (y1 < y0) { tmp = y0; y0 = y1; y1 = tmp; }
    if (x1 < 0 || y1 < 0 || x0 >= v->draw_w || y0 >= v->draw_h) {
        return;
    }
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 >= v->draw_w) { x1 = v->draw_w - 1; }
    if (y1 >= v->draw_h) { y1 = v->draw_h - 1; }
    for (int y = y0; y <= y1; y++) {
        fb_fill_row(v, y, x0, x1, rgb);
    }
}

/* 三角形扫描线光栅化：按 y 排序，每行插值左右边界后填 span。 */
static void fb_fill_triangle(RacingFbView *v, FbPoint a, FbPoint b, FbPoint c, uint32_t rgb)
{
    int y;
    int y0;
    int y1;

    /* 按 y 升序排 a<=b<=c。 */
    if (a.y > b.y) { FbPoint t = a; a = b; b = t; }
    if (b.y > c.y) { FbPoint t = b; b = c; c = t; }
    if (a.y > b.y) { FbPoint t = a; a = b; b = t; }

    y0 = (int)ceilf(a.y - 0.5f);
    y1 = (int)floorf(c.y - 0.5f);
    if (y0 < 0) { y0 = 0; }
    if (y1 >= v->draw_h) { y1 = v->draw_h - 1; }

    for (y = y0; y <= y1; y++) {
        float fy = y + 0.5f;
        float xac = (c.y == a.y) ? a.x : a.x + (c.x - a.x) * (fy - a.y) / (c.y - a.y);
        float xother;
        int xl;
        int xr;
        int ix0;
        int ix1;

        if (fy < b.y) {
            xother = (b.y == a.y) ? a.x : a.x + (b.x - a.x) * (fy - a.y) / (b.y - a.y);
        } else {
            xother = (c.y == b.y) ? b.x : b.x + (c.x - b.x) * (fy - b.y) / (c.y - b.y);
        }

        xl = xac < xother ? xac : xother;
        xr = xac < xother ? xother : xac;
        ix0 = (int)ceilf(xl - 0.5f);
        ix1 = (int)floorf(xr - 0.5f);
        if (ix0 < 0) { ix0 = 0; }
        if (ix1 >= v->draw_w) { ix1 = v->draw_w - 1; }
        if (ix0 > ix1) {
            continue;
        }
        fb_fill_row(v, y, ix0, ix1, rgb);
    }
}

/* 任意四边形拆成两个三角形。 */
static void fb_fill_quad(RacingFbView *v, FbPoint p0, FbPoint p1, FbPoint p2, FbPoint p3, uint32_t rgb)
{
    fb_fill_triangle(v, p0, p1, p2, rgb);
    fb_fill_triangle(v, p0, p2, p3, rgb);
}

/****************************************************************************
 * 贴图：读底色 / 写单像素 / alpha 混合 / 缩放 blit / 视差平铺 / 加载。
 ****************************************************************************/

/* 读 (x,y) 处的 native 像素，展开成 RGB888（alpha 混合取底色用）。 */
static uint32_t fb_read_rgb(RacingFbView *v, int x, int y)
{
    uint8_t *row = (uint8_t *)v->act_fbmem + v->pinfo.stride * (uint32_t)y;
    switch (v->pinfo.bpp) {
    case 32:
        return ((uint32_t *)row)[x] & 0xffffffu;
    case 24: {
        uint8_t *p = row + x * 3;
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    }
    case 16: {
        uint16_t c = ((uint16_t *)row)[x];
        uint32_t r = (c >> 11) & 0x1f;
        uint32_t g = (c >> 5) & 0x3f;
        uint32_t b = c & 0x1f;
        return (r << 19) | (g << 10) | (b << 3);
    }
    case 8: {
        uint8_t lum = row[x];
        return (uint32_t)lum | ((uint32_t)lum << 8) | ((uint32_t)lum << 16);
    }
    default:
        return 0;
    }
}

/* 按 fb 的 bpp 写一个 native 像素（rgb 为 0xRRGGBB）。 */
static void fb_put_native(RacingFbView *v, int x, int y, uint32_t rgb)
{
    uint8_t *row = (uint8_t *)v->act_fbmem + v->pinfo.stride * (uint32_t)y;
    switch (v->pinfo.bpp) {
    case 32:
        ((uint32_t *)row)[x] = rgb | 0xff000000u;
        break;
    case 24: {
        uint8_t *p = row + x * 3;
        p[0] = (uint8_t)rgb;
        p[1] = (uint8_t)(rgb >> 8);
        p[2] = (uint8_t)(rgb >> 16);
        break;
    }
    case 16:
        ((uint16_t *)row)[x] = rgb_to_565(rgb);
        break;
    case 8: {
        uint8_t lum = (uint8_t)((((rgb >> 16) & 0xff) * 30 +
                                 ((rgb >> 8) & 0xff) * 59 +
                                 (rgb & 0xff) * 11) / 100);
        row[x] = lum;
        break;
    }
    case 1: {
        uint8_t lum = (uint8_t)((((rgb >> 16) & 0xff) +
                                 ((rgb >> 8) & 0xff) +
                                 (rgb & 0xff)) / 3);
        uint8_t *byte = row + (x >> 3);
        uint8_t mask = (uint8_t)(0x80 >> (x & 7));
        if (lum > 127) {
            *byte |= mask;
        } else {
            *byte &= (uint8_t)~mask;
        }
        break;
    }
    default:
        break;
    }
}

/* 把一个 RGBA 源像素画到 (x,y)，按 alpha 与底色混合，越界裁剪。 */
static void fb_blend_rgba(RacingFbView *v, int x, int y,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint32_t rgb;
    uint32_t dst;
    int ia;

    if (a == 0 || x < 0 || y < 0 || x >= v->draw_w || y >= v->draw_h) {
        return;
    }
    if (a == 255) {
        rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        fb_put_native(v, x, y, rgb);
        return;
    }

    dst = fb_read_rgb(v, x, y);
    ia = 255 - a;
    rgb = (((r * a + (((dst >> 16) & 0xff) * ia)) / 255) << 16) |
          (((g * a + (((dst >> 8) & 0xff) * ia)) / 255) << 8) |
          ((b * a + ((dst & 0xff) * ia)) / 255);
    fb_put_native(v, x, y, rgb);
}

static void fb_blit_textured_triangle(RacingFbView *v, const RacingImg *img,
                                      FbTexPoint a, FbTexPoint b, FbTexPoint c,
                                      uint8_t shade)
{
    float minX;
    float maxX;
    float minY;
    float maxY;
    float denom;
    int x0;
    int x1;
    int y0;
    int y1;

    if (img == NULL || img->px == NULL)
    {
        return;
    }

    denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (fabsf(denom) < 0.0001f)
    {
        return;
    }

    minX = fminf(a.x, fminf(b.x, c.x));
    maxX = fmaxf(a.x, fmaxf(b.x, c.x));
    minY = fminf(a.y, fminf(b.y, c.y));
    maxY = fmaxf(a.y, fmaxf(b.y, c.y));
    x0 = (int)floorf(minX);
    x1 = (int)ceilf(maxX);
    y0 = (int)floorf(minY);
    y1 = (int)ceilf(maxY);
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 >= v->draw_w) { x1 = v->draw_w - 1; }
    if (y1 >= v->draw_h) { y1 = v->draw_h - 1; }
    if (x0 > x1 || y0 > y1)
    {
        return;
    }

    for (int y = y0; y <= y1; y++)
    {
        for (int x = x0; x <= x1; x++)
        {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float wa = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / denom;
            float wb = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / denom;
            float wc = 1.0f - wa - wb;
            float u;
            float vv;
            int sx;
            int sy;
            const uint8_t *sp;
            uint8_t r;
            uint8_t g;
            uint8_t bch;

            if (wa < -0.001f || wb < -0.001f || wc < -0.001f)
            {
                continue;
            }

            u = wa * a.u + wb * b.u + wc * c.u;
            vv = wa * a.v + wb * b.v + wc * c.v;
            if (u < 0.0f) { u = 0.0f; }
            if (u > 1.0f) { u = 1.0f; }
            if (vv < 0.0f) { vv = 0.0f; }
            if (vv > 1.0f) { vv = 1.0f; }

            sx = (int)(u * (float)(img->w - 1));
            sy = (int)(vv * (float)(img->h - 1));
            sp = img->px + ((size_t)sy * img->w + sx) * 4;
            r = (uint8_t)((unsigned int)sp[0] * shade / 255u);
            g = (uint8_t)((unsigned int)sp[1] * shade / 255u);
            bch = (uint8_t)((unsigned int)sp[2] * shade / 255u);
            fb_blend_rgba(v, x, y, r, g, bch, sp[3]);
        }
    }
}

static void fb_blit_textured_quad(RacingFbView *v, const RacingImg *img,
                                  FbPoint p0, FbPoint p1, FbPoint p2, FbPoint p3,
                                  uint8_t shade)
{
    FbTexPoint t0 = {p0.x, p0.y, 0.0f, 0.0f};
    FbTexPoint t1 = {p1.x, p1.y, 1.0f, 0.0f};
    FbTexPoint t2 = {p2.x, p2.y, 1.0f, 1.0f};
    FbTexPoint t3 = {p3.x, p3.y, 0.0f, 1.0f};

    fb_blit_textured_triangle(v, img, t0, t1, t2, shade);
    fb_blit_textured_triangle(v, img, t0, t2, t3, shade);
}

/* 把整张 img 缩放到目标矩形 (dx,dy,dw,dh)，最近邻 + alpha 混合，裁剪到屏幕。 */
static void fb_blit_scaled(RacingFbView *v, const RacingImg *img,
                           int dx, int dy, int dw, int dh)
{
    int x0;
    int y0;
    int x1;
    int y1;

    if (img == NULL || img->px == NULL || dw <= 0 || dh <= 0) {
        return;
    }

    x0 = dx < 0 ? 0 : dx;
    y0 = dy < 0 ? 0 : dy;
    x1 = dx + dw > v->draw_w ? v->draw_w : dx + dw;
    y1 = dy + dh > v->draw_h ? v->draw_h : dy + dh;

    for (int y = y0; y < y1; y++) {
        int sy = (y - dy) * img->h / dh;
        if (sy < 0 || sy >= img->h) {
            continue;
        }
        const uint8_t *srow = img->px + (size_t)sy * img->w * 4;
        for (int x = x0; x < x1; x++) {
            int sx = (x - dx) * img->w / dw;
            const uint8_t *sp;
            if (sx < 0 || sx >= img->w) {
                continue;
            }
            sp = srow + (size_t)sx * 4;
            fb_blend_rgba(v, x, y, sp[0], sp[1], sp[2], sp[3]);
        }
    }
}

/* 从板端数据分区加载一张 raw 贴图。失败返回 NULL（绘制时安全跳过）。 */
static RacingImg *img_load(const char *name)
{
    char path[96];
    struct {
        uint32_t magic;
        uint16_t w;
        uint16_t h;
    } hdr;
    RacingImg *img;
    int fd;
    size_t n;
    size_t got;

    snprintf(path, sizeof(path), "%s/%s", RACING_RES_ROOT, name);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("[RACING] img open fail %s: %d\n", path, errno);
        return NULL;
    }
    if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr) || hdr.magic != RACING_IMG_MAGIC) {
        printf("[RACING] img bad header %s\n", path);
        close(fd);
        return NULL;
    }

    img = (RacingImg *)calloc(1, sizeof(*img));
    if (img == NULL) {
        close(fd);
        return NULL;
    }
    img->w = hdr.w;
    img->h = hdr.h;
    n = (size_t)hdr.w * hdr.h * 4;
    img->px = (uint8_t *)malloc(n);
    if (img->px == NULL) {
        free(img);
        close(fd);
        return NULL;
    }

    got = 0;
    while (got < n) {
        ssize_t r = read(fd, img->px + got, n - got);
        if (r <= 0) {
            break;
        }
        got += (size_t)r;
    }
    close(fd);

    if (got != n) {
        printf("[RACING] img short read %s: %zu/%zu\n", path, got, n);
    }
    printf("[RACING] loaded %s %ux%u\n", path, img->w, img->h);
    return img;
}

static void img_free(RacingImg *img)
{
    if (img != NULL) {
        free(img->px);
        free(img);
    }
}

/****************************************************************************
 * 场景投影与路面绘制：透视梯形 + 路肩/路面带 + 终点旗,对齐 render_sdl.c。
 ****************************************************************************/

static ProjectionContext make_projection_context(int camX, int camY, int camZ, float angle)
{
    ProjectionContext ctx;
    ctx.camX = camX;
    ctx.camY = camY;
    ctx.camZ = camZ;
    ctx.sinAngle = sinf(angle);
    ctx.cosAngle = cosf(angle);
    return ctx;
}

static void project_road(Road *road, const ProjectionContext *ctx, float roadWidth)
{
    road->tx = (road->x - ctx->camX) * ctx->cosAngle + (road->z - ctx->camZ) * ctx->sinAngle;
    road->tz = -(road->x - ctx->camX) * ctx->sinAngle + (road->z - ctx->camZ) * ctx->cosAngle;

    if (road->tz < 0.1f) {
        road->tz = 0.1f;
    }

    road->scale = 1.0f / road->tz;
    road->X = (1.0f + road->scale * road->tx) * WIN_WIDTH / 2.0f;
    road->Y = (1.0f - road->scale * (road->y - ctx->camY)) * WIN_HEIGHT / 2.0f;
    road->W = road->scale * roadWidth * WIN_WIDTH / 2.0f;
}

static void project_world_point(float x, float y, float z, const ProjectionContext *ctx, FbPoint *point)
{
    float tx = (x - ctx->camX) * ctx->cosAngle + (z - ctx->camZ) * ctx->sinAngle;
    float tz = -(x - ctx->camX) * ctx->sinAngle + (z - ctx->camZ) * ctx->cosAngle;
    float scale;

    if (tz < 0.1f) {
        tz = 0.1f;
    }

    scale = 1.0f / tz;
    point->x = (1.0f + scale * tx) * WIN_WIDTH / 2.0f;
    point->y = (1.0f - scale * (y - ctx->camY)) * WIN_HEIGHT / 2.0f;
}

/* 路边到屏幕边的草地带(转向时随 bankAngle 倾斜,对齐 SDL draw_ground_band)。 */
static void draw_ground_band(RacingFbView *v, const Road *farRoad, const Road *nearRoad, float bankAngle)
{
    float cosBank = cosf(bankAngle);
    float sinBank = sinf(bankAngle);
    FbPoint farLeftOuter  = {0.0f, farRoad->Y - farRoad->W * sinBank};
    FbPoint farLeftInner  = {farRoad->X - farRoad->W * cosBank, farRoad->Y - farRoad->W * sinBank};
    FbPoint nearLeftInner = {nearRoad->X - nearRoad->W * cosBank, nearRoad->Y - nearRoad->W * sinBank};
    FbPoint nearLeftOuter = {0.0f, nearRoad->Y - nearRoad->W * sinBank};
    FbPoint farRightInner  = {farRoad->X + farRoad->W * cosBank, farRoad->Y + farRoad->W * sinBank};
    FbPoint farRightOuter  = {(float)WIN_WIDTH, farRoad->Y + farRoad->W * sinBank};
    FbPoint nearRightOuter = {(float)WIN_WIDTH, nearRoad->Y + nearRoad->W * sinBank};
    FbPoint nearRightInner = {nearRoad->X + nearRoad->W * cosBank, nearRoad->Y + nearRoad->W * sinBank};

    /* 地图2(bg_bupt)用深灰草地,其余地图用绿色。 */
    uint32_t grass = (v->game->mapIndex == 1) ? COL_GRASS_DARK : COL_GRASS;
    fb_fill_quad(v, farLeftOuter, farLeftInner, nearLeftInner, nearLeftOuter, grass);
    fb_fill_quad(v, farRightInner, farRightOuter, nearRightOuter, nearRightInner, grass);
}

/* 路面/路肩：透视梯形(对齐 SDL draw_trapezoid)。用矩形会在转向时因 sinBank
 * 膨胀而错位(路肩黑白色溢出成条纹)。 */
static void draw_road_band(RacingFbView *v, const Road *farRoad, const Road *nearRoad,
                           float widthScale, float bankAngle, uint32_t color)
{
    float farW = farRoad->W * widthScale;
    float nearW = nearRoad->W * widthScale;
    float cosBank = cosf(bankAngle);
    float sinBank = sinf(bankAngle);
    FbPoint fl = { farRoad->X - farW * cosBank, farRoad->Y - farW * sinBank };
    FbPoint fr = { farRoad->X + farW * cosBank, farRoad->Y + farW * sinBank };
    FbPoint nr = { nearRoad->X + nearW * cosBank, nearRoad->Y + nearW * sinBank };
    FbPoint nl = { nearRoad->X - nearW * cosBank, nearRoad->Y - nearW * sinBank };

    fb_fill_quad(v, fl, fr, nr, nl, color);
}

static void draw_world_rect(RacingFbView *v, uint32_t color, float x1, float y1,
                            float x2, float y2, float z, const ProjectionContext *ctx)
{
    FbPoint a;
    FbPoint b;
    FbPoint c;
    FbPoint d;

    project_world_point(x1, y1, z, ctx, &a);
    project_world_point(x2, y1, z, ctx, &b);
    project_world_point(x2, y2, z, ctx, &c);
    project_world_point(x1, y2, z, ctx, &d);
    fb_fill_quad(v, a, b, c, d, color);
}

/* 终点旗门：两根红柱 + 黑白方格旗面。 */
static void draw_finish_flag(RacingFbView *v, const Road *road, const ProjectionContext *ctx, float roadWidth)
{
    float z = road->z;
    float halfWidth = roadWidth * 0.78f;
    float leftX = road->x - halfWidth;
    float rightX = road->x + halfWidth;
    float poleWidth = 140.0f;
    float poleTopY = road->y + 3700.0f;
    float flagTopY = road->y + 3560.0f;
    float flagBottomY = road->y + 2460.0f;
    float flagLeftX = leftX + poleWidth;
    float flagRightX = rightX - poleWidth;
    float cellWidth = (flagRightX - flagLeftX) / 6.0f;
    float cellHeight = (flagTopY - flagBottomY) / 3.0f;

    draw_world_rect(v, COL_POLE, leftX, road->y, leftX + poleWidth, poleTopY, z, ctx);
    draw_world_rect(v, COL_POLE, rightX - poleWidth, road->y, rightX, poleTopY, z, ctx);

    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 6; column++) {
            float cx1 = flagLeftX + cellWidth * column;
            float cx2 = flagLeftX + cellWidth * (column + 1);
            float cy1 = flagTopY - cellHeight * row;
            float cy2 = flagTopY - cellHeight * (row + 1);
            uint32_t color = (row + column) % 2 == 0 ? 0xffffffu : 0x000000u;
            draw_world_rect(v, color, cx1, cy1, cx2, cy2, z, ctx);
        }
    }
}

static int segment_distance_from_start(int start, int segment)
{
    int distance = segment - start;
    if (distance < 0) {
        distance += ROAD_COUNT;
    }
    return distance;
}

static bool is_collectible_visible(const RacingGame *game, int collectibleIndex)
{
    int offset = segment_distance_from_start(racing_game_start_segment(game),
                                             game->collectiblePositions[collectibleIndex]);
    return offset > 0 && offset <= VIEW_DISTANCE;
}

/* 从远到近画 VIEW_DISTANCE 段：草地 + 路肩 + 路面 + 终点旗。 */
static void render_track(RacingFbView *v)
{
    RacingGame *game = v->game;
    int start = racing_game_start_segment(game);
    int farIndex = (start + VIEW_DISTANCE) % ROAD_COUNT;
    int nearIndex = farIndex > 0 ? farIndex - 1 : ROAD_COUNT - 1;
    ProjectionContext context = make_projection_context(game->camX, game->camY, game->camZ, game->angle);
    Road farRoad = game->roads[farIndex];
    int farWrap = start + VIEW_DISTANCE >= ROAD_COUNT ? TRACK_LENGTH : 0;
    float bankAngle = game->turnLeft ? -0.2f : game->turnRight ? 0.2f : 0.0f;

    context.camZ = game->camZ - farWrap;
    project_road(&farRoad, &context, game->roadWidth);

    for (int offset = VIEW_DISTANCE; offset > 0; offset--) {
        int segment = start + offset;
        int segmentIndex = segment >= ROAD_COUNT ? segment - ROAD_COUNT : segment;
        Road nearRoad = game->roads[nearIndex];
        int nearWrap = segment - 1 >= ROAD_COUNT ? TRACK_LENGTH : 0;
        uint32_t edge = segment % 2 ? 0x000000u : 0xffffffu;
        uint32_t road = segment % 2 ? COL_ROAD_DK : COL_ROAD_LT;

        context.camZ = game->camZ - nearWrap;
        project_road(&nearRoad, &context, game->roadWidth);

        if ((farRoad.Y >= WIN_HEIGHT && nearRoad.Y >= WIN_HEIGHT) ||
            (farRoad.Y < -WIN_HEIGHT && nearRoad.Y < -WIN_HEIGHT)) {
            farRoad = nearRoad;
            nearIndex = nearIndex > 0 ? nearIndex - 1 : ROAD_COUNT - 1;
            continue;
        }

        draw_ground_band(v, &farRoad, &nearRoad, bankAngle);
        draw_road_band(v, &farRoad, &nearRoad, 1.3f, bankAngle, edge);
        draw_road_band(v, &farRoad, &nearRoad, 1.0f, bankAngle, road);

        if (segmentIndex == ROAD_COUNT - 1) {
            draw_finish_flag(v, &game->roads[ROAD_COUNT - 1], &context, game->roadWidth);
        }

        farRoad = nearRoad;
        nearIndex = nearIndex > 0 ? nearIndex - 1 : ROAD_COUNT - 1;
    }
}

/****************************************************************************
 * 奶龙收集物：原本是贴图，这里用几何图形（绿身 + 双眼 + 嘴）画，
 * 尺寸取自 game->collectibles[i] 的投影四边形。
 ****************************************************************************/

static void draw_nailong(RacingFbView *v, int x, int y, int w, int h)
{
    int eyeW;
    int eyeH;
    int pupil;
    int lx;
    int rx;
    int ey;
    int mouthY;
    int mouthH;

    if (w < 24) { w = 24; }
    if (h < 28) { h = 28; }

    /* 身体：圆头绿块。 */
    fb_fill_rect(v, x, y, x + w - 1, y + h - 1, 0x3fbf3au);

    /* 眼睛：上三分之一两个白块 + 黑瞳。 */
    eyeW = w / 5;
    eyeH = h / 5;
    if (eyeW < 3) { eyeW = 3; }
    if (eyeH < 3) { eyeH = 3; }
    ey = y + h / 5;
    lx = x + w / 5;
    rx = x + w - w / 5 - eyeW;

    fb_fill_rect(v, lx, ey, lx + eyeW - 1, ey + eyeH - 1, 0xffffffu);
    fb_fill_rect(v, rx, ey, rx + eyeW - 1, ey + eyeH - 1, 0xffffffu);

    pupil = eyeW / 3;
    if (pupil < 1) { pupil = 1; }
    fb_fill_rect(v, lx + eyeW - pupil - 1, ey, lx + eyeW - 1, ey + pupil - 1, 0x000000u);
    fb_fill_rect(v, rx + eyeW - pupil - 1, ey, rx + eyeW - 1, ey + pupil - 1, 0x000000u);

    /* 嘴。 */
    mouthH = h / 8;
    if (mouthH < 2) { mouthH = 2; }
    mouthY = y + (h * 5) / 8;
    fb_fill_rect(v, x + w / 4, mouthY, x + (w * 3) / 4 - 1, mouthY + mouthH - 1, 0x102010u);
}

static void render_collectibles(RacingFbView *v)
{
    const RacingGame *game = v->game;

    for (int i = 0; i < COLLECTIBLE_COUNT; i++) {
        const Nailong *item = &game->collectibles[i];
        int x = (int)item->p[0].X;
        int y = (int)item->p[2].Y;
        int w = (int)fabsf(item->p[1].X - item->p[0].X);
        int h = (int)fabsf(item->p[0].Y - item->p[2].Y);

        if (item->eaten || !is_collectible_visible(game, i) ||
            item->p[0].tz <= 0.1f || item->p[1].tz <= 0.1f ||
            x >= WIN_WIDTH || y >= WIN_HEIGHT || x + w < 0 || y + h < 0) {
            continue;
        }

        /* 地图“一路邮你”用北邮校徽，其余用奶龙贴图；都没有则画几何奶龙。 */
        {
            RacingImg *sprite = (game->mapIndex == 1 && v->img_nailong_bupt != NULL)
                                    ? v->img_nailong_bupt
                                    : v->img_nailong;
            if (sprite != NULL) {
                fb_blit_scaled(v, sprite, x, y, w, h);
            } else {
                draw_nailong(v, x, y, w, h);
            }
        }
    }
}

static FbPoint fb_point_from_game_point(const Point *p)
{
    FbPoint out;
    out.x = p->X;
    out.y = p->Y;
    return out;
}

static int point_in_front(const Point *p)
{
    return p != NULL && p->tz > (float)SEG_LENGTH * 0.45f;
}

static int face_in_front(const Point *a, const Point *b, const Point *c, const Point *d)
{
    return point_in_front(a) && point_in_front(b) &&
           point_in_front(c) && point_in_front(d);
}

static void render_trees_fb(RacingFbView *v)
{
    const RacingGame *game = v->game;

    if (game->mapIndex == 1 || v->img_tree == NULL)
    {
        return;
    }

    for (int d = VIEW_DISTANCE; d >= 1; d--)
    {
        for (int i = 0; i < TREE_COUNT; i++)
        {
            const Tree *t = &game->trees[i];
            int x;
            int y;
            int w;
            int h;

            if (!t->visible || t->segDist != d || t->p[0].tz <= 0.1f)
            {
                continue;
            }

            w = (int)fabsf(t->p[1].X - t->p[0].X);
            h = (int)fabsf(t->p[0].Y - t->p[2].Y);
            x = (int)t->p[0].X;
            y = (int)t->p[2].Y;
            if (w <= 0 || h <= 0 ||
                x >= WIN_WIDTH || y >= WIN_HEIGHT || x + w < 0 || y + h < 0)
            {
                continue;
            }

            fb_blit_scaled(v, v->img_tree, x, y, w, h);
        }
    }
}

static void render_houses_fb(RacingFbView *v)
{
    const RacingGame *game = v->game;

    if (game->mapIndex == 1 || (v->img_house_a == NULL && v->img_house_b == NULL))
    {
        return;
    }

    for (int d = VIEW_DISTANCE; d >= 1; d--)
    {
        for (int i = 0; i < HOUSE_COUNT; i++)
        {
            const House *h = &game->houses[i];
            const Point *c;
            FbPoint p0;
            FbPoint p1;
            FbPoint p2;
            FbPoint p3;

            if (!h->visible || h->segDist != d)
            {
                continue;
            }

            c = h->corner;

            /* 屋顶：y 为常量的顶面，先画纯色屋顶。 */
            if (face_in_front(&c[2], &c[3], &c[7], &c[6]))
            {
                p0 = fb_point_from_game_point(&c[2]);
                p1 = fb_point_from_game_point(&c[3]);
                p2 = fb_point_from_game_point(&c[7]);
                p3 = fb_point_from_game_point(&c[6]);
                fb_fill_quad(v, p0, p1, p2, p3, 0x844e39u);
            }

            /* A 面：z 为常量，横跨 x 方向，和路的前进方向垂直。 */
            if (v->img_house_a != NULL && face_in_front(&c[2], &c[3], &c[1], &c[0]))
            {
                p0 = fb_point_from_game_point(&c[2]);
                p1 = fb_point_from_game_point(&c[3]);
                p2 = fb_point_from_game_point(&c[1]);
                p3 = fb_point_from_game_point(&c[0]);
                fb_blit_textured_quad(v, v->img_house_a, p0, p1, p2, p3, 255);
            }

            /* B 面：x 为常量，沿 z 方向延伸，和路平行；选靠近道路的一侧。 */
            if (v->img_house_b != NULL)
            {
                if (h->side >= 0 && face_in_front(&c[2], &c[6], &c[4], &c[0]))
                {
                    p0 = fb_point_from_game_point(&c[2]);
                    p1 = fb_point_from_game_point(&c[6]);
                    p2 = fb_point_from_game_point(&c[4]);
                    p3 = fb_point_from_game_point(&c[0]);
                    fb_blit_textured_quad(v, v->img_house_b, p0, p1, p2, p3, 215);
                }
                else if (h->side < 0 && face_in_front(&c[7], &c[3], &c[1], &c[5]))
                {
                    p0 = fb_point_from_game_point(&c[7]);
                    p1 = fb_point_from_game_point(&c[3]);
                    p2 = fb_point_from_game_point(&c[1]);
                    p3 = fb_point_from_game_point(&c[5]);
                    fb_blit_textured_quad(v, v->img_house_b, p0, p1, p2, p3, 215);
                }
            }
        }
    }
}

static void render_world_decor(RacingFbView *v)
{
    render_houses_fb(v);
    render_trees_fb(v);
}

/****************************************************************************
 * 背景与前景贴图(对齐 render_sdl.c)：全屏高条带背景、车内视角车、跳脸。
 ****************************************************************************/

/* 一条横向视差条带：把 img 缩放到 SKY_DRAW_H 高放在 y，按 camx*factor 水平平铺。
 * 只画天空区(地平线略下),下半屏会被草地/路面覆盖，省掉一半背景开销。 */
#define SKY_DRAW_H (WIN_HEIGHT / 2 + 20)

static void fb_blit_strip(RacingFbView *v, const RacingImg *img, int y, int camx, float factor)
{
    int off;
    if (img == NULL) {
        return;
    }
    off = (int)(camx * factor) % img->w;
    if (off < 0) {
        off += img->w;
    }
    fb_blit_scaled(v, img, -off, y, img->w, SKY_DRAW_H);
    fb_blit_scaled(v, img, -off + img->w, y, img->w, SKY_DRAW_H);
}

/* 天空装饰：云/远山/近山作为全屏高条带，y 偏移与视差因子对齐 SDL。 */
static void render_sky_decor(RacingFbView *v)
{
    int camx = v->game->camX;
    fb_blit_strip(v, v->img_cloud, 0, camx, 0.01f);
    fb_blit_strip(v, v->img_mtn_far, 50, camx, 0.018f);
    fb_blit_strip(v, v->img_mtn_near, 75, camx, 0.028f);
}

/* 车内视角:全宽底部,高度可调(纵向压扁,留出更多路面)。 */
#define CAR_DISPLAY_H 220
static void render_car(RacingFbView *v)
{
    if (v->img_car != NULL) {
        fb_blit_scaled(v, v->img_car, 0, WIN_HEIGHT - CAR_DISPLAY_H,
                       WIN_WIDTH, CAR_DISPLAY_H);
    }
}

/* 命中后奶龙跳脸：整屏闪一下。地图“一路邮你”闪北邮校徽。 */
static void render_hit_flash(RacingFbView *v)
{
    if (v->game->hitFrames > 0) {
        RacingImg *face = (v->game->mapIndex == 1 && v->img_nailong_bupt != NULL)
                              ? v->img_nailong_bupt
                              : v->img_head;
        if (face != NULL) {
            fb_blit_scaled(v, face, 0, 0, WIN_WIDTH, WIN_HEIGHT);
        }
    }
}

/****************************************************************************
 * HUD(对齐 render_sdl.c)：半透明 alpha 矩形、点阵文字、能量条、模式面板。
 ****************************************************************************/

static int fb_text_width(const char *s)
{
    return s == NULL ? 0 : (int)strlen(s) * FONT_CELL_W;
}

/* 半透明实心矩形(用于面板底色)。inclusive 坐标，自动裁剪。 */
static void fb_fill_rect_alpha(RacingFbView *v, int x0, int y0, int x1, int y1,
                               uint32_t rgb, uint8_t a)
{
    int tmp;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    int ia;

    if (a == 0) {
        return;
    }
    if (x1 < x0) { tmp = x0; x0 = x1; x1 = tmp; }
    if (y1 < y0) { tmp = y0; y0 = y1; y1 = tmp; }
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 >= v->draw_w) { x1 = v->draw_w - 1; }
    if (y1 >= v->draw_h) { y1 = v->draw_h - 1; }
    if (x0 > x1 || y0 > y1) {
        return;
    }
    if (a == 255) {
        fb_fill_rect(v, x0, y0, x1, y1, rgb);
        return;
    }

    r = (rgb >> 16) & 0xff;
    g = (rgb >> 8) & 0xff;
    b = rgb & 0xff;
    ia = 255 - a;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            uint32_t dst = fb_read_rgb(v, x, y);
            uint32_t rr = (r * a + (((dst >> 16) & 0xff) * ia)) / 255;
            uint32_t gg = (g * a + (((dst >> 8) & 0xff) * ia)) / 255;
            uint32_t bb = (b * a + ((dst & 0xff) * ia)) / 255;
            fb_put_native(v, x, y, (rr << 16) | (gg << 8) | bb);
        }
    }
}

/* 等宽点阵文字，alpha 抗锯齿混合。 */
static void fb_draw_text(RacingFbView *v, int x, int y, const char *s, uint32_t rgb)
{
    uint8_t r = (rgb >> 16) & 0xff;
    uint8_t g = (rgb >> 8) & 0xff;
    uint8_t b = rgb & 0xff;
    int cx = x;

    if (s == NULL) {
        return;
    }
    for (const char *p = s; *p != '\0'; p++) {
        const uint8_t *glyph = font_glyph((int)(uint8_t)*p);
        if (glyph != NULL) {
            for (int py = 0; py < FONT_CELL_H; py++) {
                for (int px = 0; px < FONT_CELL_W; px++) {
                    uint8_t a = glyph[py * FONT_CELL_W + px];
                    if (a != 0) {
                        fb_blend_rgba(v, cx + px, y + py, r, g, b, a);
                    }
                }
            }
        }
        cx += FONT_CELL_W;
    }
}

static void fb_draw_text_centered(RacingFbView *v, int cx, int y, const char *s, uint32_t rgb)
{
    fb_draw_text(v, cx - fb_text_width(s) / 2, y, s, rgb);
}

/* 右下能量条：10 格竖向，空槽 + 已满。 */
static void render_energy(RacingFbView *v, int energy)
{
    enum { BAR_COUNT = 10, BAR_W = 10, BAR_H = 15, BAR_GAP = 3 };
    int bars = (energy + 99) / 100;
    int panelW = BAR_W + 12;
    int panelH = BAR_COUNT * BAR_H + (BAR_COUNT - 1) * BAR_GAP + 12;
    int panelX = WIN_WIDTH - panelW - 10;
    int panelY = WIN_HEIGHT - panelH - 10;
    /* "我是奶龙" 作弊:能量条变黄。 */
    uint32_t fill = (v->game != NULL && v->game->infiniteEnergy) ? 0xffd23cu : 0x1d63ffu;

    if (bars < 0) { bars = 0; }
    if (bars > BAR_COUNT) { bars = BAR_COUNT; }

    fb_fill_rect_alpha(v, panelX, panelY, panelX + panelW - 1, panelY + panelH - 1,
                       0x0d1730u, 220);
    fb_fill_rect(v, panelX, panelY, panelX + panelW - 1, panelY, 0x7f97c7u);
    fb_fill_rect(v, panelX, panelY + panelH - 1, panelX + panelW - 1, panelY + panelH - 1, 0x7f97c7u);
    fb_fill_rect(v, panelX, panelY, panelX, panelY + panelH - 1, 0x7f97c7u);
    fb_fill_rect(v, panelX + panelW - 1, panelY, panelX + panelW - 1, panelY + panelH - 1, 0x7f97c7u);

    for (int i = 0; i < BAR_COUNT; i++) {
        int sy = panelY + 6 + (BAR_COUNT - 1 - i) * (BAR_H + BAR_GAP);
        fb_fill_rect(v, panelX + 6, sy, panelX + 6 + BAR_W - 1, sy + BAR_H - 1, 0x17305au);
    }
    for (int i = 0; i < bars; i++) {
        int sy = panelY + 6 + (BAR_COUNT - 1 - i) * (BAR_H + BAR_GAP);
        fb_fill_rect(v, panelX + 6, sy, panelX + 6 + BAR_W - 1, sy + BAR_H - 1, fill);
    }
}

/* 中央模式面板：开始/暂停/胜利时显示提示(触摸版措辞)。 */
static void render_mode_overlay(RacingFbView *v)
{
    const RacingGame *g = v->game;
    int pw = 280;
    int ph = 156;
    int px = (WIN_WIDTH - pw) / 2;
    int py = (WIN_HEIGHT - ph) / 2;
    uint32_t title = 0xffffffu;
    uint32_t body = 0xe6ecffu;
    uint32_t hint = 0xafc1e6u;
    char buf[64];

    if (g->mode == RACING_MODE_PLAYING) {
        return;
    }

    fb_fill_rect_alpha(v, px, py, px + pw - 1, py + ph - 1, 0x0d1730u, 218);
    fb_fill_rect(v, px, py, px + pw - 1, py, 0x7f97c7u);
    fb_fill_rect(v, px, py + ph - 1, px + pw - 1, py + ph - 1, 0x7f97c7u);
    fb_fill_rect(v, px, py, px, py + ph - 1, 0x7f97c7u);
    fb_fill_rect(v, px + pw - 1, py, px + pw - 1, py + ph - 1, 0x7f97c7u);

    if (g->mode == RACING_MODE_START) {
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 24, "Racing", title);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 70, "Tap center to start", body);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 96, "Btn1 start / pause", hint);
    } else if (g->mode == RACING_MODE_PAUSED) {
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 24, "Paused", title);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 64, "center: resume", body);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 86, "top: main menu", 0xffd23cu);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 104, "Btn1 resume", hint);
    } else if (g->mode == RACING_MODE_WIN) {
        snprintf(buf, sizeof(buf), "Finish in %ds", g->finalSeconds);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 24, buf, title);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 64, "center: restart", body);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 86, "top: main menu", 0xffd23cu);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 104, "Btn1 restart", hint);
    }
}

/* 俯视 2D 赛道。两种用法:
 *  - 选图预览(show_pos=0):整条赛道斜放,起点左下、终点右上,看整图形状。
 *  - 游戏小地图(show_pos=1):以当前 camZ 为中心的局部滚动窗口——前方多看、
 *    后方少看,路面随前进向上滚动,黄点标车当前位置。路宽按 roadWidth 放大
 *    画成一条带(越难越窄)。 */
static void render_track_topdown(RacingFbView *v, const RacingGame *g,
                                 int x0, int y0, int w, int h, int show_pos)
{
    enum { STEP = 10 };
    int bandHalf;     /* 路面半宽(px) */

    /* 路宽放大:按当前 roadWidth / ROAD_WIDTH 缩放,越难越窄。 */
    bandHalf = (int)(g->roadWidth / (float)ROAD_WIDTH * 6.0f + 0.5f);
    if (bandHalf < 2) {
        bandHalf = 2;
    }
    if (bandHalf > 8) {
        bandHalf = 8;
    }

    if (!show_pos) {
        /* ---- 整图斜放预览(菜单选图):起点左下 → 终点右上 ---- */
        float xmin = 1e30f;
        float xmax = -1e30f;
        float xmid;
        float half;
        int inset;
        float sx, sy, fx, fy;
        float dxs, dys, dlen, dnx, dny, pnx, pny, lat;
        FbPoint prevL, prevR;
        int havePrev = 0;
        int n;
        int k;

        for (int i = 0; i < ROAD_COUNT; i++) {
            if (g->roads[i].x < xmin) {
                xmin = g->roads[i].x;
            }
            if (g->roads[i].x > xmax) {
                xmax = g->roads[i].x;
            }
        }
        xmid = (xmin + xmax) * 0.5f;
        half = (xmax - xmin) * 0.5f;
        if (half < 1.0f) {
            half = 1.0f;
        }

        inset = (w < h ? w : h) / 8 + 2;
        sx = (float)(x0 + inset);
        sy = (float)(y0 + h - inset);
        fx = (float)(x0 + w - inset);
        fy = (float)(y0 + inset);
        dxs = fx - sx;
        dys = fy - sy;
        dlen = sqrtf(dxs * dxs + dys * dys);
        if (dlen < 1.0f) {
            dlen = 1.0f;
        }
        dnx = dxs / dlen;
        dny = dys / dlen;
        pnx = -dny;
        pny = dnx;
        lat = (w < h ? w : h) * 0.16f;

        n = (ROAD_COUNT - 1) / STEP;
        for (k = 0; k <= n; k++) {
            int idx = k * STEP;
            float u;
            float vv;
            float bx;
            float by;
            float cx;
            float cy;
            FbPoint pl;
            FbPoint pr;

            if (idx > ROAD_COUNT - 1) {
                idx = ROAD_COUNT - 1;
            }
            u = (float)idx / (float)(ROAD_COUNT - 1);
            vv = (g->roads[idx].x - xmid) / half;
            if (vv < -1.0f) {
                vv = -1.0f;
            }
            if (vv > 1.0f) {
                vv = 1.0f;
            }
            bx = sx + u * dxs;
            by = sy + u * dys;
            cx = bx + vv * lat * pnx;
            cy = by + vv * lat * pny;
            pl.x = cx - bandHalf * pnx;
            pl.y = cy - bandHalf * pny;
            pr.x = cx + bandHalf * pnx;
            pr.y = cy + bandHalf * pny;
            if (havePrev) {
                fb_fill_quad(v, prevR, pr, pl, prevL, 0x4b6fd6u);
            }
            prevL = pl;
            prevR = pr;
            havePrev = 1;
        }

        fb_fill_rect(v, (int)sx - 2, (int)sy - 2, (int)sx + 2, (int)sy + 2, 0x33c24du);
        fb_fill_rect(v, (int)fx - 2, (int)fy - 2, (int)fx + 2, (int)fy + 2, 0xe0413cu);
        return;
    }

    /* ---- 局部滚动小地图(游戏中):以 camZ 为中心,前方/后方一段 ---- */
    {
        enum { AHEAD = 60, BEHIND = 12, WIN = AHEAD + BEHIND + 1, LSTEP = 2 };
        int seg0 = (int)(g->camZ / SEG_LENGTH);
        int cx = x0 + w / 2;
        int m = (w < h ? w : h) / 10 + 2;
        int yCur = y0 + (int)(h * 0.72f);
        int topY = y0 + m;
        int botY = y0 + h - m;
        float vsA = (float)(yCur - topY) / (float)AHEAD;
        float vsB = (float)(botY - yCur) / (float)BEHIND;
        float vs = (vsA < vsB ? vsA : vsB);
        float halfw = (float)(w / 2 - m);
        float xs[WIN];
        float xmin = 1e30f;
        float xmax = -1e30f;
        float xmid;
        float half;
        FbPoint prevL, prevR;
        int havePrev = 0;
        int d;

        if (seg0 < 0) {
            seg0 = 0;
        }
        if (seg0 >= ROAD_COUNT) {
            seg0 = ROAD_COUNT - 1;
        }

        /* 先扫一遍窗口,求横向范围以自适应铺满宽度。 */
        for (d = 0; d < WIN; d++) {
            int seg = ((seg0 - BEHIND + d) % ROAD_COUNT + ROAD_COUNT) % ROAD_COUNT;
            xs[d] = g->roads[seg].x;
            if (xs[d] < xmin) {
                xmin = xs[d];
            }
            if (xs[d] > xmax) {
                xmax = xs[d];
            }
        }
        xmid = (xmin + xmax) * 0.5f;
        half = (xmax - xmin) * 0.5f;
        if (half < 1.0f) {
            half = 1.0f;
        }

        /* 路带:横向 x→屏幕水平,段偏移 dd→垂直(前方在上)。 */
        for (d = 0; d < WIN; d += LSTEP) {
            int seg = ((seg0 - BEHIND + d) % ROAD_COUNT + ROAD_COUNT) % ROAD_COUNT;
            float vv = (g->roads[seg].x - xmid) / half;
            float bx = (float)cx + vv * halfw;
            int dd = d - BEHIND;            /* 负=后方,正=前方 */
            float by = (float)yCur - dd * vs;
            FbPoint pl;
            FbPoint pr;

            pl.x = bx - bandHalf;
            pl.y = by;
            pr.x = bx + bandHalf;
            pr.y = by;
            if (havePrev) {
                fb_fill_quad(v, prevL, pl, pr, prevR, 0x4b6fd6u);
            }
            prevL = pl;
            prevR = pr;
            havePrev = 1;
        }

        /* 当前位置黄点(dd=0,即 yCur 处)。 */
        {
            int seg = seg0 % ROAD_COUNT;
            float vv = (g->roads[seg].x - xmid) / half;
            float bx = (float)cx + vv * halfw;
            fb_fill_rect(v, (int)bx - 2, yCur - 2, (int)bx + 2, yCur + 2, 0xffd23cu);
        }
    }
}

/* 左上角缩略图:环形俯视地图 + 红点(当前位置)。只在 PLAYING/PAUSED 画,约 1/4×1/4。 */
static void render_minimap(RacingFbView *v)
{
    const RacingGame *g = v->game;
    enum { W = WIN_WIDTH / 4, H = WIN_HEIGHT / 4, X0 = 4, Y0 = 4 };

    if (g->mode != RACING_MODE_PLAYING && g->mode != RACING_MODE_PAUSED) {
        return;
    }

    /* 半透明底板 + 边框(inclusive 坐标)。 */
    fb_fill_rect_alpha(v, X0, Y0, X0 + W - 1, Y0 + H - 1, 0x0d1730u, 170);
    fb_fill_rect(v, X0, Y0, X0 + W - 1, Y0, 0x7f97c7u);
    fb_fill_rect(v, X0, Y0 + H - 1, X0 + W - 1, Y0 + H - 1, 0x7f97c7u);
    fb_fill_rect(v, X0, Y0, X0, Y0 + H - 1, 0x7f97c7u);
    fb_fill_rect(v, X0 + W - 1, Y0, X0 + W - 1, Y0 + H - 1, 0x7f97c7u);

    render_track_topdown(v, g, X0, Y0, W, H, 1);
}

/* 文字 HUD：圈数(红)、时间(青)、能量条、胜利成绩(黄)，左上角缩略图，以及模式面板。 */
static void render_hud(RacingFbView *v)
{
    const RacingGame *g = v->game;
    char buf[64];

    /* HUD 文字挪到顶部居中，给左上角缩略图让位。 */
    snprintf(buf, sizeof(buf), "Lap %d/3", g->lap);
    fb_draw_text_centered(v, WIN_WIDTH / 2, 6, buf, 0xff2020u);
    snprintf(buf, sizeof(buf), "%ds", g->elapsedMs / 1000);
    fb_draw_text_centered(v, WIN_WIDTH / 2, 26, buf, 0x00ffffu);
    render_energy(v, g->energy);
    if (g->boosting) {
        fb_draw_text_centered(v, WIN_WIDTH - 48, 6, "BOOST", 0xff8800u);
    }
    if (g->mode == RACING_MODE_WIN) {
        snprintf(buf, sizeof(buf), "Win %ds", g->finalSeconds);
        fb_draw_text_centered(v, WIN_WIDTH / 2, 46, buf, 0xffd647u);
    }
    render_minimap(v);
    render_mode_overlay(v);
}

static void render_button(RacingFbView *v, int x, int y, int w, int h,
                          const char *title, const char *hint, uint32_t color)
{
    fb_fill_rect_alpha(v, x, y, x + w - 1, y + h - 1, 0x102342u, 232);
    fb_fill_rect(v, x, y, x + w - 1, y, color);
    fb_fill_rect(v, x, y + h - 1, x + w - 1, y + h - 1, color);
    fb_fill_rect(v, x, y, x, y + h - 1, color);
    fb_fill_rect(v, x + w - 1, y, x + w - 1, y + h - 1, color);
    fb_draw_text_centered(v, x + w / 2, y + 10, title, 0xffffffu);
    fb_draw_text_centered(v, x + w / 2, y + 32, hint, 0xbfd1ffu);
}

static const char *voice_state_name(int state)
{
    switch (state) {
    case 1:
        return "Starting";
    case 2:
        return "WiFi setup";
    case 3:
        return "Idle";
    case 4:
        return "Connecting";
    case 5:
        return "Listening";
    case 6:
        return "Speaking";
    case 7:
        return "Upgrading";
    case 8:
        return "Activating";
    case 9:
        return "Fatal error";
    default:
        return "Unknown";
    }
}

static uint32_t voice_state_color(int state)
{
    if (state == 3 || state == 5 || state == 6) {
        return 0x7fffc0u;
    }
    if (state == 1 || state == 4 || state == 8) {
        return 0xffcc47u;
    }
    if (state == 9) {
        return 0xff5d5du;
    }
    return 0xbfd1ffu;
}

/* 难度指示:三格方块,填充数 = 难度(简单 1 / 中等 2 / 困难 3),颜色随难度。 */
static void draw_difficulty_fb(RacingFbView *v, int cx, int cy, int mapIndex)
{
    const int n = 3;
    const int sz = 12;
    const int gap = 8;
    const uint32_t col[3] = {0x50c870u, 0xf0c846u, 0xe65a50u};
    int totalW = n * sz + (n - 1) * gap;
    int x0 = cx - totalW / 2;
    int filled = mapIndex + 1;
    int i;
    for (i = 0; i < n; i++)
    {
        uint32_t c = (i < filled) ? col[mapIndex] : 0x46506eu;
        int sx = x0 + i * (sz + gap);
        fb_fill_rect(v, sx, cy, sx + sz - 1, cy + sz - 1, c);
    }
}

static void render_menu_background(RacingFbView *v)
{
    fb_clear(v, COL_SKY);
    render_sky_decor(v);
    fb_fill_rect_alpha(v, 0, WIN_HEIGHT / 2, WIN_WIDTH - 1, WIN_HEIGHT - 1,
                       0x102342u, 70);
}

static void render_gyro_rows(RacingFbView *v, const Jy60Sample *sample)
{
    char buf[96];
    int y = 72;

    if (sample == NULL || !sample->valid) {
        fb_draw_text_centered(v, WIN_WIDTH / 2, 126, "Waiting for JY60 data", 0xffffffu);
        fb_draw_text_centered(v, WIN_WIDTH / 2, 154, "/dev/uart1 9600 8N1", 0xbfd1ffu);
        return;
    }

    snprintf(buf, sizeof(buf), "Frames %u", sample->frame_count);
    fb_draw_text(v, 28, y, buf, 0xbfd1ffu);
    y += 28;
    snprintf(buf, sizeof(buf), "ACC  X:%6.2fg Y:%6.2fg Z:%6.2fg",
             sample->acc_x_g, sample->acc_y_g, sample->acc_z_g);
    fb_draw_text(v, 28, y, buf, 0xffffffu);
    y += 28;
    snprintf(buf, sizeof(buf), "GYRO X:%6.1f Y:%6.1f Z:%6.1f dps",
             sample->gyro_x_dps, sample->gyro_y_dps, sample->gyro_z_dps);
    fb_draw_text(v, 28, y, buf, 0xffffffu);
    y += 28;
    snprintf(buf, sizeof(buf), "ANGLE R:%6.1f P:%6.1f Y:%6.1f deg",
             sample->roll_deg, sample->pitch_deg, sample->yaw_deg);
    fb_draw_text(v, 28, y, buf, 0xffffffu);
}

/****************************************************************************
 * Public API
 ****************************************************************************/

RacingFbView *racing_fb_create(RacingGame *game)
{
    RacingFbView *v;

    if (game == NULL) {
        return NULL;
    }

    v = (RacingFbView *)calloc(1, sizeof(*v));
    if (v == NULL) {
        return NULL;
    }
    v->game = game;
    v->fd = -1;

    v->fd = open(CONFIG_EXAMPLES_RACING_FB_DEVPATH, O_RDWR);
    if (v->fd < 0) {
        fprintf(stderr, "[RACING] open %s failed: %d\n", CONFIG_EXAMPLES_RACING_FB_DEVPATH, errno);
        free(v);
        return NULL;
    }

    if (ioctl(v->fd, FBIOGET_VIDEOINFO, (unsigned long)((uintptr_t)&v->vinfo)) < 0) {
        fprintf(stderr, "[RACING] ioctl(FBIOGET_VIDEOINFO) failed: %d\n", errno);
        close(v->fd);
        free(v);
        return NULL;
    }

    printf("[RACING] VideoInfo: %ux%u fmt=%u nplanes=%u\n",
           v->vinfo.xres, v->vinfo.yres, v->vinfo.fmt, v->vinfo.nplanes);

    if (fbdev_get_pinfo(v->fd, &v->pinfo) < 0) {
        close(v->fd);
        free(v);
        return NULL;
    }

    v->fbmem = mmap(NULL, v->pinfo.fblen, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_FILE, v->fd, 0);
    if (v->fbmem == MAP_FAILED) {
        fprintf(stderr, "[RACING] mmap failed: %d\n", errno);
        v->fbmem = NULL;
        close(v->fd);
        free(v);
        return NULL;
    }

    v->act_fbmem = v->fbmem;
    v->double_buffered = (v->pinfo.yres_virtual == (v->vinfo.yres * 2));
    if (v->double_buffered) {
        if (fb_init_mem2(v) < 0) {
            v->double_buffered = 0;
            v->fbmem2 = NULL;
        }
    }

    v->draw_w = (int)v->vinfo.xres;
    if (v->draw_w > WIN_WIDTH) { v->draw_w = WIN_WIDTH; }
    v->draw_h = (int)v->vinfo.yres;
    if (v->draw_h > WIN_HEIGHT) { v->draw_h = WIN_HEIGHT; }

    printf("[RACING] mapped %p bpp=%u stride=%u double=%d draw=%dx%d\n",
           v->fbmem, v->pinfo.bpp, v->pinfo.stride, v->double_buffered, v->draw_w, v->draw_h);

    v->img_nailong  = img_load("nailong.raw");
    v->img_car      = img_load("car.raw");
    v->img_head     = img_load("nailong_head.raw");
    v->img_mtn_far  = img_load("mountain_far.raw");
    v->img_mtn_near = img_load("mountain_near.raw");
    v->img_cloud    = img_load("cloud.raw");
    v->img_nailong_bupt = img_load("nailong_bupt.raw");
    v->img_bg_bupt      = img_load("bg_bupt.raw");
    v->img_tree         = img_load("tree.raw");
    v->img_house_a      = img_load("house_a.raw");
    v->img_house_b      = img_load("house_b.raw");

    return v;
}

void racing_fb_delete(RacingFbView *v)
{
    if (v == NULL) {
        return;
    }
    img_free(v->img_nailong);
    img_free(v->img_car);
    img_free(v->img_head);
    img_free(v->img_mtn_far);
    img_free(v->img_mtn_near);
    img_free(v->img_cloud);
    img_free(v->img_nailong_bupt);
    img_free(v->img_bg_bupt);
    img_free(v->img_tree);
    img_free(v->img_house_a);
    img_free(v->img_house_b);

    /* 退出前清屏到黑并推送一次,避免残留在最后一帧、释放显示。 */
    if (v->fbmem != NULL && v->fbmem != MAP_FAILED) {
        fb_clear(v, 0x000000u);
        fb_present(v);
        munmap(v->fbmem, v->pinfo.fblen);
    }
    if (v->fd >= 0) {
        close(v->fd);
    }
    free(v);
}

static long fb_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

void racing_fb_render(RacingFbView *v)
{
    static int rcnt;
    static long acc_draw;
    static long acc_present;
    long t0;
    long t1;
    long t2;

    if (v == NULL) {
        return;
    }

    t0 = fb_now_ms();
    /* 背景：地图“一路邮你”用北邮校门照，其余用天空 + 云/山。 */
    if (v->game->mapIndex == 1 && v->img_bg_bupt != NULL) {
        fb_blit_scaled(v, v->img_bg_bupt, 0, 0, WIN_WIDTH, WIN_HEIGHT);
    } else {
        fb_clear(v, COL_SKY);       /* 背景：整屏天空 */
        render_sky_decor(v);        /* 云 + 远近山（视差） */
    }
    render_track(v);            /* 路面：草地 + 路肩 + 沥青 + 终点旗 */
    render_world_decor(v);      /* 路外装饰：房子 + 树 */
    render_collectibles(v);     /* 奶龙（贴图或几何） */
    render_car(v);              /* 车内视角 cockpit */
    render_hud(v);              /* 文字 HUD + 能量条 + 模式面板 */
    render_hit_flash(v);        /* 命中跳脸闪屏(最上层) */
    t1 = fb_now_ms();
    fb_present(v);              /* 翻页 / SPI LCD 刷新 */
    t2 = fb_now_ms();

    /* 拆分计时:CPU 渲染 vs 推屏(present)。每 20 帧打印一次,定位卡顿根因。 */
    acc_draw += t1 - t0;
    acc_present += t2 - t1;
    if (++rcnt >= 20) {
        printf("[RACING-RENDER] draw=%ldms present=%ldms (avg/20)\n",
               acc_draw / 20, acc_present / 20);
        rcnt = 0;
        acc_draw = 0;
        acc_present = 0;
    }
}

void racing_fb_render_menu(RacingFbView *v)
{
    const RacingGame *g;

    if (v == NULL) {
        return;
    }
    g = v->game;

    render_menu_background(v);

    if (g->mode == RACING_MODE_MAP_SELECT)
    {
        /* 关卡选择:中间完整赛道缩略图(起点左下→终点右上),两侧 < > 切图,下方难度。 */
        int tw = 300;
        int th = 176;
        int tx = (WIN_WIDTH - tw) / 2;
        int ty = 60;
        char mbuf[48];
        fb_draw_text_centered(v, WIN_WIDTH / 2, 14, "SELECT TRACK", 0xffffffu);
        fb_fill_rect_alpha(v, tx - 4, ty - 4, tx + tw + 3, ty + th + 3, 0x0d1730u, 210);
        render_track_topdown(v, g, tx, ty, tw, th, 0);
        render_button(v, 14, ty + th / 2 - 26, 56, 52, "<", "prev", 0x47a8ffu);
        render_button(v, WIN_WIDTH - 14 - 56, ty + th / 2 - 26, 56, 52, ">", "next", 0x47a8ffu);
        snprintf(mbuf, sizeof(mbuf), "%s   %s", racing_game_map_name_ascii(g->mapIndex),
                 g->mapIndex == 0 ? "Easy" : (g->mapIndex == 1 ? "Medium" : "Hard"));
        fb_draw_text_centered(v, WIN_WIDTH / 2, ty + th + 12, mbuf, 0x7fffc0u);
        draw_difficulty_fb(v, WIN_WIDTH / 2, ty + th + 36, g->mapIndex);
        fb_draw_text_centered(v, WIN_WIDTH / 2, WIN_HEIGHT - 14,
                              "< > switch | center START | top BACK", 0xbfd1ffu);
        fb_present(v);
        return;
    }

    if (g->mode == RACING_MODE_CONTROL_SELECT)
    {
        /* 操作选择:Original / Gyro / Test,当前高亮(自身色),其余暗色。 */
        const char *names[3] = {"Original", "Gyro", "Test"};
        const char *hints[3] = {"Touch L/R + GPIO", "JY60 tilt L/R", "Show JY60 data"};
        uint32_t col[3] = {0x55d37au, 0x47a8ffu, 0xffcc47u};
        int bw = 300;
        int bh = 52;
        int bx = (WIN_WIDTH - bw) / 2;
        int k;
        fb_draw_text_centered(v, WIN_WIDTH / 2, 16, "SELECT CONTROL", 0xffffffu);
        for (k = 0; k < 3; k++)
        {
            char t[48];
            uint32_t c = (k == g->menuControlMode) ? col[k] : 0x2a3a5au;
            snprintf(t, sizeof(t), "%s %s", (k == g->menuControlMode) ? ">" : " ", names[k]);
            render_button(v, bx, 52 + k * (bh + 12), bw, bh, t, hints[k], c);
        }
        fb_draw_text_centered(v, WIN_WIDTH / 2, WIN_HEIGHT - 14,
                              "< > switch | center OK | top BACK", 0xbfd1ffu);
        fb_present(v);
        return;
    }

    if (g->mode == RACING_MODE_NETWORK_SELECT)
    {
        int bw = 320;
        int bh = 56;
        int bx = (WIN_WIDTH - bw) / 2;
        char sbuf[80];
        fb_draw_text_centered(v, WIN_WIDTH / 2, 18, "NETWORK / VOICE", 0xffffffu);
        snprintf(sbuf, sizeof(sbuf), "Voice: %s", voice_state_name(g->voiceState));
        fb_draw_text_centered(v, WIN_WIDTH / 2, 52, sbuf, voice_state_color(g->voiceState));
        render_button(v, bx, 92, bw, bh, "VOICE STATUS", "started before racing",
                      0x55d37au);
        render_button(v, bx, 164, bw, bh, "WIFI", "SSID iphone17 / 12345678",
                      0x47a8ffu);
        if (g->voiceText[0] != '\0') {
            fb_draw_text_centered(v, WIN_WIDTH / 2, 238, g->voiceText, 0xe6ecffu);
        }
        fb_draw_text_centered(v, WIN_WIDTH / 2, WIN_HEIGHT - 38,
                              "run sh /data/racing_xiaozhi.sh once", 0xbfd1ffu);
        fb_draw_text_centered(v, WIN_WIDTH / 2, WIN_HEIGHT - 16,
                              "voice: say 'network', then 'start'", 0xbfd1ffu);
        fb_present(v);
        return;
    }

    /* 主菜单(START):标题 + 当前操作模式 + 关卡/操作/网络三个按钮。 */
    {
        int bw = 320;
        int bh = 52;
        int bx = (WIN_WIDTH - bw) / 2;
        char mbuf[48];
        char vbuf[80];
        fb_draw_text_centered(v, WIN_WIDTH / 2, 18, "NAILONG  RACING", 0xffd23cu);
        snprintf(mbuf, sizeof(mbuf), "Control: %s",
                 g->menuControlMode == 1 ? "Gyro" : "Original");
        fb_draw_text_centered(v, WIN_WIDTH / 2, 44, mbuf, 0x7fffc0u);
        render_button(v, bx, 68, bw, bh, "TRACK", "Select track", 0x55d37au);
        render_button(v, bx, 68 + bh + 12, bw, bh, "CONTROL", "Select control", 0x47a8ffu);
        render_button(v, bx, 68 + (bh + 12) * 2, bw, bh, "NETWORK", "XiaoZhi voice bridge",
                      0xffcc47u);
        snprintf(vbuf, sizeof(vbuf), "Voice: %s", voice_state_name(g->voiceState));
        fb_draw_text_centered(v, WIN_WIDTH / 2, 262, vbuf, voice_state_color(g->voiceState));
        if (g->voiceText[0] != '\0') {
            fb_draw_text_centered(v, WIN_WIDTH / 2, 284, g->voiceText, 0xe6ecffu);
        }
        fb_draw_text_centered(v, WIN_WIDTH / 2, WIN_HEIGHT - 14,
                              "Btn2: quit | tap Track / Control / Network", 0xbfd1ffu);
        fb_present(v);
    }
}

void racing_fb_render_gyro_test(RacingFbView *v, const Jy60Sample *sample)
{
    if (v == NULL) {
        return;
    }

    render_menu_background(v);
    fb_draw_text_centered(v, WIN_WIDTH / 2, 24, "JY60 Test", 0xffffffu);
    fb_draw_text_centered(v, WIN_WIDTH / 2, 48, "Btn1 / center returns to menu", 0xbfd1ffu);
    fb_fill_rect_alpha(v, 14, 64, WIN_WIDTH - 15, WIN_HEIGHT - 46, 0x0d1730u, 220);
    render_gyro_rows(v, sample);
    fb_present(v);
}
