// Plan 33b Task 3: per-OS process setup for rr-game (no-op off Windows).
#pragma once

namespace rr {
// Windows: `--console` attaches stdout/stderr to the parent console (rr-game
// is a GUI-subsystem program), and SDL log output is also written to
// rr-game.log in the local data directory (the previous run kept as
// rr-game.1.log). Call first in main, before SDL_Init.
void platform_game_startup(int argc, char** argv);
} // namespace rr
