// Plan 33b Task 6: trusted roots for wss:// connections.
#pragma once
#include <string>

namespace rr::net {
// PEM file holding the operating system's trusted root certificates, written
// once per process (Windows ROOT store, macOS anchors). OpenSSL is linked
// statically there, so its default CA paths point at the build machine's
// OpenSSL (Homebrew or vcpkg) and are missing on players' machines. On Linux
// (plan 33a; the packages link OpenSSL statically too, built with
// OPENSSLDIR=/etc/ssl) it is $SSL_CERT_FILE when set, else the first existing
// distribution bundle (Debian/Ubuntu/Arch/SteamOS, Fedora/RHEL, openSUSE,
// Alpine). Empty when there is none or the store cannot be read.
const std::string& system_ca_file();
// Number of certificates in that file (0 when there is none).
int system_ca_count();
} // namespace rr::net
