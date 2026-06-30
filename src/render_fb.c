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
#define COL_GRASS    0x00c700u
#define COL_ROAD_DK  0x696969u
#define COL_ROAD_LT  0x656565u
#define COL_POLE     0xeb2d18u

typedef struct {
    float x;
    float y;
} FbPoint;

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
    RacingImg *img_tree;
    RacingImg *img_house0;    /* 小楼(正面) */
    RacingImg *img_house1;    /* 神庙盒(正面) */
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

static void project_road(Road *road, const ProjectionContext *ctx)
{
    road->tx = (road->x - ctx->camX) * ctx->cosAngle + (road->z - ctx->camZ) * ctx->sinAngle;
    road->tz = -(road->x - ctx->camX) * ctx->sinAngle + (road->z - ctx->camZ) * ctx->cosAngle;

    if (road->tz < 0.1f) {
        road->tz = 0.1f;
    }

    road->scale = 1.0f / road->tz;
    road->X = (1.0f + road->scale * road->tx) * WIN_WIDTH / 2.0f;
    road->Y = (1.0f - road->scale * (road->y - ctx->camY)) * WIN_HEIGHT / 2.0f;
    road->W = road->scale * ROAD_WIDTH * WIN_WIDTH / 2.0f;
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

    fb_fill_quad(v, farLeftOuter, farLeftInner, nearLeftInner, nearLeftOuter, COL_GRASS);
    fb_fill_quad(v, farRightInner, farRightOuter, nearRightOuter, nearRightInner, COL_GRASS);
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
static void draw_finish_flag(RacingFbView *v, const Road *road, const ProjectionContext *ctx)
{
    float z = road->z;
    float halfWidth = ROAD_WIDTH * 0.78f;
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
    project_road(&farRoad, &context);

    for (int offset = VIEW_DISTANCE; offset > 0; offset--) {
        int segment = start + offset;
        int segmentIndex = segment >= ROAD_COUNT ? segment - ROAD_COUNT : segment;
        Road nearRoad = game->roads[nearIndex];
        int nearWrap = segment - 1 >= ROAD_COUNT ? TRACK_LENGTH : 0;
        uint32_t edge = segment % 2 ? 0x000000u : 0xffffffu;
        uint32_t road = segment % 2 ? COL_ROAD_DK : COL_ROAD_LT;

        context.camZ = game->camZ - nearWrap;
        project_road(&nearRoad, &context);

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
            draw_finish_flag(v, &game->roads[ROAD_COUNT - 1], &context);
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

        if (v->img_nailong != NULL) {
            fb_blit_scaled(v, v->img_nailong, x, y, w, h);
        } else {
            draw_nailong(v, x, y, w, h);
        }
    }
}

/* 路边树(空气墙外装饰):按 segDist 从远到近画,近处遮挡远处;无贴图则跳过。 */
static void render_trees(RacingFbView *v)
{
    const RacingGame *g = v->game;

    if (v->img_tree == NULL) {
        return;
    }

    for (int d = VIEW_DISTANCE; d >= 1; d--) {
        for (int i = 0; i < TREE_COUNT; i++) {
            const Tree *t = &g->trees[i];
            int x;
            int y;
            int w;
            int h;

            if (!t->visible || t->segDist != d) {
                continue;
            }

            x = (int)t->p[0].X;
            y = (int)t->p[2].Y;
            w = (int)fabsf(t->p[1].X - t->p[0].X);
            h = (int)fabsf(t->p[0].Y - t->p[2].Y);

            if (t->p[0].tz <= 0.1f || t->p[1].tz <= 0.1f ||
                x >= WIN_WIDTH || y >= WIN_HEIGHT || x + w < 0 || y + h < 0) {
                continue;
            }

            fb_blit_scaled(v, v->img_tree, x, y, w, h);
        }
    }
}

/* 路边房子(空气墙外,立体盒子):按 segDist 从远到近画。type 选 house0/1,side 选正/镜像。
 * 贴地 billboard,和树同一套锚定(路中心深度定位纵向 + bankAngle 倾角)。 */
static void render_houses(RacingFbView *v)
{
    const RacingGame *g = v->game;

    for (int d = VIEW_DISTANCE; d >= 1; d--) {
        for (int i = 0; i < HOUSE_COUNT; i++) {
            const House *h = &g->houses[i];
            const RacingImg *img;
            int x;
            int y;
            int w;
            int hh;

            if (!h->visible || h->segDist != d) {
                continue;
            }

            img = (h->type == 0) ? v->img_house0 : v->img_house1;
            if (img == NULL) {
                continue;
            }

            x = (int)h->p[0].X;
            y = (int)h->p[2].Y;
            w = (int)fabsf(h->p[1].X - h->p[0].X);
            hh = (int)fabsf(h->p[0].Y - h->p[2].Y);

            if (h->p[0].tz <= 0.1f || h->p[1].tz <= 0.1f ||
                x >= WIN_WIDTH || y >= WIN_HEIGHT || x + w < 0 || y + hh < 0) {
                continue;
            }

            fb_blit_scaled(v, img, x, y, w, hh);
        }
    }
}

/****************************************************************************
 * 背景与前景贴图(对齐 render_sdl.c)：全屏高条带背景、车内视角车、跳脸。
 ****************************************************************************/

/* 一条横向视差条带：把 img 缩放到 SKY_DRAW_H 高放在 y，按 camx*factor 水平平铺。
 * 只画天空区(地平线略下),下半屏会被草地/路面覆盖，省掉一半背景开销。 */
#define SKY_DRAW_H (WIN_HEIGHT / 2 + 20)
/* 平地默认地平线 Y(camY=2000、~VIEW_DISTANCE 处约 190)。天空装饰以此为锚，
 * horizon 随起伏/转向上下移动时整组云/山跟着平移，避免山与地面脱节而漂浮。可按观感微调。 */
#define SKY_HORIZON_REF 190

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

/* 地平线 Y：最远可见路面(farIndex)的投影 Y，与 render_track 初始 farRoad 一致。 */
static float sky_horizon_y(RacingFbView *v)
{
    const RacingGame *game = v->game;
    int start = racing_game_start_segment(game);
    int farIndex = (start + VIEW_DISTANCE) % ROAD_COUNT;
    int farWrap = (start + VIEW_DISTANCE >= ROAD_COUNT) ? TRACK_LENGTH : 0;
    ProjectionContext ctx = make_projection_context(game->camX, game->camY, game->camZ, game->angle);
    Road farRoad = game->roads[farIndex];

    ctx.camZ = game->camZ - farWrap;
    project_road(&farRoad, &ctx);
    return farRoad.Y;
}

/* 天空装饰：云/远山/近山作为全屏高条带，y 偏移与视差因子对齐 SDL。
 * 纵向按地平线(sky_horizon_y)相对 SKY_HORIZON_REF 的偏移整体平移，跟着远处路面走，
 * 避免转向/起伏时地平线移动而山固定导致山"漂浮"。 */
static void render_sky_decor(RacingFbView *v)
{
    int camx = v->game->camX;
    int dy = (int)(sky_horizon_y(v) - SKY_HORIZON_REF);

    fb_blit_strip(v, v->img_cloud, 0 + dy, camx, 0.01f);
    fb_blit_strip(v, v->img_mtn_far, 50 + dy, camx, 0.018f);
    fb_blit_strip(v, v->img_mtn_near, 75 + dy, camx, 0.028f);
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

/* 命中后奶龙跳脸：整屏闪一下。 */
static void render_hit_flash(RacingFbView *v)
{
    if (v->game->hitFrames > 0 && v->img_head != NULL) {
        fb_blit_scaled(v, v->img_head, 0, 0, WIN_WIDTH, WIN_HEIGHT);
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

/* 撞墙反馈：在撞墙一侧画红色半透明边带，alpha 随剩余帧衰减，给冲击感。 */
static void render_wall_hit(RacingFbView *v)
{
    const RacingGame *g = v->game;
    int edge;
    int a;

    if (g->wallHitFrames <= 0) {
        return;
    }

    edge = WIN_WIDTH / 8;
    a = g->wallHitFrames * 255 / AIR_WALL_FEEDBACK_FRAMES;
    if (a > 200) {
        a = 200;
    }

    if (g->wallHitSide <= 0) {
        fb_fill_rect_alpha(v, 0, 0, edge - 1, WIN_HEIGHT - 1, 0xff2020u, (uint8_t)a);
    }
    if (g->wallHitSide >= 0) {
        fb_fill_rect_alpha(v, WIN_WIDTH - edge, 0, WIN_WIDTH - 1, WIN_HEIGHT - 1, 0xff2020u, (uint8_t)a);
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
        fb_fill_rect(v, panelX + 6, sy, panelX + 6 + BAR_W - 1, sy + BAR_H - 1, 0x1d63ffu);
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
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 70, "Tap center to resume", body);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 96, "Btn1 resume", hint);
    } else if (g->mode == RACING_MODE_WIN) {
        snprintf(buf, sizeof(buf), "Finish in %ds", g->finalSeconds);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 24, buf, title);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 70, "Tap center to restart", body);
        fb_draw_text_centered(v, WIN_WIDTH / 2, py + 96, "Btn1 restart", hint);
    }
}

