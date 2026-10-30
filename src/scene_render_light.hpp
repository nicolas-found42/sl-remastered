#pragma once

#include <array>
#include <cstdint>
#include <optional>

// Stock light instance RGB triples at native peer +0x10, +0x20, and +0x30.
struct SceneRenderLight {
  std::array<float, 3> at10{1, 1, 1};
  std::array<float, 3> at20{0.25f, 0.25f, 0.25f};
  std::array<float, 3> at30{1, 1, 1};
  struct Flare {
    int32_t textureId = 0;
    int32_t wordE0 = 0;
    float atD4 = 0;
    float atD8 = 0;
    int32_t at100 = 0;
    int32_t atE4 = 0;
  };
  std::optional<Flare> flare;

  static SceneRenderLight stockDefaults();
  // RenderRef.setLight(III)V at 00486ab0 -> FUN_0048c9d0 channel order.
  void applySetLight(uint32_t first, uint32_t second, uint32_t third);
};
