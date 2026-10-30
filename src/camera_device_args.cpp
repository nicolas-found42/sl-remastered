#include "camera_device_args.hpp"
#include "car_config.hpp"

CameraDeviceArgs parseCameraDeviceArgs(std::string_view input, float nearDistance, float farDistance) {
  CameraDeviceArgs out;
  out.nearDistance = nearDistance;
  out.farDistance = farDistance;
  const auto scan = stockScan(input, "%f,%f,%f,%f,%f,%f,%d,%f,%f,%f,%f");
  float *floats[] = {&out.position[0], &out.position[1], &out.position[2],
                     &out.orientation[0], &out.orientation[1], &out.orientation[2],
                     &out.lodAmplitude, &out.lodBias, &out.nearDistance, &out.farDistance};
  for (size_t i = 0; i < scan.values.size(); ++i) {
    if (i == 6)
      out.flags = static_cast<uint32_t>(scan.values[i].i);
    else
      *floats[i < 6 ? i : i - 1] = scan.values[i].f;
  }
  out.converted = static_cast<unsigned>(scan.values.size());
  return out;
}
