#include "cursor_resource.hpp"
#include "platform.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

int main() {
  CursorPixels cursor;
  std::string error;
  const char* exe = "ref/raw/StreetLegal_Redline.bak.exe";
  if (!loadStockCursor(exe, 116, cursor, error)) {
    std::printf("FAIL stock cursor: %s\n", error.c_str());
    return 1;
  }
  if (cursor.width != 32 || cursor.height != 32 || cursor.hotspotX != 1 ||
      cursor.hotspotY != 1 || cursor.rgba.size() != 4096) return 1;
  size_t transparent = 0, visible = 0;
  for (size_t i = 3; i < cursor.rgba.size(); i += 4) {
    transparent += cursor.rgba[i] == 0;
    visible += cursor.rgba[i] == 255;
  }
  if (transparent != 749 || visible != 275) return 1;
  if (loadStockCursor(exe, 65535, cursor, error) ||
      !cursor.rgba.empty() || error.empty()) return 1;
  // The stock index->group mapping (FUN_004b7bd0) addresses cursor groups
  // 0x74..0x79 and 0x7b..0x7e; every mapped group must exist in the stock
  // executable and decode as a cursor, while the skipped slot 0x7a holds no
  // cursor resource at all.
  for (uint16_t group : {0x74, 0x75, 0x76, 0x77, 0x78,
                         0x79, 0x7b, 0x7c, 0x7d, 0x7e}) {
    if (!loadStockCursor(exe, group, cursor, error) ||
        cursor.width != 32 || cursor.height != 32 || cursor.rgba.size() != 4096) {
      std::printf("FAIL cursor group 0x%x: %s\n", group, error.c_str());
      return 1;
    }
  }
  if (loadStockCursor(exe, 0x7a, cursor, error) || !cursor.rgba.empty())
    return 1;
  // Behavioural check of the mapping through PlatformInput (SDL dummy video,
  // no window): a fresh cursor slot with an unreadable executable fails...
  if (!SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy")) return 1;
  auto input = PlatformInput::create(error);
  if (!input) {
    std::printf("FAIL platform input: %s\n", error.c_str());
    return 1;
  }
  if (input->selectStockCursor("/nonexistent-slrr-cursor.exe", 5, true, error) ||
      error.empty())
    return 1;
  // ...every index 0..9 selects its mapped group successfully (index 5 now
  // recovers after the failed load)...
  for (int index = 0; index <= 9; ++index) {
    if (!input->selectStockCursor(exe, index, index % 2 == 0, error)) {
      std::printf("FAIL select index %d: %s\n", index, error.c_str());
      return 1;
    }
  }
  // ...out-of-range indices clamp to index 0 (group 0x74) instead of failing
  // or reading past the mapping table...
  if (!input->selectStockCursor(exe, 10, true, error) ||
      !input->selectStockCursor(exe, -3, false, error))
    return 1;
  // ...and a cached cursor is reused without re-reading the executable.
  if (!input->selectStockCursor("/nonexistent-slrr-cursor.exe", 2, true, error))
    return 1;
  std::puts("cursor_resource_test: ok");
  return 0;
}
