#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include "collectible.h"
#include "background.h"
#include "config.h"
#include "draw.h"
#include "finish_flag.h"
#include "game.h"
#include "track.h"

#ifndef ASSET_DIR
#define ASSET_DIR "assets"
#endif

typedef enum GameMode
{
    GAME_START,
    GAME_PLAYING,
    GAME_PAUSED,
    GAME_WIN,
    GAME_EXIT
} GameMode;

typedef struct Assets
{
    sf::Texture cloudLayer;
    sf::Texture mountainFar;
    sf::Texture mountainNear;
    sf::Texture car;
    sf::Texture nailong;
    sf::Texture nailongHead;
    sf::SoundBuffer *soundBuffers[7];
    sf::Font font;
    bool audioEnabled;
} Assets;

typedef struct GameState
{
    GameMode mode;
    int camX;
    int camY;
    int camZ;
    int energy;
    int round;
    int hitTime;
    int finalScore;
    int scoreFreezeFrames;
    int flyTime;
    int bgmIndex;
    int switchCooldown;
    int timerSeconds;
    float angle;
    float distance;
    bool turnLeft;
    bool turnRight;
    bool isOut;
    bool isFlying;
} GameState;

typedef struct Button
{
    sf::RectangleShape box;
    sf::Text text;
} Button;

struct Game
{
    Assets assets;
    GameState state;
    std::vector<Road> roads;
    Nailong nailongs[3];
    int positions[3];
    sf::RenderWindow window;
    sf::Sprite car;
    sf::Sprite nailongHead;
    sf::Sound sound;
    sf::Sound bgm;
    sf::Text distanceText;
    sf::Text timerText;
    sf::Text startText;
    Button backToStartButton;
    Button exitButton;
    sf::Clock clock;
    bool initialized;
};

static bool audio_enabled_from_environment(void)
{
    const char *disableAudio = std::getenv("RACING_NO_AUDIO");

    if (disableAudio != NULL && std::string(disableAudio) == "1")
    {
        return false;
    }

    return true;
}

static void init_assets(Assets *assets)
{
    assets->audioEnabled = true;

    for (int i = 0; i < 7; i++)
    {
        assets->soundBuffers[i] = NULL;
    }
}

static void free_assets(Assets *assets)
{
    for (int i = 0; i < 7; i++)
    {
        delete assets->soundBuffers[i];
        assets->soundBuffers[i] = NULL;
    }
}

static bool load_sound_buffer(Assets *assets, int index, const std::string &path)
{
    assets->soundBuffers[index] = new sf::SoundBuffer;
    return assets->soundBuffers[index]->loadFromFile(path);
}

static bool load_assets(Assets *assets)
{
    bool ok = true;

    assets->audioEnabled = audio_enabled_from_environment();
    std::cerr << "Audio: " << (assets->audioEnabled ? "enabled" : "disabled") << "\n";

    ok = assets->cloudLayer.loadFromFile(std::string(ASSET_DIR) + "/images/cloud_layer.png") && ok;
    ok = assets->mountainFar.loadFromFile(std::string(ASSET_DIR) + "/images/mountain_far.png") && ok;
    ok = assets->mountainNear.loadFromFile(std::string(ASSET_DIR) + "/images/mountain_near.png") && ok;
    ok = assets->car.loadFromFile(std::string(ASSET_DIR) + "/images/car.png") && ok;
    ok = assets->nailong.loadFromFile(std::string(ASSET_DIR) + "/images/nailong.png") && ok;
    ok = assets->nailongHead.loadFromFile(std::string(ASSET_DIR) + "/images/nailong_head.png") && ok;
    ok = assets->font.loadFromFile(std::string(ASSET_DIR) + "/fonts/Arial.ttf") && ok;

    if (assets->audioEnabled)
    {
        ok = load_sound_buffer(assets, 0, std::string(ASSET_DIR) + "/audio/qidong.mp3") && ok;
        ok = load_sound_buffer(assets, 1, std::string(ASSET_DIR) + "/audio/chongci.mp3") && ok;
        ok = load_sound_buffer(assets, 2, std::string(ASSET_DIR) + "/audio/haxue.mp3") && ok;
        ok = load_sound_buffer(assets, 3, std::string(ASSET_DIR) + "/audio/mengzhong.mp3") && ok;
        ok = load_sound_buffer(assets, 4, std::string(ASSET_DIR) + "/audio/dashi.mp3") && ok;
        ok = load_sound_buffer(assets, 5, std::string(ASSET_DIR) + "/audio/leyihaqi.mp3") && ok;
        ok = load_sound_buffer(assets, 6, std::string(ASSET_DIR) + "/audio/jiangnan.mp3") && ok;
    }
    else
    {
        std::cerr << "Audio disabled by RACING_NO_AUDIO=1. Running without sound.\n";
    }

    return ok;
}

