#include "game.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct ProjectionContext
{
    int camX;
    int camY;
    int camZ;
    float sinAngle;
    float cosAngle;
} ProjectionContext;

/* 摄像机投影上下文，路段和奶龙投影都复用这组参数。 */
static ProjectionContext make_projection_context(int camX, int camY, int camZ, float angle)
{
    ProjectionContext context;
    context.camX = camX;
    context.camY = camY;
    context.camZ = camZ;
    context.sinAngle = sinf(angle);
    context.cosAngle = cosf(angle);
    return context;
}

static Point make_point(float x, float y, float z)
{
    Point point;
    point.x = x;
    point.y = y;
    point.z = z;
    point.X = 0.0f;
    point.Y = 0.0f;
    point.scale = 0.0f;
    point.tx = 0.0f;
    point.tz = 0.0f;
    return point;
}

static void project_point(Point *point, const ProjectionContext *context)
{
    point->tx = (point->x - context->camX) * context->cosAngle + (point->z - context->camZ) * context->sinAngle;
    point->tz = -(point->x - context->camX) * context->sinAngle + (point->z - context->camZ) * context->cosAngle;

    if (point->tz < 0.1f)
    {
        point->tz = 0.1f;
    }

    point->scale = 1.0f / point->tz;
    point->X = (1.0f + point->scale * point->tx) * WIN_WIDTH / 2.0f;
    point->Y = (1.0f - point->scale * (point->y - context->camY)) * WIN_HEIGHT / 2.0f;
}

static Road make_road(float x, float y, float z)
{
    Road road;
    road.x = x;
    road.y = y;
    road.z = z;
    road.X = 0.0f;
    road.Y = 0.0f;
    road.W = 0.0f;
    road.scale = 0.0f;
    road.tz = 0.0f;
    road.tx = 0.0f;
    return road;
}

/* 赛道是预先生成好的离散段，曲线和起伏都固定在这张表里。 */
/* 横向偏移 x 由每段 curve 累加得到，最后统一 *45 缩放成世界坐标。 */

/* 局部确定性 LCG：只用于生成蜿蜒赛道的随机段长/振幅。不碰全局 rand()，
 * 这样奶龙位置仍按启动时刻种子随机，而每张地图的赛道形状固定、可复现。 */
static float track_rand(unsigned int *state)
{
    *state = (*state * 1664525u + 1013904223u);
    return (float)((*state >> 8) & 0xFFFFu) / (float)0x10000u;   /* 0..1 */
}

/* 每张地图的生成参数。secLen 区间决定弯道疏密，curveAmp 区间决定弯道急缓，
 * hill* 决定上下坡幅度/频率，widthScale 决定路面宽度，seed 让形状固定。 */
typedef struct
{
    int   secLenMin;
    int   secLenMax;
    float curveAmpMin;
    float curveAmpMax;
    float hillAmp;
    float hillFreq;
    float widthScale;
    unsigned int seed;
} TrackParams;

/* 三条赛道由易到难：越难段越短（弯更密）、振幅越大（弯更急）、上下坡更陡
 * 更高、路更窄。widthScale 同时被 racing_game_set_map 用来设置 roadWidth。 */
static const TrackParams g_track_params[RACING_MAP_COUNT] =
{
    { 110, 170, 0.5f, 1.1f, 1300.0f, 4.0f, 1.3f, 11111u },  /* 简单：缓弯缓坡宽路 */
    { 80,  140, 0.9f, 2.0f, 1800.0f, 6.0f, 1.0f, 22222u },  /* 中等：中弯中坡 */
    { 68,  120, 1.1f, 2.4f, 1900.0f, 7.0f, 0.82f, 33333u }, /* 困难：急弯陡坡窄路(已下调) */
};

/* 分段蜿蜒：把整条路切成若干随机长度的段，每段内 curve 走一个完整正弦周期
 * （净漂移为 0，赛道不会整体飘走），但段长与振幅/方向都是随机的，所以弯道
 * 疏密、急缓全不规则，像真实赛道而非单一正弦。上下坡用 3 层不同频率/相位的
 * 正弦叠加（频率比 1:2.3:4.7 不可约），起伏自然、非单一正弦。 */
