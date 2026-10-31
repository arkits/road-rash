// Plan 33b Task 3: per-OS process setup for rr-game (Windows and desktop
// Linux; a no-op elsewhere).
#pragma once

namespace rr {
// Windows: `--console` attaches stdout/stderr to the parent console (rr-game
// is a GUI-subsystem program), and SDL log output is also written to
// rr-game.log in the local data directory (the previous run kept as
// rr-game.1.log). Desktop Linux (plan 33a) writes the same log file in the
// SDL pref path. Call first in main, before SDL_Init.
void platform_game_startup(int argc, char** argv);
} // namespace rr
