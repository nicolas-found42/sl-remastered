#include "camera_device_config.hpp"
#include "camera_device_args.hpp"
#include "tufa.hpp"
#include <cmath>
#include <cstdio>

int main() {
  auto loaded = tufaLoadFile(".scratch/2005-extract/Program Files/Street Legal Racing - Redline/system/Scripts/util/Config.class");
  if (!loaded.ok || loaded.file.classes.empty()) return 1;
  auto cls = loaded.file.classes[0];
  cls.classHeader[3] = UINT32_MAX;
  for (auto &list : cls.fields) {
    std::vector<TufaField> selected;
    for (const auto &field : list)
      if ((field.words[0] & 8) && cls.constants[field.words[1]].text.rfind("camera_", 0) == 0)
        selected.push_back(field);
    list = std::move(selected);
  }
  std::vector<const TufaClass *> classes{&cls};
  std::vector<VmStaticFields> fields;
  if (!vmInitializeClass(cls, classes, fields).ok) return 2;
  auto lookup = [&](const std::string &name) -> std::optional<VmValue> {
    for (const auto &field : fields[0].fields)
      if (field.name == name) return field.value;
    return std::nullopt;
  };
  auto config = cameraDeviceConfig(lookup);
  if (!config || config->fieldOfView != 34.375f || config->torque != 0.02f ||
      config->force != 2.0f || config->damping != 1.0f ||
      config->perpendicularDamping != -0.7f || config->nearDistance != 0.2f ||
      config->farDistance != 200.0f || config->maxForce != 20.0f ||
      !config->collisionEnabled) return 3;
  for (auto &field : fields[0].fields) {
    if (field.name == "camera_max_force") field.value = -2.0f;
    if (field.name == "camera_nocollision") field.value = int32_t{1};
    if (field.name == "camera_FOV") field.value = 2.0f;
  }
  config = cameraDeviceConfig(lookup);
  if (!config || config->fieldOfView != 110.0f || config->maxForce != 1.0f ||
      config->collisionEnabled) return 4;
  auto withoutFov = [&](const std::string &name) -> std::optional<VmValue> {
    return name == "camera_FOV" ? std::nullopt : lookup(name);
  };
  config = cameraDeviceConfig(withoutFov);
  if (!config || config->fieldOfView != 55.0f) return 5;
  auto missing = [&](const std::string &name) -> std::optional<VmValue> {
    return name == "camera_force" ? std::nullopt : lookup(name);
  };
  if (cameraDeviceConfig(missing)) return 6;
  auto args = parseCameraDeviceArgs("", 0.2f, 200.0f);
  if (args.converted || args.flags || args.lodAmplitude != 1.0f || args.lodBias != 0.0f ||
      args.nearDistance != 0.2f || args.farDistance != 200.0f) return 7;
  args = parseCameraDeviceArgs("-278.518,12.800,1039.001,0.000,0.000,0.000,0x02, 1.0,0, 0.01", 0.2f, 200.0f);
  if (args.converted != 10 || args.flags != 2 || args.lodAmplitude != 1.0f ||
      args.lodBias != 0 || std::fabs(args.nearDistance - 0.01f) > 1e-6f || args.farDistance != 200.0f ||
      std::fabs(args.position[1] - 12.8f) > 1e-5f) return 8;
  args = parseCameraDeviceArgs("1,2,3,4,5,6,0X1f,2,3,4,5", 0.2f, 200.0f);
  if (args.converted != 11 || args.flags != 31 || args.farDistance != 5) return 9;
  args = parseCameraDeviceArgs("1,2,bad,4", 0.2f, 200.0f);
  if (args.converted != 2 || args.position[2] != 0 || args.nearDistance != 0.2f) return 10;
  args = parseCameraDeviceArgs("1,2,3,4,5,6,0x,1", 0.2f, 200.0f);
  if (args.converted != 6 || args.flags != 0) return 11;
  std::puts("camera_device_config_test: ok");
  return 0;
}
