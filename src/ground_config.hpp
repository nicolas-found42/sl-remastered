#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class GroundResourceArchive { GroundPack, Particles, Sound };

struct GroundResource {
  uint32_t id;
  GroundResourceArchive archive;
};

struct GroundMaterial {
  std::string name;
  float friction;
  float rollingResistance;
  float collisionCoefficient;
  GroundResource skidParticles;
  GroundResource skidMark;
  uint32_t skidMarkColor;
  GroundResource skidSound;
  float skidSoundScale;
};

struct GroundConfig {
  std::vector<GroundMaterial> materials;
};

// globalFriction is the ground_friction value after the factory's nonpositive fallback.
bool parseGroundConfig(std::string_view text, float globalFriction, GroundConfig& out, std::string& error);
