#include "game.h"

#include <math.h>
#include <stdlib.h>

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

/* 地图0 环形：整条赛道走 2 圈连续正弦，平滑大弯且首尾相接成闭环。 */
static void build_track_ring(Road roads[ROAD_COUNT])
{
    const float two_pi = 6.2831853f;
    const float amp = 0.6f;
    const int loops = 2;
    float x = 0.0f;

    for (int i = 0; i < ROAD_COUNT; i++)
    {
        x += amp * sinf(two_pi * loops * (float)i / (float)ROAD_COUNT);
        roads[i] = make_road(x * 45.0f, 0.0f, (float)((1 + i) * SEG_LENGTH));
    }
}

/* 地图1 宽直道：curve 恒为 0，纯平直路；路面宽度在 set_map 里调宽。 */
static void build_track_straight(Road roads[ROAD_COUNT])
{
    for (int i = 0; i < ROAD_COUNT; i++)
    {
        roads[i] = make_road(0.0f, 0.0f, (float)((1 + i) * SEG_LENGTH));
    }
}

/* 地图2 Z/S 交叉10次：20 段交替。每段用一个完整正弦周期驱动 curve
 * （恒定 curve 会让 x、z 同比线性增长，投影成直线看不出弯；正弦 curve 使
 * x 超线性增长，路面才显出弯）。Z 段振幅大=急弯，S 段振幅小=缓弯；完整
 * 周期净偏移为零，赛道不会整体漂走。 */
static void build_track_zigzag(Road roads[ROAD_COUNT])
{
    const int sections = 20;          /* 10 段 Z + 10 段 S */
    const int secLen = ROAD_COUNT / sections;
    const float two_pi = 6.2831853f;
    float x = 0.0f;

    for (int i = 0; i < ROAD_COUNT; i++)
    {
        int section = i / secLen;
        float t = (float)(i % secLen) / (float)secLen;   /* 段内 0..1 */
        float curve;

        if (section % 2 == 0)
        {
            /* Z 形：大振幅完整正弦周期，先急转再回拐。 */
            curve = 2.5f * sinf(two_pi * t);
        }
        else
        {
            /* S 形：小振幅完整正弦周期，平滑缓弯。 */
            curve = 1.0f * sinf(two_pi * t);
        }

        x += curve;
        roads[i] = make_road(x * 45.0f, 0.0f, (float)((1 + i) * SEG_LENGTH));
    }
}

static void build_track(Road roads[ROAD_COUNT], int mapIndex)
{
    switch (mapIndex)
    {
    case 1:
        build_track_straight(roads);
        break;
    case 2:
        build_track_zigzag(roads);
        break;
    case 0:
    default:
        build_track_ring(roads);
        break;
    }
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
 * 地图“一路邮你”collectibleRespawnMs 较小：校徽被吃后很快在前方重生，
 * 刷新频率更高。其它地图 respawnMs=0，不重生（维持一局 N 个）。 */
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

void racing_game_init(RacingGame *game, unsigned int seed)
{
    if (game == NULL)
    {
        return;
    }

    srand(seed);
    game->mapIndex = 0;
    game->roadWidth = (float)ROAD_WIDTH;
    game->collectibleRespawnMs = 0;
    build_track(game->roads, game->mapIndex);
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
    /* 地图1（一路邮你）路面更宽 + 校徽被吃后 1.2s 在前方重生（刷新更快）；其余默认。 */
    game->roadWidth = (mapIndex == 1) ? (float)ROAD_WIDTH * 1.8f : (float)ROAD_WIDTH;
    game->collectibleRespawnMs = (mapIndex == 1) ? 1200 : 0;
    build_track(game->roads, mapIndex);
    reset_runtime_state(game);
    reset_collectibles(game->collectibles, game->roads, game->collectiblePositions, game->roadWidth);
}

const char *racing_game_map_name(int mapIndex)
{
    switch (mapIndex)
    {
    case 1:
        return "一路邮你";
    case 2:
        return "Z/S x10";
    case 0:
    default:
        return "Ring";
    }
}

/* 板端点阵字体只有 ASCII，中文显示不了，给嵌入式 UI 用 ASCII 回退名。 */
const char *racing_game_map_name_ascii(int mapIndex)
{
    switch (mapIndex)
    {
    case 1:
        return "BUPT";
    case 2:
        return "Z/S x10";
    case 0:
    default:
        return "Ring";
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
    game->isOut = game->camX >= game->roads[start].x + game->roadWidth / 1.5f ||
                  game->camX <= game->roads[start].x - game->roadWidth / 1.5f;

    context = make_projection_context(game->camX, game->camY, game->camZ, game->angle);
    for (int i = 0; i < COLLECTIBLE_COUNT; i++)
    {
        project_nailong(&game->collectibles[i], &context);
    }

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
