# Racing

A small pure-C racing game core with an optional LVGL view for embedded
targets.

## Dependencies

The gameplay core is plain C and can build by itself. Desktop preview and the
LVGL adapter are optional and only build when their libraries are available.

On Debian or Ubuntu, install the desktop preview dependencies with:

```sh
sudo apt-get update
sudo apt-get install -y libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

If `cmake` says SDL2 is missing, the core is still fine. You can build the
library and smoke test without the desktop renderer:

```sh
cmake -S . -B build-c
cmake --build build-c
```

If CMake mentions a missing toolchain file such as a NuttX path, you are
probably reusing an old build directory from another project. Start from a
fresh directory, or delete the stale cache first:

```sh
unset CMAKE_TOOLCHAIN_FILE
rm -rf build build-c build-fast
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

## Project Layout

- `include/config.h`: screen size, view distance, timing, and tuning macros.
- `include/game.h`: public game state and input API.
- `include/render_lvgl.h`: optional LVGL canvas view API.
- `src/game.c`: gameplay, projection, collectibles, and track generation.
- `src/render_lvgl.c`: LVGL canvas renderer for Linux or embedded targets.
- `src/render_sdl.c`: SDL renderer for desktop preview and asset checks.
- `src/desktop_main.c`: SDL window, timing, and keyboard loop.
- `src/main.c`: small smoke-test executable.

## Build The Core

```sh
cmake -S . -B build-c
cmake --build build-c
```

The default build produces `libracing_core.a` and a tiny smoke executable. The
core has no desktop window dependency.

## Play On Desktop

After SDL2, SDL2_image, and SDL2_ttf are installed, build and run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/racing_desktop
```

Controls:

- `W`/`Up`: accelerate
- `S`/`Down`: brake
- `A`/`Left`, `D`/`Right`: steer
- `Space`: boost
- `F`: fly when energy reaches 1000
- `Enter`: start or resume
- `R`: restart
- `Esc` or `P`: pause/resume

## LVGL

LVGL can run on Linux through its SDL, framebuffer, or DRM/KMS backends. If
`pkg-config` can find `lvgl`, CMake builds the separate `racing_lvgl` adapter
declared in `include/render_lvgl.h`:

```c
#include "game.h"
#include "render_lvgl.h"

RacingGame game;
racing_game_init(&game, seed);

RacingLvglView *view = racing_lvgl_create(lv_scr_act(), &game);
racing_lvgl_set_input(view, input);
```

On embedded hardware, initialize LVGL, your display driver, and your input
driver in the board project, then create the `RacingLvglView` on the screen or
parent object you want to use.

For tighter RAM control, provide the canvas buffer yourself:

```c
static lv_color_t canvas_buf[WIN_WIDTH * WIN_HEIGHT];
RacingLvglView *view = racing_lvgl_create_with_buffer(
    lv_scr_act(), &game, canvas_buf, WIN_WIDTH * WIN_HEIGHT);
```

## Performance Tuning

The embedded build can override these at compile time:

```sh
-DWIN_WIDTH=480 -DWIN_HEIGHT=320 -DVIEW_DISTANCE=140 -DRACING_LVGL_FRAME_MS=33
```

Lower `VIEW_DISTANCE` reduces road polygons per frame. Higher
`RACING_LVGL_FRAME_MS` lowers refresh rate and CPU use. Smaller screen
dimensions reduce canvas memory and fill cost.

## Game States

- Start: press `Enter` to begin.
- Playing: steer, accelerate, and collect奶龙道具.
- Paused: press `Enter` to resume, or `R` to restart.
- Win: reached after 3 laps; press `Enter` or `R` to restart.
- Exit: close the SDL window.

## Notes

The old SFML renderer was removed so the gameplay code stays portable C. Audio
is intentionally not wired into the core; add it in the target-specific
platform layer if the embedded board has audio output.