static void build_track_winding(Road roads[ROAD_COUNT], const TrackParams *p)
{
    const float two_pi = 6.2831853f;
    unsigned int state = p->seed;
    float x = 0.0f;
    int i = 0;

    while (i < ROAD_COUNT)
    {
        int secLen;
        float sign;
        float amp;
        int j;

        secLen = p->secLenMin + (int)(track_rand(&state) *
                    (float)(p->secLenMax - p->secLenMin + 1));
        if (secLen < 1)
        {
            secLen = 1;
        }
        if (i + secLen > ROAD_COUNT)
        {
            secLen = ROAD_COUNT - i;
        }

        sign = (track_rand(&state) < 0.5f) ? -1.0f : 1.0f;
        amp = (p->curveAmpMin + track_rand(&state) *
                (p->curveAmpMax - p->curveAmpMin)) * sign;

        for (j = 0; j < secLen; j++)
        {
            float t;
            float curve;
            int idx;
            float zi;
            float hill;

            t = (float)j / (float)secLen;            /* 段内 0..1 */
            curve = amp * sinf(two_pi * t);          /* 完整周期 -> 净漂移 0 */
            x += curve;

            idx = i + j;
            zi = (float)((1 + idx) * SEG_LENGTH);
            hill = p->hillAmp *
                (0.85f * sinf(two_pi * p->hillFreq * (float)idx / (float)ROAD_COUNT + 0.7f) +
                 0.15f * sinf(two_pi * p->hillFreq * 2.0f * (float)idx / (float)ROAD_COUNT + 2.1f));
            roads[idx] = make_road(x * 45.0f, hill, zi);
        }

        i += secLen;
    }
}

static void build_track(Road roads[ROAD_COUNT], int mapIndex)
{
    int idx = (mapIndex < 0 || mapIndex >= RACING_MAP_COUNT) ? 0 : mapIndex;
    build_track_winding(roads, &g_track_params[idx]);
}

/* 奶龙本体先用四个角定义，渲染端再按投影结果画成矩形。 */
static Nailong make_nailong(float x, float y, float z)
{
    Nailong nailong;
    nailong.p[0] = make_point(x, y, z);
    nailong.p[1] = make_point(x + 1200.0f, y, z);
    nailong.p[2] = make_point(x + 1200.0f, y + 1800.0f, z);
    nailong.p[3] = make_point(x, y + 1800.0f, z);
    nailong.eaten = false;
    nailong.respawnMs = 0;
    return nailong;
}

/* 把奶龙四个顶点投到屏幕坐标，供渲染层直接使用。 */
static void project_nailong(Nailong *nailong, const ProjectionContext *context)
{
    for (int i = 0; i < 4; i++)
    {
        project_point(&nailong->p[i], context);
    }
}

/* 奶龙重生到指定赛段附近，位置保留一点随机性。 */
static void reset_nailong(Nailong *nailong, const Road *road, float width)
{
    float x = road->x - width / 2.0f + (float)(rand() % 1500);
    *nailong = make_nailong(x, road->y, road->z);
}

/* 纯判定函数：这里只负责判断是否撞到奶龙。 */
static bool collect_nailong(Nailong *nailong, int camX, int camZ)
{
    if (nailong->eaten)
    {
        return false;
    }

    if (camZ < nailong->p[0].z + NAILONG_HIT_Z_MARGIN &&
        camZ > nailong->p[0].z - NAILONG_HIT_Z_MARGIN &&
        camX > nailong->p[0].x - NAILONG_HIT_X_MARGIN &&
        camX < nailong->p[1].x + NAILONG_HIT_X_MARGIN)
    {
        nailong->eaten = true;
        return true;
    }

    return false;
}

/* 每局开始时重新分配奶龙位置，让三次收集节奏更均匀。 */
static void reset_collectibles(Nailong nailongs[COLLECTIBLE_COUNT], const Road roads[ROAD_COUNT], int positions[COLLECTIBLE_COUNT], float width)
{
    positions[0] = rand() % 500 + 200;
    positions[1] = rand() % 500 + 700;
    positions[2] = rand() % 500 + 1200;

    for (int i = 0; i < COLLECTIBLE_COUNT; i++)
    {
        reset_nailong(&nailongs[i], &roads[positions[i]], width);
    }
}

static RacingInput empty_input(void)
{
    static const RacingInput input = {0};
    return input;
}

