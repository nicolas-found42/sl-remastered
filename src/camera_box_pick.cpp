#include "camera_box_pick.hpp"
#include "physics_box_ray.hpp"
#include <cmath>

std::optional<CameraBoxPick> pickCameraBox(
    const std::array<float, 2>& cursor, const ViewportInstance& viewport,
    const std::array<float, 16>& cameraWorld, const std::array<float, 16>& projection,
    const std::vector<PhysicsBox>& boxes) {
  if (!std::isfinite(cursor[0]) || !std::isfinite(cursor[1]) ||
      !std::isfinite(viewport.left) || !std::isfinite(viewport.top) ||
      !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
      viewport.width <= 0 || viewport.height <= 0) return std::nullopt;
  const float x = (cursor[0] - (2 * viewport.left - 1)) / viewport.width - 1;
  const float y = (cursor[1] - (2 * viewport.top - 1)) / viewport.height - 1;
  if (x < -1 || x > 1 || y < -1 || y > 1) return std::nullopt;
  const auto near = unprojectCameraPoint({x, -y, 1}, cameraWorld, projection);
  const auto far = unprojectCameraPoint({x, -y, 0}, cameraWorld, projection);
  if (!near || !far) return std::nullopt;
  std::optional<CameraBoxPick> result;
  for (size_t i = 0; i < boxes.size(); ++i) {
    if (boxes[i].geometryFlags != 5 || boxes[i].position[0] != boxes[i].position[1]) continue;
    std::array<float, 3> start{}, end{};
    for (size_t axis = 0; axis < 3; ++axis) {
      start[axis] = (*near)[axis] * 0.1f - boxes[i].position[0][axis];
      end[axis] = (*far)[axis] * 0.1f - boxes[i].position[0][axis];
    }
    const auto fraction = physicsBoxRayFraction(start, end, boxes[i].halfExtents);
    if (fraction && (!result || *fraction < result->fraction)) {
      std::array<float, 3> contact{};
      for (size_t axis = 0; axis < 3; ++axis)
        contact[axis] = end[axis] + (start[axis] - end[axis]) * *fraction;
      result = CameraBoxPick{i, *fraction, contact};
    }
  }
  return result;
}
