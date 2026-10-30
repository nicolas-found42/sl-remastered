#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct CursorPixels {
  uint32_t width = 0, height = 0, hotspotX = 0, hotspotY = 0;
  std::vector<uint8_t> rgba;
};
// Decode the selected stock PE group cursor and its 4-bit image. Windows
// screen-inversion pixels are rejected because SDL color cursors cannot
// represent them. No extracted game data is written to disk.
bool loadStockCursor(const std::filesystem::path &exe, uint16_t groupId,
                     CursorPixels &out, std::string &error);
