// Browser boundary only. All parsing, hashing, simulation and rendering remain
// in the same library as the native application. Game files never use network I/O.
#include "platform_web.hpp"
#include "import_job.hpp"
#include "importer.hpp"
#include <emscripten.h>
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>

EM_ASYNC_JS(int, read_local_asset, (const char* path, unsigned* length), {
    try {
        const file = await Module.localAssets.read(UTF8ToString(path));
        const data = new Uint8Array(await file.arrayBuffer());
        const ptr = _malloc(data.length || 1);
        if (!ptr) throw new Error('Not enough memory to load game data');
        HEAPU8.set(data, ptr);
        HEAPU32[length >>> 2] = data.length;
        return ptr;
    } catch (error) {
        Module.assetError = String(error.message || error);
        return 0;
    }
});
EM_JS(int, has_local_asset, (const char* path), { return Module.localAssets.names.has(UTF8ToString(path)); });
EM_JS(void, copy_asset_error, (char* message, unsigned capacity),
      { stringToUTF8(Module.assetError || 'Game data unavailable. Reload to retry.', message, capacity); });
EM_JS(void, report_web_loading, (const char* stage, int completed, int total),
      { Module.webLoading ?.(UTF8ToString(stage), completed, total); });
EM_JS(void, copy_room_invite, (char* out, unsigned capacity),
      { stringToUTF8(new URLSearchParams(globalThis.location.search).get("room") || "", out, capacity); });
namespace rr {
std::string web_room_invite() {
    char room[257]{};
    copy_room_invite(room, sizeof(room));
    return room;
}
void web_loading(const char* stage, int completed, int total) {
    report_web_loading(stage, completed, total);
}
std::vector<uint8_t> web_read_asset(const std::string& path) {
    unsigned length = 0;
    auto* bytes = reinterpret_cast<uint8_t*>(read_local_asset(path.c_str(), &length));
    if (!bytes) {
        char message[1024]{};
        copy_asset_error(message, sizeof(message));
        throw std::runtime_error(path + ": " + message);
    }
    std::vector<uint8_t> result(bytes, bytes + length);
    std::free(bytes);
    return result;
}
bool web_asset_exists(const std::string& path) {
    return has_local_asset(path.c_str());
}
} // namespace rr
namespace {
struct BrowserSink final : rr::ImportSink {
    std::string path;
    std::vector<uint8_t> data;
    bool ready = false;
    void begin_file(const std::string& p, uint64_t size) override {
        if (ready) throw std::runtime_error("Unflushed browser import file");
        path = p;
        data.clear();
        data.reserve(size_t(size));
    }
    void write(const uint8_t* bytes, size_t size) override { data.insert(data.end(), bytes, bytes + size); }
    void end_file(bool ok) override { ready = ok; }
    bool commit() override { return true; } // JS publishes the marker after the final OPFS write.
    void abort() override {
        ready = false;
        data.clear();
    }
} sink;
std::unique_ptr<rr::ImportJob> job;
std::string response;
std::string quote(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') {
            out += '\\';
            out += char(c);
        } else if (c < 32)
            out += ' ';
        else
            out += char(c);
    }
    return out + "\"";
}
} // namespace
extern "C" {
EMSCRIPTEN_KEEPALIVE void rr_web_visibility(int hidden) {
    SDL_Event event{};
    event.type = hidden ? SDL_EVENT_DID_ENTER_BACKGROUND : SDL_EVENT_DID_ENTER_FOREGROUND;
    SDL_PushEvent(&event);
}
EMSCRIPTEN_KEEPALIVE void rr_import_begin(int iso) {
    job.reset();
    sink = BrowserSink{};
    job = std::make_unique<rr::ImportJob>(rr::reference_hashes(), sink, iso != 0);
}
EMSCRIPTEN_KEEPALIVE const char* rr_import_next() {
    if (!job) return "{\"error\":\"Import not initialized\"}";
    if (job->done()) {
        const auto& result = job->result();
        if (result.ok) return "{\"done\":true}";
        response = result.error;
        for (const auto& p : result.missing) response += " Missing: " + p;
        for (const auto& p : result.mismatched) response += " Hash mismatch: " + p;
        response = "{\"error\":" + quote(response) + "}";
        return response.c_str();
    }
    if (auto range = job->need_range()) {
        response =
            "{\"offset\":" + std::to_string(range->offset) + ",\"size\":" + std::to_string(range->size) + "}";
        return response.c_str();
    }
    if (auto file = job->need_file()) {
        response = "{\"file\":" + quote(*file) + "}";
        return response.c_str();
    }
    return "{\"error\":\"Import made no progress\"}";
}
EMSCRIPTEN_KEEPALIVE void rr_import_supply(const uint8_t* data, unsigned size) {
    job->supply(data, size);
}
EMSCRIPTEN_KEEPALIVE void rr_import_missing() {
    job->missing_file("Not found in selected folder");
}
EMSCRIPTEN_KEEPALIVE void rr_import_cancel() {
    if (job) job->cancel();
}
EMSCRIPTEN_KEEPALIVE const char* rr_import_output() {
    return sink.ready ? sink.path.c_str() : "";
}
EMSCRIPTEN_KEEPALIVE const uint8_t* rr_import_data() {
    return sink.data.data();
}
EMSCRIPTEN_KEEPALIVE unsigned rr_import_size() {
    return unsigned(sink.data.size());
}
EMSCRIPTEN_KEEPALIVE unsigned rr_import_count() {
    return unsigned(job->progress().done);
}
EMSCRIPTEN_KEEPALIVE void rr_import_flushed() {
    sink.ready = false;
    sink.data.clear();
}
}