/* 清空一局内的运行时状态，保留赛道和随机结果。 */
static void reset_runtime_state(RacingGame *game)
{
    game->mode = RACING_MODE_START;
    game->camX = 0;
    game->camY = 2000;
    game->camZ = 0;
    game->energy = 700;
    game->lap = 0;
    game->hitFrames = 0;
    game->finalSeconds = 0;
    game->flyFrames = 0;
    game->elapsedMs = 0;
    game->angle = 0.0f;
    game->distance = 0.0f;
    game->turnLeft = false;
    game->turnRight = false;
    game->isOut = false;
    game->isFlying = false;
    game->wallHitFrames = 0;
    game->wallHitSide = 0;
}

/* 摄像机高度贴着当前路面走，飞行状态则独立抬高。 */
static void refresh_camera_height(RacingGame *game)
{
    int start = racing_game_start_segment(game);

    if (game->isFlying)
    {
        if (game->camY < 6000)
        {
            game->camY += 80;
        }
        return;
    }

    if (game->camY >= 2000 + (int)game->roads[start].y)
    {
        game->camY -= 100;
    }

    if (game->camY < 2000 + (int)game->roads[start].y)
    {
        game->camY = 2000 + (int)game->roads[start].y;
    }
}

/* 奶龙被吃到后补能量，并触发短暂跳脸反馈。
 * collectibleRespawnMs=0：被吃后不重生，维持一局固定 N 个。 */
static void update_collectibles(RacingGame *game, int deltaMs)
{
    int startSeg = racing_game_start_segment(game);

    for (int i = 0; i < COLLECTIBLE_COUNT; i++)
    {
        Nailong *n = &game->collectibles[i];
        if (collect_nailong(n, game->camX, game->camZ))
        {
            game->energy += 100;
            game->hitFrames = 15;
            n->respawnMs = game->collectibleRespawnMs;
        }
        else if (n->eaten && n->respawnMs > 0)
        {
            n->respawnMs -= deltaMs;
            if (n->respawnMs <= 0)
            {
                int ahead = (startSeg + 80) % ROAD_COUNT;
                reset_nailong(n, &game->roads[ahead], game->roadWidth);
                game->collectiblePositions[i] = ahead;
            }
        }
    }
}

/* 赛道首尾相接，camZ 越过 TRACK_LENGTH 就进入下一圈。 */
static void update_lap(RacingGame *game)
{
    if (game->camZ >= TRACK_LENGTH)
    {
        game->camZ -= TRACK_LENGTH;
        game->lap++;
        reset_collectibles(game->collectibles, game->roads, game->collectiblePositions, game->roadWidth);

        if (game->lap >= 3)
        {
            game->finalSeconds = game->elapsedMs / 1000;
            game->mode = RACING_MODE_WIN;
        }
    }

    if (game->camZ < 0)
    {
        game->camZ += TRACK_LENGTH;
    }
}

/* 统一处理转向、前进、刹车、加速和飞行的移动逻辑。 */
static void update_movement(RacingGame *game, const RacingInput *input)
{
    float forwardSin;
    float forwardCos;

    if (input->left)
    {
        game->angle += 0.018f;
        game->turnLeft = true;
    }
    else
    {
        game->turnLeft = false;
    }

    if (input->right)
    {
        game->angle -= 0.018f;
        game->turnRight = true;
    }
    else
    {
        game->turnRight = false;
    }

    /* 转向角夹在 ±1.4 弧度(约 80°)：远大于旧的 1rad，但低于 90° 退化区。
     * 一旦 |angle| >= 90°，前方路面的投影深度 tz 会变负、被夹成 0.1，
     * 整条路塌缩成一点 → 旋转后空屏。所以允许大幅转向但不许转到面朝后。 */
    if (game->angle > 1.4f)
    {
        game->angle = 1.4f;
    }
    if (game->angle < -1.4f)
    {
        game->angle = -1.4f;
    }

    forwardCos = cosf(-game->angle);
    forwardSin = sinf(-game->angle);

    game->boosting = false;
    if (!game->isFlying)
    {
        if (input->accelerate)
        {
            int speed = game->isOut ? SEG_LENGTH : 3 * SEG_LENGTH;
            float distanceStep = game->isOut ? 0.2f : 0.6f;

            game->camZ += (int)(speed * forwardCos);
            game->camX += (int)(speed * forwardSin);
            game->distance += distanceStep;

            if (input->boost && game->energy > 0)
            {
                int boostSpeed = game->isOut ? 0 : 2 * SEG_LENGTH;
                game->camZ += (int)(boostSpeed * forwardCos);
                game->camX += (int)(boostSpeed * forwardSin);
                game->distance += game->isOut ? 0.0f : 0.4f;
                game->energy -= game->isOut ? 1 : 2;
                game->boosting = true;
            }
        }

        if (input->brake)
        {
            game->camZ -= (int)(SEG_LENGTH * forwardCos);
            game->camX -= (int)(SEG_LENGTH * forwardSin);
            game->distance -= 0.2f;

            if (input->boost)
            {
                game->camZ -= SEG_LENGTH;
            }
        }

        if (input->fly && game->energy >= 1000)
        {
            game->energy -= 1000;
            game->isFlying = true;
        }
    }
    else
    {
        if (input->accelerate)
        {
            game->camZ += (int)(8 * SEG_LENGTH * forwardCos);
            game->camX += (int)(8 * SEG_LENGTH * forwardSin);
            game->distance += 1.6f;
        }

        game->flyFrames++;
        if (game->flyFrames > 300)
        {
            game->isFlying = false;
            game->flyFrames = 0;
        }
    }

    if (game->energy < 0)
    {
        game->energy = 0;
    }
}