static GameState make_game_state(void)
{
    GameState state;
    state.mode = GAME_START;
    state.camX = 0;
    state.camY = 2000;
    state.camZ = 0;
    state.energy = 700;
    state.round = 0;
    state.hitTime = 0;
    state.finalScore = 0;
    state.scoreFreezeFrames = 0;
    state.flyTime = 0;
    state.bgmIndex = 0;
    state.switchCooldown = 0;
    state.timerSeconds = 0;
    state.angle = 0.0f;
    state.distance = 0.0f;
    state.turnLeft = false;
    state.turnRight = false;
    state.isOut = false;
    state.isFlying = false;
    return state;
}

static sf::Text make_text(const sf::Font *font, unsigned int size, sf::Color color, float x, float y)
{
    sf::Text text;
    text.setFont(*font);
    text.setCharacterSize(size);
    text.setFillColor(color);
    text.setPosition(x, y);
    return text;
}

static Button make_button(const sf::Font *font, const std::string &label, float x, float y)
{
    Button button;
    button.box.setSize(sf::Vector2f(300.0f, 70.0f));
    button.box.setPosition(x, y);
    button.box.setFillColor(sf::Color(245, 238, 210, 230));
    button.box.setOutlineColor(sf::Color(70, 57, 42));
    button.box.setOutlineThickness(4.0f);

    button.text = make_text(font, 32, sf::Color(45, 38, 30), x + 30.0f, y + 16.0f);
    button.text.setString(label);
    return button;
}

static bool button_contains(const Button *button, sf::Vector2f point)
{
    return button->box.getGlobalBounds().contains(point);
}

static void draw_button(sf::RenderWindow *window, const Button *button)
{
    window->draw(button->box);
    window->draw(button->text);
}

static void draw_panel(sf::RenderWindow *window, const sf::Font *font, const std::string &title, const std::string &subtitle, const Button *startButton, const Button *exitButton)
{
    sf::RectangleShape shade(sf::Vector2f((float)WIN_WIDTH, (float)WIN_HEIGHT));
    sf::RectangleShape panel(sf::Vector2f(560.0f, 360.0f));
    sf::Text titleText = make_text(font, 54, sf::Color(255, 234, 185), 0.0f, 190.0f);
    sf::Text subtitleText = make_text(font, 28, sf::Color::White, 0.0f, 270.0f);

    shade.setFillColor(sf::Color(0, 0, 0, 130));
    panel.setFillColor(sf::Color(42, 55, 58, 230));
    panel.setOutlineColor(sf::Color(255, 234, 185));
    panel.setOutlineThickness(5.0f);
    panel.setPosition(232.0f, 150.0f);

    titleText.setString(title);
    titleText.setPosition((WIN_WIDTH - titleText.getLocalBounds().width) / 2.0f, 190.0f);

    subtitleText.setString(subtitle);
    subtitleText.setPosition((WIN_WIDTH - subtitleText.getLocalBounds().width) / 2.0f, 270.0f);

    window->draw(shade);
    window->draw(panel);
    window->draw(titleText);
    window->draw(subtitleText);
    draw_button(window, startButton);
    draw_button(window, exitButton);
}

