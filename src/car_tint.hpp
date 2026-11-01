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

// The scheme tint lives on the body's render resource: FUN_0048c8c0 writes
// colors[scheme % count] at +0xcc and the draw reads it back (FUN_0048aa70
// caches it into the material colour). The car record's remapped-resource map
// is checked first; the render instance's own +0xcc (RenderRef.setColor) is
// the fallback source of the same stock field. 0xffffffff means "not set".
inline std::optional<uint32_t>
resolveBodyTint(const std::map<uint32_t, uint32_t>& tints, uint32_t renderType,
                uint32_t instanceColor) {
  if (const auto found = lookupBodyTint(tints, renderType))
    return found;
  if (instanceColor != 0xffffffffu)
    return instanceColor;
  return std::nullopt;
}

// FUN_00467ff0 replaces the render record's texture with the freshly set one
// (`*(record + 0x84) != *(new + 8)` then the record's setter). The record is
// per part, so the paint only lands on the parts Part.setTexture was called
// on: glass and trim records never receive it. `paintTexture` is that
// record's texture after setTexture; 0 means the part was never painted and
// keeps its material texture.
inline uint32_t bodyPaintTexture(uint32_t materialTexture, uint32_t defaultTexture,
                                 uint32_t paintTexture) {
  (void)defaultTexture;
  return paintTexture ? paintTexture : materialTexture;
}

inline void applyBodyTint(std::array<float, 4> &diffuse, uint32_t color) {
  diffuse[0] *= float((color >> 16) & 0xff) / 255.0f;
  diffuse[1] *= float((color >> 8) & 0xff) / 255.0f;
  diffuse[2] *= float(color & 0xff) / 255.0f;
}
