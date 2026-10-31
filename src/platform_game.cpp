// Plan 33b Task 3: per-OS process setup for rr-game.
#include "platform_game.hpp"
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
#define RR_GAME_LOG_FILE 1
#include "platform_files.hpp"
#include "utf8_path.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#endif
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rr {
#ifdef RR_GAME_LOG_FILE
namespace {
std::FILE* log_file = nullptr;
SDL_LogOutputFunction default_output = nullptr;
void* default_userdata = nullptr;
const char* priority_name(SDL_LogPriority priority) {
    switch (priority) {
    case SDL_LOG_PRIORITY_ERROR:
    case SDL_LOG_PRIORITY_CRITICAL:
        return "ERROR";
    case SDL_LOG_PRIORITY_WARN:
        return "WARN";
    default:
        return "INFO";
    }
}
void SDLCALL log_output(void*, int category, SDL_LogPriority priority, const char* message) {
    // SDL's default output already reaches OutputDebugString and an attached
    // console on Windows, and stderr on Linux.
    if (default_output) default_output(default_userdata, category, priority, message);
    if (log_file) {
        std::fprintf(log_file, "[%10llu ms] %s %s\n", static_cast<unsigned long long>(SDL_GetTicks()),
                     priority_name(priority), message);
        std::fflush(log_file);
    }
}
} // namespace

void platform_game_startup([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
#if defined(_WIN32)
    // A GUI-subsystem process has no console. `--console` attaches to the
    // parent's, but only for streams that are not already redirected to a
    // pipe or file (for example `rr-game --console --version > out.txt`).
    const auto redirected = [](DWORD which) {
        const HANDLE h = ::GetStdHandle(which);
        return h && h != INVALID_HANDLE_VALUE && ::GetFileType(h) != FILE_TYPE_UNKNOWN;
    };
    // Checked before attaching, which may install console handles.
    const bool out_redirected = redirected(STD_OUTPUT_HANDLE), err_redirected = redirected(STD_ERROR_HANDLE);
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--console") == 0 && !(out_redirected && err_redirected) &&
            ::AttachConsole(ATTACH_PARENT_PROCESS)) {
            std::FILE* stream = nullptr;
            if (!out_redirected) freopen_s(&stream, "CONOUT$", "w", stdout);
            if (!err_redirected) freopen_s(&stream, "CONOUT$", "w", stderr);
        }
#endif
    // Plan 33a: Linux too. A game started from the application menu, an
    // AppImage or Steam has no terminal, so problem reports need the file.
    try {
        const std::filesystem::path dir = path_from_utf8(local_data_directory());
        const std::filesystem::path current = dir / "rr-game.log", previous = dir / "rr-game.1.log";
        std::error_code ec;
        if (std::filesystem::exists(current, ec)) rename_with_retry(current, previous, ec);
        log_file = open_file(current, "w");
    } catch (...) {
        log_file = nullptr; // logging is best-effort; the game still starts
    }
    SDL_GetLogOutputFunction(&default_output, &default_userdata);
    SDL_SetLogOutputFunction(log_output, nullptr);
}
#else
void platform_game_startup(int, char**) {}
#endif
} // namespace rr
