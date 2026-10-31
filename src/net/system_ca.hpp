// Plan 33b Task 6: trusted roots for wss:// connections.
#pragma once
#include <string>

namespace rr::net {
// PEM file holding the operating system's trusted root certificates, written
// once per process (Windows ROOT store, macOS anchors). OpenSSL is linked
// statically there, so its default CA paths point at the build machine's
// OpenSSL (Homebrew or vcpkg) and are missing on players' machines. Empty on
// Linux, where the distribution's OpenSSL finds the system bundle itself,
// and when the store cannot be read.
const std::string& system_ca_file();
// Number of certificates in that file (0 when there is none).
int system_ca_count();
} // namespace rr::net
