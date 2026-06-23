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
static void build_track(Road roads[ROAD_COUNT])
{
    float x = 0.0f;

    for (int i = 0; i < ROAD_COUNT; i++)
    {
        float curve;
        int y = 0;

        if (i <= 123 || (456 < i && i <= 789) || (1234 < i && i <= 1721))
        {
            curve = 0.5f;
        }
        else
        {
            curve = -0.5f;
        }

        x += curve;

        if (i > 300 && i < 1240)
        {
            y = (int)(1600.0f * sinf(i / 30.0f - 10.0f));
        }

        roads[i] = make_road(x * 45.0f, (float)y, (float)((1 + i) * SEG_LENGTH));
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
static void reset_nailong(Nailong *nailong, const Road *road)
{
    float x = road->x - ROAD_WIDTH / 2.0f + (float)(rand() % 1500);
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
static void reset_collectibles(Nailong nailongs[COLLECTIBLE_COUNT], const Road roads[ROAD_COUNT], int positions[COLLECTIBLE_COUNT])
{
    positions[0] = rand() % 500 + 200;
    positions[1] = rand() % 500 + 700;
    positions[2] = rand() % 500 + 1200;

    for (int i = 0; i < COLLECTIBLE_COUNT; i++)
    {
        reset_nailong(&nailongs[i], &roads[positions[i]]);
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

/* 奶龙被吃到后补能量，并触发短暂跳脸反馈。 */
static void update_collectibles(RacingGame *game)
{
    for (int i = 0; i < COLLECTIBLE_COUNT; i++)
    {
        if (collect_nailong(&game->collectibles[i], game->camX, game->camZ))
        {
            game->energy += 100;
            game->hitFrames = 15;
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
        reset_collectibles(game->collectibles, game->roads, game->collectiblePositions);

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

    if (game->angle > 1.0f)
    {
        game->angle = 1.0f;
    }

    if (game->angle < -1.0f)
    {
        game->angle = -1.0f;
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
    build_track(game->roads);
    reset_runtime_state(game);
    reset_collectibles(game->collectibles, game->roads, game->collectiblePositions);
}

void racing_game_reset_to_start(RacingGame *game)
{
    if (game == NULL)
    {
        return;
    }

    reset_runtime_state(game);
    reset_collectibles(game->collectibles, game->roads, game->collectiblePositions);
}

void racing_game_start(RacingGame *game)
{
    if (game == NULL)
    {
        return;
    }

    reset_runtime_state(game);
    game->mode = RACING_MODE_PLAYING;
    reset_collectibles(game->collectibles, game->roads, game->collectiblePositions);
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
    update_collectibles(game);
    update_lap(game);
    refresh_camera_height(game);

    start = racing_game_start_segment(game);
    game->isOut = game->camX >= game->roads[start].x + ROAD_WIDTH / 1.5f ||
                  game->camX <= game->roads[start].x - ROAD_WIDTH / 1.5f;

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