/* 文字 HUD：圈数(红)、时间(青)、能量条、胜利成绩(黄)，以及模式面板。 */
static void render_hud(RacingFbView *v)
{
    const RacingGame *g = v->game;
    char buf[64];

    snprintf(buf, sizeof(buf), "Lap %d/3", g->lap);
    fb_draw_text(v, 8, 8, buf, 0xff2020u);
    snprintf(buf, sizeof(buf), "%ds", g->elapsedMs / 1000);
    fb_draw_text(v, 8, 38, buf, 0x00ffffu);
    render_energy(v, g->energy);
    if (g->mode == RACING_MODE_WIN) {
        snprintf(buf, sizeof(buf), "Win %ds", g->finalSeconds);
        fb_draw_text(v, 8, 68, buf, 0xffd647u);
    }
    render_mode_overlay(v);
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
    v->img_tree     = img_load("tree.raw");
    v->img_house0 = img_load("house0.raw");
    v->img_house1 = img_load("house1.raw");

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
    img_free(v->img_tree);
    img_free(v->img_house0);
    img_free(v->img_house1);

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
    fb_clear(v, COL_SKY);       /* 背景：整屏天空 */
    render_sky_decor(v);        /* 云 + 远近山（视差） */
    render_track(v);            /* 路面：草地 + 路肩 + 沥青 + 终点旗 */
    render_trees(v);            /* 路边树(墙外装饰) */
    render_houses(v);           /* 路边房子(墙外立体盒子) */
    render_collectibles(v);     /* 奶龙（贴图或几何） */
    render_car(v);              /* 车内视角 cockpit */
    render_wall_hit(v);         /* 撞墙红色边带反馈 */
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
