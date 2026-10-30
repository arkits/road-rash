#include "importer.hpp"
#include "import_job.hpp"
#include "iso9660.hpp"
#include "platform_files.hpp"
#include "sha256.hpp"
#include "utf8_path.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
namespace rr {
namespace fs = std::filesystem;
namespace {
struct CloseIO {
    void operator()(SDL_IOStream* p) const {
        if (p) SDL_CloseIO(p);
    }
};
using IO = std::unique_ptr<SDL_IOStream, CloseIO>;
void validate_path(const std::string& path) {
    if (path.empty() || path.find('\\') != std::string::npos || path.find('\0') != std::string::npos ||
        path_from_utf8(path).is_absolute())
        throw std::runtime_error("Invalid import reference path");
    if (path == ".rr-import" || path == ".rr-source.iso")
        throw std::runtime_error("Reserved import metadata path");
    for (const auto& part : path_from_utf8(path))
        if (part == ".." || part == "." || part.empty())
            throw std::runtime_error("Import reference escapes destination");
    // (The ImportJob constructor re-validates the table with the same rules.)
}
std::vector<uint8_t> load_file(const fs::path& path) {
    IO io(SDL_IOFromFile(path_to_utf8(path).c_str(), "rb"));
    if (!io) throw std::runtime_error("Cannot open source file: " + path_to_utf8(path));
    const Sint64 size = SDL_GetIOSize(io.get());
    if (size < 0 || size > 256 * 1024 * 1024)
        throw std::runtime_error("Invalid source file size: " + path_to_utf8(path));
    std::vector<uint8_t> result(static_cast<size_t>(size));
    if (SDL_ReadIO(io.get(), result.data(), result.size()) != result.size())
        throw std::runtime_error("Truncated source file: " + path_to_utf8(path));
    return result;
}
// Plan 33 Task A5: disc folders are found case-insensitively. The Linux
// iso9660 driver lowercases plain ISO9660 names by default (map=normal), and a
// copied disc may use any case; the ISO path already compares upper-case.
// Two directory entries that differ only in case are ambiguous and reported,
// never guessed between.
class CaseResolver {
  public:
    explicit CaseResolver(fs::path root) : root_(std::move(root)) {}
    // root/relative with each component matched case-insensitively; a
    // component that is not found is kept as written (reported as missing).
    fs::path resolve(const std::string& relative) {
        fs::path at = root_;
        for (const auto& part : path_from_utf8(relative)) {
            const auto& entries = listing(at);
            const auto found = entries.find(upper(path_to_utf8(part)));
            if (found == entries.end()) {
                at /= part;
                continue;
            }
            if (found->second.size() > 1)
                throw std::runtime_error("Ambiguous disc folder: " + path_to_utf8(found->second[0]) +
                                         " and " + path_to_utf8(found->second[1]) + " differ only in case");
            at /= found->second[0];
        }
        return at;
    }

