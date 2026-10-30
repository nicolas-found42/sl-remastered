#pragma once

#include <array>
#include <cstdint>
#include <string_view>

struct CameraDeviceArgs {
  std::array<float, 3> position{};
  std::array<float, 3> orientation{};
  uint32_t flags = 0;
  float lodAmplitude = 1.0f;
  float lodBias = 0.0f;
  float nearDistance = 0.0f;
  float farDistance = 0.0f;
  unsigned converted = 0;
};

CameraDeviceArgs parseCameraDeviceArgs(std::string_view input, float nearDistance, float farDistance);