static float slow_grass_half_width(const RacingGame *game)
{
    return game->roadWidth * SLOW_GRASS_HALF_SCALE;
}

static float air_wall_half_width(const RacingGame *game)
{
    return game->roadWidth * AIR_WALL_HALF_SCALE;
}

/* 路边树/房子:空气墙外随机分布,纯装饰无碰撞。centerWorldX = 路中心 ± (墙距+随机+半宽)。 */
static Tree make_tree(float centerWorldX, int segment)
{
    Tree t;
    t.centerWorldX = centerWorldX;
    t.segment = segment;
    t.segDist = 0;
    t.visible = false;
    return t;
}

static void build_trees(RacingGame *game)
{
    int i;
    float wallHalf = air_wall_half_width(game);

    for (i = 0; i < TREE_COUNT; i++)
    {
        int segment = rand() % ROAD_COUNT;
        int side = (rand() & 1) ? 1 : -1;
        int beyond = TREE_MIN_BEYOND + rand() % TREE_SPREAD;
        float centerWorldX = game->roads[segment].x +
                             side * (wallHalf + (float)beyond + TREE_WORLD_W / 2.0f);
        game->trees[i] = make_tree(centerWorldX, segment);
    }
}

static House make_house(float centerWorldX, int segment, int type, int side)
{
    House h;
    h.centerWorldX = centerWorldX;
    h.segment = segment;
    h.type = type;
    h.side = side;
    h.segDist = 0;
    h.visible = false;
    return h;
}

static void build_houses(RacingGame *game)
{
    int i;
    float wallHalf = air_wall_half_width(game);

    for (i = 0; i < HOUSE_COUNT; i++)
    {
        int segment = rand() % ROAD_COUNT;
        int side = (rand() & 1) ? 1 : -1;
        int type = rand() & 1;
        int beyond = HOUSE_MIN_BEYOND + rand() % HOUSE_SPREAD;
        float centerWorldX = game->roads[segment].x +
                             side * (wallHalf + (float)beyond + HOUSE_WORLD_W / 2.0f);
        game->houses[i] = make_house(centerWorldX, segment, type, side);
    }
}

/* 空气墙:把车钳在路缘外侧硬边界内,撞墙向内弹回 + 扣能量 + 触发红边反馈。 */
static void apply_air_wall(RacingGame *game, int start)
{
    int centerX = game->roads[start].x;
    float wallHalf = air_wall_half_width(game);
    int leftWall = (int)(centerX - wallHalf);
    int rightWall = (int)(centerX + wallHalf);
    int side = 0;

    if (game->camX <= leftWall)
    {
        game->camX = leftWall + AIR_WALL_BOUNCE;
        side = -1;
    }
    else if (game->camX >= rightWall)
    {
        game->camX = rightWall - AIR_WALL_BOUNCE;
        side = 1;
    }

    if (side != 0)
    {
        game->wallHitSide = side;
        game->wallHitFrames = AIR_WALL_FEEDBACK_FRAMES;
        game->energy -= AIR_WALL_ENERGY_COST;
        if (game->energy < 0)
        {
            game->energy = 0;
        }
    }
    else if (game->wallHitFrames > 0)
    {
        game->wallHitFrames--;
    }
}