  private:
    static std::string upper(std::string s) {
        for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }
    const std::map<std::string, std::vector<fs::path>>& listing(const fs::path& directory) {
        auto [it, inserted] = listings_.try_emplace(directory);
        if (inserted) {
            std::error_code error;
            for (fs::directory_iterator entry(directory, error), end; !error && entry != end;
                 entry.increment(error)) {
                const fs::path name = entry->path().filename();
                it->second[upper(path_to_utf8(name))].push_back(name);
            }
            for (auto& [key, names] : it->second) std::sort(names.begin(), names.end()); // total-order: path
        }
        return it->second;
    }
    fs::path root_;
    std::map<fs::path, std::map<std::string, std::vector<fs::path>>> listings_;
};
// ImportSink writing verified files into the staging directory. commit()
// writes the marker and renames staging to the destination, exactly as the
// former blocking import_game did; abort() removes the staging tree.
class FilesystemSink : public ImportSink {
  public:
    FilesystemSink(fs::path staging, fs::path destination, size_t file_count)
        : staging_(std::move(staging)), destination_(std::move(destination)), file_count_(file_count) {}
    void begin_file(const std::string& path, uint64_t) override {
        current_.clear();
        const auto output = staging_ / path;
        fs::create_directories(output.parent_path());
        stream_.open(output, std::ios::binary);
        if (!stream_) throw std::runtime_error("Cannot write imported file: " + path);
        current_ = path;
    }
    void write(const uint8_t* data, size_t size) override {
        stream_.write(reinterpret_cast<const char*>(data), std::streamsize(size));
        if (!stream_) throw std::runtime_error("Cannot write imported file: " + current_);
    }
    void end_file(bool ok) override {
        stream_.close();
        if (!ok) {
            std::error_code error;
            fs::remove(staging_ / current_, error);
        }
        current_.clear();
    }
    bool commit() override {
        std::error_code error;
        fs::remove(staging_ / ".rr-source.iso", error);
        std::ofstream marker(staging_ / ".rr-import", std::ios::binary);
        marker << "rr-import 1\n" << file_count_ << '\n';
        marker.close();
        if (!marker) throw std::runtime_error("Cannot write import marker");
        if (fs::exists(destination_))
            fs::remove(
                destination_); // previously checked empty; remove refuses a raced-in nonempty directory
        // Plan 33b: retried on Windows, where scanners briefly lock new files.
        if (!rename_with_retry(staging_, destination_, error))
            throw std::runtime_error("Cannot move the verified import into place: " + error.message());
        return true;
    }
    void abort() override {
        std::error_code error;
        if (!staging_.empty()) fs::remove_all(staging_, error);
    }