static void reset_game(GameState *state, Nailong nailongs[3], const std::vector<Road> *roads, int positions[3], sf::Clock *clock, sf::Text *timerText)
{
    int bgmIndex = state->bgmIndex;
    int switchCooldown = state->switchCooldown;

    *state = make_game_state();
    state->mode = GAME_PLAYING;
    state->bgmIndex = bgmIndex;
    state->switchCooldown = switchCooldown;
    reset_collectibles(nailongs, roads, positions);
    clock->restart();
    timerText->setString("0");
}

static void handle_music_switch(GameState *state, sf::Sound *bgm, const Assets *assets)
{
    if (!assets->audioEnabled)
    {
        return;
    }

    if (sf::Keyboard::isKeyPressed(sf::Keyboard::LAlt) && state->switchCooldown <= 0)
    {
        state->switchCooldown = 20;
        state->bgmIndex++;
        bgm->setBuffer(*assets->soundBuffers[2 + (state->bgmIndex % 5)]);
        bgm->play();
    }

    if (state->switchCooldown > -10)
    {
        state->switchCooldown--;
    }
}

static void update_timer(GameState *state, sf::Clock *clock, sf::Text *timerText)
{
    sf::Time elapsed = clock->getElapsedTime();

    if (elapsed.asSeconds() >= 1.0f)
    {
        state->timerSeconds++;
        clock->restart();
    }

    timerText->setString(std::to_string(state->timerSeconds));
}

static void play_sound(sf::Sound *sound, const Assets *assets, int index)
{
    if (!assets->audioEnabled)
    {
        return;
    }

    if (assets->soundBuffers[index] == NULL)
    {
        return;
    }

    sound->setBuffer(*assets->soundBuffers[index]);
    sound->play();
}

static void update_input(GameState *state, sf::Sound *sound, const Assets *assets)
{
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::A))
    {
        state->angle += 0.009f;
        state->turnLeft = true;
    }
    else
    {
        state->turnLeft = false;
    }

    if (sf::Keyboard::isKeyPressed(sf::Keyboard::D))
    {
        state->angle -= 0.009f;
        state->turnRight = true;
    }
    else
    {
        state->turnRight = false;
    }

    if (!state->isFlying)
    {
        if (!state->isOut)
        {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::W))
            {
                state->camZ += (int)(3 * SEG_LENGTH * std::cos(-state->angle));
                state->camX += (int)(3 * SEG_LENGTH * std::sin(-state->angle));
                state->distance += 0.6f;
            }

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Space) &&
                sf::Keyboard::isKeyPressed(sf::Keyboard::W) &&
                state->energy > 0)
            {
                state->camZ += (int)(2 * SEG_LENGTH * std::cos(-state->angle));
                state->camX += (int)(2 * SEG_LENGTH * std::sin(-state->angle));
                state->distance += 0.4f;
                state->energy -= 2;
            }
        }
        else if (sf::Keyboard::isKeyPressed(sf::Keyboard::W))
        {
            state->camZ += (int)(SEG_LENGTH * std::cos(-state->angle));
            state->camX += (int)(SEG_LENGTH * std::sin(-state->angle));
            state->distance += 0.2f;

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Space))
            {
                state->energy -= 1;
            }
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::S))
        {
            state->camZ -= (int)(SEG_LENGTH * std::cos(state->angle));
            state->camX -= (int)(SEG_LENGTH * std::sin(-state->angle));
            state->distance -= 0.2f;
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Space) &&
            sf::Keyboard::isKeyPressed(sf::Keyboard::S))
        {
            state->camZ -= SEG_LENGTH;
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::F) && state->energy >= 1000)
        {
            state->energy -= 1000;
            state->isFlying = true;
            play_sound(sound, assets, 1);
        }
    }

    if (state->isFlying)
    {
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::W))
        {
            state->camZ += (int)(8 * SEG_LENGTH * std::cos(-state->angle));
            state->camX += (int)(8 * SEG_LENGTH * std::sin(-state->angle));
            state->distance += 1.6f;
        }

        state->flyTime++;
        if (state->flyTime > 300)
        {
            state->isFlying = false;
            state->flyTime = 0;
        }
    }

    if (state->angle > 1.0f)
    {
        state->angle = 1.0f;
    }

    if (state->angle < -1.0f)
    {
        state->angle = -1.0f;
    }
}

