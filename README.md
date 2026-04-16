# Racing

A small SFML racing game.

## Project Layout

- `src/`: C/C++ source files.
- `include/`: project headers.
- `assets/images/`: image resources.
- `assets/audio/`: music and sound effects.
- `assets/fonts/`: font files.

The background uses two transparent mountain layers:

- `assets/images/mountain_far.png`: slow far-background parallax.
- `assets/images/mountain_near.png`: faster near-background parallax.

## Build

```sh
cmake -S . -B build
cmake --build build
```

Run the game from the project root or directly from the generated executable.

## Game States

- Start: press `Enter` to begin.
- Playing: press `Esc` to pause.
- Paused: click `Back to Start` or `Exit`.
- Win: reached after 3 laps; click `Back to Start` or `Exit`.
- Exit: closes the game window.

## Audio

Run normally if you want sound:

```sh
./build/main
```

Only use this fallback when the machine has no working audio device:

```sh
RACING_NO_AUDIO=1 ./build/main
```

If you used the fallback before and want sound again, clear the variable:

```sh
unset RACING_NO_AUDIO
./build/main
```
