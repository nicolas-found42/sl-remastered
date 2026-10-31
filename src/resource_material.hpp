#pragma once

#include "pack_registry.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Bounded parser for mesh, flags and ordered textures in a type-14 payload.
struct ResourceMaterialResult {
  bool ok = false;
  uint32_t mesh = 0;
  std::vector<uint32_t> textures;
  uint32_t flags = 0;
  float lodBias = 0;
  float lodAmplitude = 1;
  // particles.rpk materials name their particle script config, e.g.
  // "particle particles\scripts\particle_clouds\spark.cfg". Consumed by
  // the particle system, tolerated by the general material cache.
  std::string particle;
  // Skidmark materials name their skid config instead.
  std::string skidmark;
  // Shadow directives (stock reads them in the same scan): shd_texture names a
  // type-7 texture, the others carry 1, 3 or 4 floats in file units. They only
  // feed the shadow pass, which is not drawn yet; retained in file order.
  uint32_t shadowTexture = 0;
  bool hasShadowTexture = false;
  std::vector<std::pair<std::string, std::vector<float>>> shadow;
  std::string error;
};
ResourceMaterialResult resourceMaterialParse(const PackResource &resource);
