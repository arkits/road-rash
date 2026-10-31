# Run the native port on macOS

Checked on October 1, 2026: the playable executable exists at `build/rr-game`, and the original assets exist at `../cdrom/ROADRASH`. Commands and course IDs were checked against CMake, `src/game_main.cpp`, and `run-macos.sh`. This documentation check did not launch the game.

From Terminal:

```sh
cd /Users/archit/Dev/rr/native-port
./build/rr-game --assets ../cdrom/ROADRASH
```

To build or rebuild first (requires CMake, SDL3, and a C++17 compiler):

```sh
brew install cmake sdl3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/rr-game --assets ../cdrom/ROADRASH
```

Arrow keys or WASD accelerate, brake, and steer. Z punches, X kicks, C backhands. Esc quits. Enter restarts after the finish.

Select a course with `--course`: 0 Napa, 1 City, 2 Highway, 3 Canyon, 4 Medley. Select a bike with `--bike 0` through `--bike 14`.

`./run-macos.sh` (and `./run-linux.sh`, `run-windows.bat` / `run-windows.ps1` on Linux and Windows) builds and launches the playable game (`rr-game`), passing `--assets` when the disc is mounted and falling back to the first-run import otherwise. The asset research viewer is a separate developer tool, launched with `./run-viewer.sh` (`rr-viewer`).

Launch progress: started `./build/rr-game --assets ../cdrom/ROADRASH` from the native-port directory at the user's request. The process remained running after the initial one-second startup check, with no error output.
