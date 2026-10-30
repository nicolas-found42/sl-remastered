#pragma once

#include "vm.hpp"
#include <functional>
#include <optional>
#include <string>

struct CameraDeviceConfig {
  float fieldOfView, torque, force, damping, perpendicularDamping;
  float nearDistance, farDistance, maxForce;
  bool collisionEnabled;
};

using CameraConfigLookup = std::function<std::optional<VmValue>(const std::string &)>;
std::optional<CameraDeviceConfig> cameraDeviceConfig(const CameraConfigLookup &lookup);
std::optional<CameraDeviceConfig> cameraDeviceConfig(const VmArena &arena);
