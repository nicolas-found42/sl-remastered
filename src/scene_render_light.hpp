#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

struct PackResource;

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
    int32_t colorType = 0;
    float rayMinWidth = 0, rayMaxWidth = 0;
    float rayMinLength = 0, rayMaxLength = 0;
    float rayFirstAngle = 0, rayRotating = 0;
    float flareMinSize = 0, flareMaxSize = 0, flareDistance = 1;
    float glowOffset = 0, minNdotV = 0;
    int32_t useNdotH = 0, textureMode = 2;
  };
  std::optional<Flare> flare;
  uint32_t sourceKind = 0;
  std::array<float, 3> position{};
  std::array<float, 3> direction{};
  float range = 0;
  float falloff = 0;
  std::array<float, 3> attenuation{1, 0, 0};
  float theta = 0;
  float phi = 0;
  bool litDynamic = true;
  std::optional<int32_t> attHTextureId;
  std::optional<int32_t> attVTextureId;
  std::array<float, 4> plane{};
  int32_t litSobj = 4;
  int32_t litDobj = 1;
  int32_t shdScSr = 3;
  int32_t shdScDr = 0;
  int32_t shdDcSr = 1;
  int32_t shdDcDr = 0;
  bool isColorable = false;
  // Color directives serialize alpha before the three RGB channels.
  float diffuseAlpha = 0;
  float specularAlpha = 0;
  float ambientAlpha = 0;

  static SceneRenderLight stockDefaults();
  // RenderRef.setLight(III)V at 00486ab0 -> FUN_0048c9d0 channel order.
  void applySetLight(uint32_t first, uint32_t second, uint32_t third);
};

struct SceneLightParseResult {
  bool ok = false;
  SceneRenderLight light;
  std::string error;
};

SceneLightParseResult sceneParseLight(const PackResource& resource);