static void update_collectibles(GameState *state, Nailong nailongs[3])
{
    for (int i = 0; i < 3; i++)
    {
        if (collect_nailong(&nailongs[i], state->camX, state->camZ))
        {
            state->energy += 100;
            state->hitTime = 30;
        }
    }
}

static void update_lap(GameState *state, Nailong nailongs[3], const std::vector<Road> *roads, int positions[3])
{
    int totalLength = ROAD_COUNT * SEG_LENGTH;

    if (state->camZ >= totalLength)
    {
        state->camZ -= totalLength;
        state->round++;
        reset_collectibles(nailongs, roads, positions);

        if (state->round >= 3)
        {
            state->finalScore = state->timerSeconds;
            state->mode = GAME_WIN;
        }
    }

    if (state->camZ < 0)
    {
        state->camZ += totalLength;
    }
}

static void update_camera_height(GameState *state, const std::vector<Road> *roads, int startPos)
{
    if (state->isFlying)
    {
        if (state->camY < 6000)
        {
            state->camY += 80;
        }
        return;
    }

    if (state->camY >= 2000 + (int)(*roads)[startPos].y)
    {
        state->camY -= 100;
    }

    if (state->camY < 2000 + (int)(*roads)[startPos].y)
    {
        state->camY = 2000 + (int)(*roads)[startPos].y;
    }
}

static void draw_track(sf::RenderWindow *window, GameState *state, std::vector<Road> *roads, Nailong nailongs[3], const int positions[3], sf::Texture *nailongTexture)
{
    int totalLength = ROAD_COUNT * SEG_LENGTH;
    int startPos = state->camZ / SEG_LENGTH;
    int finishSegment = ROAD_COUNT - 1;

    for (int i = startPos + 300; i > startPos; i--)
    {
        Road *prev = &(*roads)[i % ROAD_COUNT];
        Road *now = &(*roads)[(i - 1) % ROAD_COUNT];
        float bankAngle = 0.0f;
        sf::Color grass = i % 2 ? sf::Color(12, 210, 16) : sf::Color(0, 199, 0);
        sf::Color edge = i % 2 ? sf::Color::Black : sf::Color::White;
        sf::Color road = i % 2 ? sf::Color(105, 105, 105) : sf::Color(101, 101, 101);

        project_road(prev, state->camX, state->camY, state->camZ - (i >= ROAD_COUNT ? totalLength : 0), state->angle);
        project_road(now, state->camX, state->camY, state->camZ - ((i - 1) >= ROAD_COUNT ? totalLength : 0), state->angle);

        if (state->turnLeft)
        {
            bankAngle = -0.1f;
        }
        else if (state->turnRight)
        {
            bankAngle = 0.1f;
        }

        draw_trapezoid(window, grass, (int)prev->X, (int)prev->Y, WIN_WIDTH * 10, (int)now->X, (int)now->Y, WIN_WIDTH * 10, bankAngle);
        draw_trapezoid(window, edge, (int)prev->X, (int)prev->Y, (int)(prev->W * 1.3f), (int)now->X, (int)now->Y, (int)(now->W * 1.3f), bankAngle);
        draw_trapezoid(window, road, (int)prev->X, (int)prev->Y, (int)prev->W, (int)now->X, (int)now->Y, (int)now->W, bankAngle);

        if (i % ROAD_COUNT == finishSegment)
        {
            draw_finish_flag(window, prev, state->camX, state->camY, state->camZ, state->angle);
        }

        for (int j = 0; j < 3; j++)
        {
            if (i == positions[j] && !nailongs[j].eaten)
            {
                draw_nailong(window, nailongTexture, &nailongs[j]);
            }
        }
    }
}