/* 每帧把视野内的树投成贴地 billboard 4 角(无 bankAngle,不随转向倾斜)。
 * 纵向用本赛段路中心深度定位(避免侧向偏移导致深度失真而"飘"),横向按路宽偏移。 */
static void project_trees(RacingGame *game, int start, const ProjectionContext *context)
{
    int i;
    for (i = 0; i < TREE_COUNT; i++)
    {
        Tree *t = &game->trees[i];
        int segDist = (t->segment - start + ROAD_COUNT) % ROAD_COUNT;
        ProjectionContext ctx;
        Point ground;
        float roadW;
        float lateral;
        float baseX;
        float baseY;
        float sw;
        float sh;

        t->segDist = segDist;
        t->visible = (segDist > 0 && segDist <= VIEW_DISTANCE);
        if (!t->visible)
        {
            continue;
        }

        ctx = *context;
        ctx.camZ = game->camZ - ((start + segDist >= ROAD_COUNT) ? TRACK_LENGTH : 0);

        ground = make_point(game->roads[t->segment].x,
                            game->roads[t->segment].y,
                            game->roads[t->segment].z);
        project_point(&ground, &ctx);

        roadW = ground.scale * game->roadWidth * WIN_WIDTH / 2.0f;
        lateral = (t->centerWorldX - ground.x) / game->roadWidth;
        baseX = ground.X + lateral * roadW;
        baseY = ground.Y;
        sw = (TREE_WORLD_W / game->roadWidth) * roadW;
        sh = ground.scale * TREE_WORLD_H * WIN_HEIGHT / 2.0f;

        t->p[0].X = baseX - sw / 2.0f; t->p[0].Y = baseY;      t->p[0].tz = ground.tz;
        t->p[1].X = baseX + sw / 2.0f; t->p[1].Y = baseY;      t->p[1].tz = ground.tz;
        t->p[2].X = baseX + sw / 2.0f; t->p[2].Y = baseY - sh; t->p[2].tz = ground.tz;
        t->p[3].X = baseX - sw / 2.0f; t->p[3].Y = baseY - sh; t->p[3].tz = ground.tz;
    }
}

/* 每帧把视野内的房子 8 个世界角投影到屏幕(无 bankAngle)。渲染端按 corner[8] 组三个可见面。
 * 角编号 c = (zr<<2)|(yr<<1)|xr:xr=0/1 → x 低/高,yr=0/1 → 底/顶,zr=0/1 → 近/远(z 低/高)。 */
static void project_houses(RacingGame *game, int start, const ProjectionContext *context)
{
    int i;
    for (i = 0; i < HOUSE_COUNT; i++)
    {
        House *h = &game->houses[i];
        int segDist = (h->segment - start + ROAD_COUNT) % ROAD_COUNT;
        ProjectionContext ctx;
        const Road *r;
        float cx;
        float cyb;
        float cz;
        float xs[2];
        float ys[2];
        float zs[2];
        int c;

        h->segDist = segDist;
        h->visible = (segDist > 0 && segDist <= VIEW_DISTANCE);
        if (!h->visible)
        {
            continue;
        }

        ctx = *context;
        ctx.camZ = game->camZ - ((start + segDist >= ROAD_COUNT) ? TRACK_LENGTH : 0);

        r = &game->roads[h->segment];
        cx = h->centerWorldX;
        cyb = r->y;
        cz = r->z;
        xs[0] = cx - HOUSE_WORLD_W / 2.0f;
        xs[1] = cx + HOUSE_WORLD_W / 2.0f;
        ys[0] = cyb;
        ys[1] = cyb + HOUSE_WORLD_H;
        zs[0] = cz - HOUSE_WORLD_D / 2.0f;
        zs[1] = cz + HOUSE_WORLD_D / 2.0f;

        for (c = 0; c < 8; c++)
        {
            Point p = make_point(xs[c & 1], ys[(c >> 1) & 1], zs[(c >> 2) & 1]);
            project_point(&p, &ctx);
            h->corner[c] = p;
        }
    }
}

