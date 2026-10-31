#include "net/system_ca.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace rr::net {
namespace {
using Der = std::vector<uint8_t>;

std::vector<Der> os_roots() {
    std::vector<Der> roots;
#ifdef _WIN32
    if (HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT")) {
        for (PCCERT_CONTEXT c = CertEnumCertificatesInStore(store, nullptr); c;
             c = CertEnumCertificatesInStore(store, c))
            if (c->dwCertEncodingType & X509_ASN_ENCODING)
                roots.emplace_back(c->pbCertEncoded, c->pbCertEncoded + c->cbCertEncoded);
        CertCloseStore(store, 0);
    }
#elif defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CFArrayRef anchors = nullptr;
    if (SecTrustCopyAnchorCertificates(&anchors) == errSecSuccess && anchors) {
        for (CFIndex i = 0; i < CFArrayGetCount(anchors); ++i) {
            auto cert = (SecCertificateRef)CFArrayGetValueAtIndex(anchors, i);
            if (CFDataRef data = SecCertificateCopyData(cert)) {
                const auto* p = CFDataGetBytePtr(data);
                roots.emplace_back(p, p + CFDataGetLength(data));
                CFRelease(data);
            }
        }
        CFRelease(anchors);
    }
#pragma clang diagnostic pop
#endif
    return roots;
}

std::string pem(const std::vector<Der>& roots) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (const auto& der : roots) {
        out += "-----BEGIN CERTIFICATE-----\n";
        std::string line;
        for (size_t i = 0; i < der.size(); i += 3) {
            const uint32_t n = uint32_t(der[i]) << 16 | (i + 1 < der.size() ? uint32_t(der[i + 1]) << 8 : 0) |
                               (i + 2 < der.size() ? uint32_t(der[i + 2]) : 0);
            line += table[n >> 18 & 63];
            line += table[n >> 12 & 63];
            line += i + 1 < der.size() ? table[n >> 6 & 63] : '=';
            line += i + 2 < der.size() ? table[n & 63] : '=';
            if (line.size() == 64) {
                out += line + "\n";
                line.clear();
            }
        }
        if (!line.empty()) out += line + "\n";
        out += "-----END CERTIFICATE-----\n";
    }
    return out;
}

struct CaFile {
    std::string path;
    int count = 0;
};

const CaFile& ca_file() {
    static const CaFile file = [] {
        CaFile f;
        const auto roots = os_roots();
        if (roots.empty()) return f;
        std::error_code ec;
        const auto dir = std::filesystem::temp_directory_path(ec);
        if (ec) return f;
        // One shared name, replaced atomically; a racing process that holds
        // it open makes the rename fail, and its equivalent file is used.
        const auto target = dir / "RoadRashNative-ca-roots.pem";
#ifdef _WIN32
        const auto pid = GetCurrentProcessId();
#else
        const auto pid = getpid();
#endif
        const auto temp = dir / ("RoadRashNative-ca-roots." + std::to_string(pid) + ".tmp");
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << pem(roots);
            if (!out.flush()) return f;
        }
        std::filesystem::rename(temp, target, ec);
        if (ec) std::filesystem::remove(temp, ec);
        if (!std::filesystem::is_regular_file(target, ec)) return f;
        f.path = target.string();
        f.count = int(roots.size());
        return f;
    }();
    return file;
}
} // namespace

const std::string& system_ca_file() {
    return ca_file().path;
}

int system_ca_count() {
    return ca_file().count;
}
} // namespace rr::net