static void go_to_start(Game *game)
{
    game->state = make_game_state();
    reset_collectibles(game->nailongs, &game->roads, game->positions);
    game->clock.restart();
    game->timerText.setString("Press Enter to start");
}

static void start_playing(Game *game)
{
    play_sound(&game->sound, &game->assets, 0);
    reset_game(&game->state, game->nailongs, &game->roads, game->positions, &game->clock, &game->timerText);
}

static void process_state_events(Game *game)
{
    sf::Event event;

    while (game->window.pollEvent(event))
    {
        if (event.type == sf::Event::Closed)
        {
            game->state.mode = GAME_EXIT;
        }

        if (event.type == sf::Event::KeyPressed)
        {
            if (game->state.mode == GAME_START && event.key.code == sf::Keyboard::Enter)
            {
                start_playing(game);
            }
            else if (game->state.mode == GAME_PLAYING && event.key.code == sf::Keyboard::Escape)
            {
                game->state.mode = GAME_PAUSED;
            }
        }

        if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left)
        {
            sf::Vector2f mouse((float)event.mouseButton.x, (float)event.mouseButton.y);

            if (game->state.mode == GAME_PAUSED || game->state.mode == GAME_WIN)
            {
                if (button_contains(&game->backToStartButton, mouse))
                {
                    go_to_start(game);
                }
                else if (button_contains(&game->exitButton, mouse))
                {
                    game->state.mode = GAME_EXIT;
                }
            }
        }
    }
}

static void update_playing_state(Game *game)
{
    if (game->state.mode != GAME_PLAYING)
    {
        return;
    }

    update_timer(&game->state, &game->clock, &game->timerText);
    update_input(&game->state, &game->sound, &game->assets);
    update_collectibles(&game->state, game->nailongs);
    update_lap(&game->state, game->nailongs, &game->roads, game->positions);
}

static void update_scene_projection(Game *game)
{
    int startPos = game->state.camZ / SEG_LENGTH;

    update_camera_height(&game->state, &game->roads, startPos);
    game->state.isOut = game->state.camX >= game->roads[startPos].x + ROAD_WIDTH / 1.5f ||
                        game->state.camX <= game->roads[startPos].x - ROAD_WIDTH / 1.5f;

    for (int i = 0; i < 3; i++)
    {
        project_nailong(&game->nailongs[i], game->state.camX, game->state.camY, game->state.camZ, game->state.angle);
    }
}

