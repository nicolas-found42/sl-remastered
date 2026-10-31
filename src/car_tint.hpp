#pragma once
// Per-body paint (stock FUN_0048c8c0 writes colors[scheme % count] onto a body
// record's render resource). Palette entries are 0x00RRGGBB, so only RGB tints.
#include <array>
#include <cstdint>
#include <map>
#include <optional>

inline std::optional<uint32_t>
lookupBodyTint(const std::map<uint32_t, uint32_t> &tints,
               uint32_t renderType) {
  const auto found = tints.find(renderType);
  if (found == tints.end())
    return std::nullopt;
  return found->second;
}

inline void applyBodyTint(std::array<float, 4> &diffuse, uint32_t color) {
  diffuse[0] *= float((color >> 16) & 0xff) / 255.0f;
  diffuse[1] *= float((color >> 8) & 0xff) / 255.0f;
  diffuse[2] *= float(color & 0xff) / 255.0f;
}
