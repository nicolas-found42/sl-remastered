#pragma once
#include "world_model.hpp"
#include <array>
#include <optional>
#include <vector>

struct CameraBoxPick {
  size_t index;
  float fraction;
  std::array<float, 3> localContact;
};
// Ordered static boxes with identity orientation; coordinates use the stock
// renderer-to-physics 0.1 scale. The fraction is measured from the far end.
std::optional<CameraBoxPick> pickCameraBox(
    const std::array<float, 2>& cursor, const ViewportInstance& viewport,
    const std::array<float, 16>& cameraWorld, const std::array<float, 16>& projection,
    const std::vector<PhysicsBox>& boxes);