void racing_game_init(RacingGame *game, unsigned int seed)
{
    if (game == NULL)
    {
        return;
    }

    srand(seed);
    game->mapIndex = 0;
    game->menuControlMode = 0;
    game->voiceState = 0;
    game->voiceText[0] = '\0';
    game->roadWidth = (float)ROAD_WIDTH * g_track_params[0].widthScale;
    game->collectibleRespawnMs = 0;
    build_track(game->roads, game->mapIndex);
    build_trees(game);
    build_houses(game);
    reset_runtime_state(game);
    reset_collectibles(game->collectibles, game->roads, game->collectiblePositions, game->roadWidth);
}

/* 切换地图：重建赛道表 + 按地图设路宽/收集物重生间隔 + 重置运行时状态。 */
void racing_game_set_map(RacingGame *game, int mapIndex)
{
    if (game == NULL)
    {
        return;
    }

    if (mapIndex < 0 || mapIndex >= RACING_MAP_COUNT)
    {
        mapIndex = 0;
    }

    game->mapIndex = mapIndex;
    /* 路宽按难度缩放：越难越窄（简单 1.3x / 中等 1.0x / 困难 0.7x）。 */
    game->roadWidth = (float)ROAD_WIDTH * g_track_params[mapIndex].widthScale;
    game->collectibleRespawnMs = 0;
    build_track(game->roads, mapIndex);
    build_trees(game);
    build_houses(game);
    reset_runtime_state(game);
    reset_collectibles(game->collectibles, game->roads, game->collectiblePositions, game->roadWidth);
}

const char *racing_game_map_name(int mapIndex)
{
    switch (mapIndex)
    {
    case 1:
        return "中等";
    case 2:
        return "困难";
    case 0:
    default:
        return "简单";
    }
}

/* 板端点阵字体只有 ASCII，中文显示不了，给嵌入式 UI 用 ASCII 回退名。 */
const char *racing_game_map_name_ascii(int mapIndex)
{
    switch (mapIndex)
    {
    case 1:
        return "Medium";
    case 2:
        return "Hard";
    case 0:
    default:
        return "Easy";
    }
}

void racing_game_reset_to_start(RacingGame *game)
{
    if (game == NULL)
    {
        return;
    }

    reset_runtime_state(game);
    reset_collectibles(game->collectibles, game->roads, game->collectiblePositions, game->roadWidth);
}

void racing_game_start(RacingGame *game)
{
    if (game == NULL)
    {
        return;
    }

    reset_runtime_state(game);
    game->mode = RACING_MODE_PLAYING;
    reset_collectibles(game->collectibles, game->roads, game->collectiblePositions, game->roadWidth);
}

