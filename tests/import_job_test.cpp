#include "import_job.hpp"
#include "importer.hpp"
#include "iso_fixture.hpp"
#include "sha256.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include "temp_dir.hpp"

namespace fs = std::filesystem;
namespace {

// In-memory sink: records files and whether commit/abort ran.
struct MemSink : rr::ImportSink {
    std::map<std::string, std::vector<uint8_t>> files;
    std::string current;
    bool committed = false, aborted = false;
    void begin_file(const std::string& path, uint64_t) override {
        current = path;
        files[path].clear();
    }
    void write(const uint8_t* data, size_t size) override {
        files[current].insert(files[current].end(), data, data + size);
    }
    void end_file(bool ok) override {
        if (!ok) files.erase(current);
    }
    bool commit() override {
        committed = true;
        return true;
    }
    void abort() override { aborted = true; }
};

std::vector<rr::ReferenceHash> table() {
    return {{"A.BIN", rr::sha256(std::vector<uint8_t>{'h', 'e', 'l', 'l', 'o'})}};
}

// Answer a single need from the fixture image (short at end of image).
// Returns false when the job needs nothing (done or failed).
bool step_once(rr::ImportJob& job, const std::vector<uint8_t>& image) {
    if (auto r = job.need_range()) {
        const size_t have = r->offset < image.size() ? image.size() - size_t(r->offset) : 0;
        const size_t n = std::min<size_t>(have, r->size);
        job.supply(n ? image.data() + r->offset : nullptr, n);
        return true;
    }
    return false;
}

// Synchronous driver over the ISO fixture image: answers every range need
// from memory, supplying only the bytes that exist (short at end of image).
void drive_iso(rr::ImportJob& job, const std::vector<uint8_t>& image) {
    for (int guard = 0; guard < 100000 && !job.done(); ++guard) {
        if (!step_once(job, image)) {
            if (!job.done())
                job.fail_source("unexpected file need for ISO source");
            else
                break;
        }
    }
}

} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << '\n';
        }
    };
    const auto image = fixture::iso();

    // 1. A sync driver feeding exact ranges produces the same files and
    // ImportResult as import_game on the same fixture.
    {
        MemSink sink;
        rr::ImportJob job(table(), sink, true);
        drive_iso(job, image);
        check(job.done(), "job finishes");
        check(job.result().ok && job.result().mismatched.empty() && job.result().missing.empty(),
              "job result ok");
        check(sink.committed && !sink.aborted, "sink committed, not aborted");
        check(sink.files.count("A.BIN") &&
                  sink.files["A.BIN"] == std::vector<uint8_t>{'h', 'e', 'l', 'l', 'o'},
              "imported bytes match");
        const auto tmp =
            fs::temp_directory_path() / ("rr-import-job-sync-" + rr::test::process_tag() + ".iso");
        const auto dest =
            fs::temp_directory_path() / ("rr-import-job-sync-" + rr::test::process_tag() + ".out");
        fs::remove_all(dest);
        {
            std::ofstream f(tmp, std::ios::binary);
            f.write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));
        }
        const auto expected = rr::import_game(tmp.string(), dest.string(), table());
        check(expected.ok && sink.files["A.BIN"].size() == fs::file_size(dest / "A.BIN"),
              "matches import_game");
        fs::remove_all(dest);
        fs::remove(tmp);
    }

    // 2. Random delays between need and supply (simulated by alternating
    // single steps between two jobs) still succeed.
    {
        MemSink s1, s2;
        rr::ImportJob a(table(), s1, true), b(table(), s2, true);
        for (int guard = 0; guard < 200000 && (!a.done() || !b.done()); ++guard) {
            if (!a.done() && !step_once(a, image)) break;
            if (!b.done() && !step_once(b, image)) break;
            // Extra delay: idle turns where neither job is fed.
            if ((guard % 3) == 2) continue;
            if (!a.done() && !step_once(a, image)) break;
        }
        check(a.done() && a.result().ok && b.done() && b.result().ok, "interleaved jobs succeed");
        check(s1.files == s2.files && !s1.files.empty(), "interleaved bytes identical");
    }

    // 3. One modified byte fails with that file in mismatched; abort runs,
    // commit never runs.
    {
        auto bad = image;
        bad[20 * 2048] ^= 0xff;
        MemSink sink;
        rr::ImportJob job(table(), sink, true);
        drive_iso(job, bad);
        check(job.done() && !job.result().ok, "corrupt import fails");
        check(job.result().mismatched == std::vector<std::string>{"A.BIN"}, "modified file listed");
        check(sink.aborted && !sink.committed, "abort called, commit never called");
        check(!sink.files.count("A.BIN"), "bad file not delivered to sink");
    }

    // 4. cancel() mid-file aborts the sink.
    {
        MemSink sink;
        rr::ImportJob job(table(), sink, true);
        // Advance to the first need, then cancel before supplying anything.
        check(job.need_range().has_value(), "job starts with a need");
        job.cancel();
        check(job.done() && !job.result().ok, "canceled job done and not ok");
        check(sink.aborted && !sink.committed, "cancel aborts the sink");
    }

    // 5. fail_source ends the job with the message in result().error.
    {
        MemSink sink;
        rr::ImportJob job(table(), sink, true);
        job.fail_source("disc unreadable");
        check(job.done() && !job.result().ok, "failed job done and not ok");
        check(job.result().error == "disc unreadable", "source message in result");
        check(sink.aborted && !sink.committed, "fail_source aborts the sink");
    }

    // 6. A directory record pointing past the end of the image fails cleanly.
    {
        auto bad = image;
        // PVD root record (offset 16*2048+156): point its extent far past EOF.
        const size_t at = 16 * 2048 + 156;
        const uint32_t far = 0x00ffffff;
        for (int i = 0; i < 4; ++i) {
            bad[at + 2 + i] = uint8_t(far >> (i * 8));
            bad[at + 6 - i] = uint8_t(far >> (i * 8));
        }
        MemSink sink;
        rr::ImportJob job(table(), sink, true);
        drive_iso(job, bad); // driver supplies only bytes that exist
        check(job.done() && !job.result().ok, "bad record fails");
        check(!job.result().error.empty(), "bad record error recorded");
        check(sink.aborted && !sink.committed, "bad record aborts, never commits");
    }

    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
