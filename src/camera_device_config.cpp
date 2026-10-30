#include "camera_device_config.hpp"

std::optional<CameraDeviceConfig> cameraDeviceConfig(const CameraConfigLookup &lookup) {
  const auto number = [&](const char *name) -> std::optional<float> {
    auto value = lookup(name);
    if (!value) return std::nullopt;
    const auto *f = std::get_if<float>(&*value);
    return f ? std::optional<float>(*f) : std::nullopt;
  };
  const auto integer = [&](const char *name) -> std::optional<int32_t> {
    auto value = lookup(name);
    if (!value) return std::nullopt;
    const auto *i = std::get_if<int32_t>(&*value);
    return i ? std::optional<int32_t>(*i) : std::nullopt;
  };
  const auto aov = number("camera_AOV"), torque = number("camera_torque"),
      force = number("camera_force"), damping = number("camera_damping"),
      perp = number("camera_perpdamping"), near = number("camera_ext_clipplane"),
      far = number("camera_ext_viewrange"), max = number("camera_max_force");
  const auto noCollision = integer("camera_nocollision");
  if (!aov || !torque || !force || !damping || !perp || !near || !far || !max ||
      !noCollision) return std::nullopt;
  const auto fovValue = lookup("camera_FOV");
  float fieldOfView = *aov;
  if (fovValue) {
    const auto *fov = std::get_if<float>(&*fovValue);
    if (!fov) return std::nullopt;
    fieldOfView *= *fov;
  }
  return CameraDeviceConfig{fieldOfView, *torque, *force, *damping, -*perp,
                            *near, *far, *max <= 0.0f ? 1.0f : *max,
                            *noCollision == 0};
}

std::optional<CameraDeviceConfig> cameraDeviceConfig(const VmArena &arena) {
  return cameraDeviceConfig([&](const std::string &name) {
    return vmActiveStaticField(arena, "java.util.Config", name);
  });
}