  private:
    fs::path staging_, destination_;
    size_t file_count_ = 0;
    std::ofstream stream_;
    std::string current_;
};
} // namespace
const std::vector<ReferenceHash>& reference_hashes() {
    static const std::vector<ReferenceHash> hashes = {
#include "reference_hashes.inc"
    };
    return hashes;
}
std::string data_root() {
    if (const char* override_path = SDL_getenv("RR_PREF_OVERRIDE"))
        return path_to_utf8(path_from_utf8(override_path) / "game" / "ROADRASH");
    char* pref = SDL_GetPrefPath("RoadRashNative", "RoadRash");
    if (!pref) throw std::runtime_error(SDL_GetError());
    const fs::path roaming_game = path_from_utf8(pref) / "game";
    SDL_free(pref);
    // Plan 33b Task 2: the 518 MB imported disc lives in machine-local data
    // (%LOCALAPPDATA% on Windows; the same pref path elsewhere). A tree that an
    // earlier build imported into the roaming profile is moved there once.
    const fs::path local_game = path_from_utf8(local_data_directory()) / "game";
    std::error_code error;
    if (local_game != roaming_game && !fs::exists(local_game, error) && fs::is_directory(roaming_game, error))
        migrate_directory(roaming_game, local_game);
    return path_to_utf8(local_game / "ROADRASH");
}
ImportResult import_game(const std::string& source, const std::string& dest,
                         const std::function<void(const ImportProgress&)>& progress) {
    return import_game(source, dest, reference_hashes(), progress);
}
ImportResult import_game(const std::string& source, const std::string& dest,
                         const std::vector<ReferenceHash>& hashes,
                         const std::function<void(const ImportProgress&)>& progress) {
    ImportResult result;
    fs::path staging;
    try {
        if (hashes.empty()) throw std::runtime_error("Empty import reference table");
        std::set<std::string> paths;
        for (const auto& entry : hashes) {
            validate_path(entry.path);
            if (!paths.insert(entry.path).second || entry.hash.size() != 64 ||
                entry.hash.find_first_not_of("0123456789abcdef") != std::string::npos)
                throw std::runtime_error("Invalid import reference table");
        }
        const fs::path destination(dest);
        if (dest.empty() || destination == destination.root_path())
            throw std::runtime_error("Invalid import destination");
        if (fs::exists(destination) && (!fs::is_directory(destination) || !fs::is_empty(destination)))
            throw std::runtime_error("Import destination already contains files: " + dest);
        if (!destination.parent_path().empty()) fs::create_directories(destination.parent_path());
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            const fs::path candidate =
                dest + ".partial." + std::to_string(SDL_GetTicksNS()) + "." + std::to_string(attempt);
            if (fs::create_directory(candidate)) {
                staging = candidate;
                break;
            }
        }
        if (staging.empty()) throw std::runtime_error("Cannot create import staging directory");
        std::error_code source_error;
        const bool folder = fs::is_directory(path_from_utf8(source), source_error);
        fs::path source_root = path_from_utf8(source);
        if (folder) {
            const fs::path nested = CaseResolver(source_root).resolve("ROADRASH");
            if (fs::is_directory(nested, source_error)) source_root = nested;
        }
        CaseResolver disc(source_root);
        IO iso_io;
        if (!folder) {
            iso_io.reset(SDL_IOFromFile(source.c_str(), "rb"));
            if (!iso_io) throw std::runtime_error(std::string("Cannot open ISO: ") + SDL_GetError());
            // Android document providers may expose an unseekable pipe. Cache
            // it inside our exclusively owned staging directory before parsing.
            if (SDL_SeekIO(iso_io.get(), 0, SDL_IO_SEEK_SET) != 0) {
                const auto cache = staging / ".rr-source.iso";
                std::ofstream stream(cache, std::ios::binary);
                std::array<uint8_t, 65536> buffer;
                uint64_t copied = 0;
                for (;;) {
                    if (progress) progress({0, hashes.size(), "Reading disc image"});
                    const size_t n = SDL_ReadIO(iso_io.get(), buffer.data(), buffer.size());
                    if (n) {
                        copied += n;
                        if (copied > 4ull * 1024 * 1024 * 1024)
                            throw std::runtime_error("Disc image exceeds import limit");
                        stream.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(n));
                        if (!stream) throw std::runtime_error("Cannot cache disc image");
                    }
                    if (n < buffer.size()) {
                        const auto status = SDL_GetIOStatus(iso_io.get());
                        if (status == SDL_IO_STATUS_EOF) break;
                        if (status != SDL_IO_STATUS_READY || !n)
                            throw std::runtime_error("Cannot read disc image stream");
                    }
                }
                stream.close();
                if (!stream) throw std::runtime_error("Cannot finish disc image cache");
                iso_io.reset(SDL_IOFromFile(path_to_utf8(cache).c_str(), "rb"));
                if (!iso_io) throw std::runtime_error("Cannot reopen cached disc image");
            }
        }
        // The import itself runs as a pull-based job: the driver below
        // answers byte-range/file needs from SDL_IOStream reads (the browser
        // will answer them from picked files instead). A throwing progress
        // callback cancels with its message, as before.
        FilesystemSink sink(staging, destination, hashes.size());
        ImportJob job(hashes, sink, !folder);
        std::vector<uint8_t> chunk;
        try {
            for (int guard = 0; guard < 10000000 && !job.done(); ++guard) {
                if (progress) progress(job.progress());
                if (auto file = job.need_file()) {
                    try {
                        const auto bytes = load_file(disc.resolve(*file));
                        job.supply(bytes.data(), bytes.size());
                    } catch (const std::exception& e) {
                        job.missing_file(e.what());
                    }
                    continue;
                }
                auto range = job.need_range();
                if (!range) break;
                chunk.assign(range->size, 0);
                size_t got = 0;
                if (range->size) {
                    if (range->offset <= uint64_t(INT64_MAX) &&
                        SDL_SeekIO(iso_io.get(), Sint64(range->offset), SDL_IO_SEEK_SET) ==
                            Sint64(range->offset))
                        got = SDL_ReadIO(iso_io.get(), chunk.data(), chunk.size());
                }
                chunk.resize(got);
                job.supply(chunk.data(), chunk.size());
            }
            if (progress) progress(job.progress());
            if (!job.done()) throw std::runtime_error("Import stalled with no pending need");
            result = job.result();
            if (result.ok) staging.clear(); // committed: owned by the destination now
        } catch (const std::exception& e) {
            job.fail_source(e.what());
            result = job.result();
        }
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    if (!staging.empty()) {
        std::error_code error;
        fs::remove_all(staging, error);
    }
    return result;
}
} // namespace rr