void racing_game_update(RacingGame *game, const RacingInput *input, int deltaMs)
{
    RacingInput localInput;
    ProjectionContext context;
    int start;

    if (game == NULL)
    {
        return;
    }

    localInput = input != NULL ? *input : empty_input();

    if (localInput.restart)
    {
        racing_game_reset_to_start(game);
        racing_game_start(game);
        return;
    }

    /* 暂停 / 胜利界面：返回主菜单(停在开始页,不开新局)。 */
    if (localInput.toMenu &&
        (game->mode == RACING_MODE_PAUSED || game->mode == RACING_MODE_WIN))
    {
        racing_game_reset_to_start(game);
        return;
    }

    /* 1/2/3 任意时刻都可换图（切到不同的图才动作）。
     * 开始页切换→停在开始页；游戏中切换→直接从新图起点继续跑。 */
    if (localInput.map1 || localInput.map2 || localInput.map3)
    {
        int target = localInput.map1 ? 0 : (localInput.map2 ? 1 : 2);
        if (target != game->mapIndex)
        {
            RacingMode prevMode = game->mode;
            racing_game_set_map(game, target); /* 重建赛道，重置到起点 */
            if (prevMode == RACING_MODE_PLAYING || prevMode == RACING_MODE_PAUSED)
            {
                game->mode = RACING_MODE_PLAYING; /* 游戏中换图：直接继续跑 */
            }
            else if (prevMode == RACING_MODE_MAP_SELECT)
            {
                game->mode = RACING_MODE_MAP_SELECT; /* 选图界面里换图：留在选图界面 */
            }
            return;
        }
    }

    /* 开始界面 → 进入地图选择界面。 */
    if (localInput.mapSelect && game->mode == RACING_MODE_START)
    {
        game->mode = RACING_MODE_MAP_SELECT;
        return;
    }

    /* 开始界面 → 进入操作选择界面。 */
    if (localInput.controlSelect && game->mode == RACING_MODE_START)
    {
        game->mode = RACING_MODE_CONTROL_SELECT;
        return;
    }

    /* 开始界面 → 进入网络/小智语音启动界面。 */
    if (localInput.networkSelect && game->mode == RACING_MODE_START)
    {
        game->mode = RACING_MODE_NETWORK_SELECT;
        return;
    }

    /* 操作选择界面：左右切换操控方式(Original/Gyro/Test)，Enter 确认;选 Test 直接进测试屏,
     * 选 Original/Gyro 返回主菜单(设为发车用模式)。Esc 返回。 */
    if (game->mode == RACING_MODE_CONTROL_SELECT)
    {
        if (localInput.cyclePrev || localInput.cycleNext)
        {
            game->menuControlMode = localInput.cycleNext
                             ? (game->menuControlMode + 1) % 3
                             : (game->menuControlMode + 2) % 3;
            return;
        }
        if (localInput.start || localInput.back || localInput.pause)
        {
            game->mode = RACING_MODE_START;
            return;
        }
        return;
    }

    /* 地图选择界面：左右循环切图（全程预览），Enter/点击进游戏，Esc 返回。 */
    if (game->mode == RACING_MODE_MAP_SELECT)
    {
        if (localInput.cyclePrev || localInput.cycleNext)
        {
            int target = localInput.cycleNext
                             ? (game->mapIndex + 1) % RACING_MAP_COUNT
                             : (game->mapIndex + RACING_MAP_COUNT - 1) % RACING_MAP_COUNT;
            racing_game_set_map(game, target);
            game->mode = RACING_MODE_MAP_SELECT;
            return;
        }
        if (localInput.start)
        {
            racing_game_start(game);
            return;
        }
        if (localInput.back || localInput.pause)
        {
            game->mode = RACING_MODE_START;
            return;
        }
        return;
    }

    /* 网络页只负责提示和触发外部脚本;脚本执行在平台主循环里做。 */
    if (game->mode == RACING_MODE_NETWORK_SELECT)
    {
        if (localInput.back || localInput.pause)
        {
            game->mode = RACING_MODE_START;
        }
        return;
    }

    if (localInput.start && game->mode == RACING_MODE_START)
    {
        racing_game_start(game);
        return;
    }

    if (localInput.start && game->mode == RACING_MODE_PAUSED)
    {
        game->mode = RACING_MODE_PLAYING;
        return;
    }

    if (localInput.start && game->mode == RACING_MODE_WIN)
    {
        racing_game_reset_to_start(game);
        racing_game_start(game);
        return;
    }

    if (localInput.pause && game->mode == RACING_MODE_PLAYING)
    {
        game->mode = RACING_MODE_PAUSED;
    }
    else if (localInput.pause && game->mode == RACING_MODE_PAUSED)
    {
        game->mode = RACING_MODE_PLAYING;
    }

    if (game->mode != RACING_MODE_PLAYING)
    {
        return;
    }

    game->elapsedMs += deltaMs > 0 ? deltaMs : 0;
    update_movement(game, &localInput);
    update_collectibles(game, deltaMs);
    update_lap(game);
    refresh_camera_height(game);

    start = racing_game_start_segment(game);
    apply_air_wall(game, start);   /* 空气墙:钳 camX + 弹回 + 红边反馈(在投影前,用钳后位置) */
    game->isOut = game->camX >= game->roads[start].x + slow_grass_half_width(game) ||
                  game->camX <= game->roads[start].x - slow_grass_half_width(game);

    context = make_projection_context(game->camX, game->camY, game->camZ, game->angle);
    for (int i = 0; i < COLLECTIBLE_COUNT; i++)
    {
        project_nailong(&game->collectibles[i], &context);
    }
    project_trees(game, start, &context);
    project_houses(game, start, &context);

    if (game->hitFrames > 0)
    {
        game->hitFrames--;
    }
}

/* 当前摄像机落在哪个赛段上，用来驱动赛道和奶龙的分段渲染。 */
int racing_game_start_segment(const RacingGame *game)
{
    int segment;

    if (game == NULL)
    {
        return 0;
    }

    segment = game->camZ / SEG_LENGTH;
    if (segment < 0)
    {
        segment = 0;
    }

    return segment % ROAD_COUNT;
}
