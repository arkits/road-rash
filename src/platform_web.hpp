#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace rr {
std::vector<uint8_t> web_read_asset(const std::string& path);
bool web_asset_exists(const std::string& path);
std::string web_room_invite();
// Negative counts indicate a stage with unknown total work.
void web_loading(const char* stage, int completed = -1, int total = -1);
} // namespace rr
