#include "cursor_resource.hpp"
#include <algorithm>
#include <cstdio>

int main() {
  CursorPixels cursor;
  std::string error;
  if (!loadStockCursor("ref/raw/StreetLegal_Redline.bak.exe", 116, cursor, error)) {
    std::printf("FAIL stock cursor: %s\n", error.c_str()); return 1;
  }
  if (cursor.width != 32 || cursor.height != 32 || cursor.hotspotX != 1 ||
      cursor.hotspotY != 1 || cursor.rgba.size() != 4096) return 1;
  size_t transparent = 0, visible = 0;
  for (size_t i = 3; i < cursor.rgba.size(); i += 4) {
    transparent += cursor.rgba[i] == 0;
    visible += cursor.rgba[i] == 255;
  }
  if (transparent != 749 || visible != 275) return 1;
  if (loadStockCursor("ref/raw/StreetLegal_Redline.bak.exe", 65535, cursor, error) ||
      !cursor.rgba.empty() || error.empty()) return 1;
  std::puts("cursor_resource_test: ok");
  return 0;
}