static void render_game(Game *game)
{
    sf::Text roundText = make_text(&game->assets.font, 36, sf::Color::Red, 0.0f, 100.0f);

    game->window.clear();
    draw_sky(&game->window);
    draw_parallax_layer(&game->window, &game->assets.cloudLayer, game->state.camX, game->state.camY, 0.0f, 0.002f, 0.0008f, 180.0f, 0.8f);
    draw_parallax_layer(&game->window, &game->assets.mountainFar, game->state.camX, game->state.camY, 20.0f, 0.005f, 0.0015f, 0.0f, 0.8f);
    draw_parallax_layer(&game->window, &game->assets.mountainNear, game->state.camX, game->state.camY, 30.0f, 0.010f, 0.0017f, 100.0f, 0.8f);
    draw_track(&game->window, &game->state, &game->roads, game->nailongs, game->positions, &game->assets.nailong);

    if (game->state.hitTime > 0)
    {
        game->window.draw(game->nailongHead);
    }

    game->window.draw(game->car);

    game->distanceText.setString(std::to_string(game->state.distance));
    game->window.draw(game->distanceText);
    draw_energy(&game->window, (game->state.energy + 99) / 100);
    game->window.draw(game->timerText);

    roundText.setString(std::to_string(game->state.round) + "/3");
    game->window.draw(roundText);

    if (game->state.mode == GAME_START)
    {
        game->window.draw(game->startText);
    }
    else if (game->state.mode == GAME_PAUSED)
    {
        draw_panel(&game->window, &game->assets.font, "Paused", "Choose what to do next", &game->backToStartButton, &game->exitButton);
    }
    else if (game->state.mode == GAME_WIN)
    {
        draw_panel(&game->window, &game->assets.font, "Win", "Time: " + std::to_string(game->state.finalScore) + "s", &game->backToStartButton, &game->exitButton);
    }

    if (game->state.mode == GAME_PLAYING)
    {
        game->state.hitTime--;
    }

    game->window.display();
}

Game *game_create(void)
{
    Game *game = new Game;
    game->positions[0] = 0;
    game->positions[1] = 0;
    game->positions[2] = 0;
    game->initialized = false;
    init_assets(&game->assets);
    game->state = make_game_state();
    return game;
}

bool game_init(Game *game)
{
    if (game == NULL)
    {
        return false;
    }

    std::srand((unsigned int)std::time(NULL));
    build_track(&game->roads);
    reset_collectibles(game->nailongs, &game->roads, game->positions);

    game->window.create(sf::VideoMode(WIN_WIDTH, WIN_HEIGHT), "Racing");
    game->window.setFramerateLimit(60);

    if (!load_assets(&game->assets))
    {
        free_assets(&game->assets);
        return false;
    }

    game->car.setTexture(game->assets.car);
    game->car.setTextureRect(sf::IntRect(0, 0, WIN_WIDTH, WIN_HEIGHT));
    game->nailongHead.setTexture(game->assets.nailongHead);
    game->nailongHead.setTextureRect(sf::IntRect(0, 0, WIN_WIDTH, WIN_HEIGHT));

    if (game->assets.audioEnabled)
    {
        game->bgm.setBuffer(*game->assets.soundBuffers[2]);
        game->bgm.setLoop(true);
        game->bgm.play();
    }

    game->distanceText = make_text(&game->assets.font, 36, sf::Color::Red, 660.0f, 600.0f);
    game->timerText = make_text(&game->assets.font, 48, sf::Color::Cyan, 0.0f, 0.0f);
    game->startText = make_text(&game->assets.font, 34, sf::Color::Black, 230.0f, 150.0f);
    game->startText.setLineSpacing(1.25f);
    game->startText.setString("Press Enter to start\nWS: move\nAD: turn\nF: fly (cost 1000 energy)\nSpace: accelerate\nEsc: pause\nAlt: shift bgm");
    game->timerText.setString("Press Enter to start");
    game->backToStartButton = make_button(&game->assets.font, "Back to Start", 362.0f, 330.0f);
    game->exitButton = make_button(&game->assets.font, "Exit", 362.0f, 420.0f);
    game->initialized = true;
    return true;
}

bool game_is_running(const Game *game)
{
    return game != NULL && game->initialized && game->window.isOpen();
}

void game_step(Game *game)
{
    if (game == NULL || !game->initialized)
    {
        return;
    }

    process_state_events(game);

    if (game->state.mode == GAME_EXIT)
    {
        game->window.close();
        return;
    }

    handle_music_switch(&game->state, &game->bgm, &game->assets);
    update_playing_state(game);
    update_scene_projection(game);
    render_game(game);
}

void game_destroy(Game *game)
{
    if (game == NULL)
    {
        return;
    }

    free_assets(&game->assets);
    delete game;
}
