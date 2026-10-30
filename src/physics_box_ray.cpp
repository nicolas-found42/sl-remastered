#include "physics_box_ray.hpp"

#include <algorithm>
#include <cmath>

std::optional<float> physicsBoxRayFraction(const std::array<float, 3>& start,
                                           const std::array<float, 3>& end,
                                           const std::array<float, 3>& halfExtents) {
  float enter = 0.0f;
  float exit = 1.0f;
  for (size_t axis = 0; axis < 3; ++axis) {
    const float a = start[axis], b = end[axis], h = halfExtents[axis];
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(h) || h < 0.0f)
      return std::nullopt;
    if ((a > h && b > h) || (a < -h && b < -h)) return std::nullopt;
    const float delta = a - b;
    if (delta == 0.0f) continue;
    const float first = (-std::copysign(h, delta) - b) / delta;
    const float last = (std::copysign(h, delta) - b) / delta;
    enter = std::max(enter, first);
    exit = std::min(exit, last);
  }
  // 0049deb0 rejects the zero fraction and the current nearest fraction (initially 1).
  if (enter > exit || enter <= 0.0f || enter >= 1.0f) return std::nullopt;
  return enter;
}
