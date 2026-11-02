#pragma once
#include "world_model.hpp"
#include <array>
#include <optional>
#include <vector>

struct CameraBoxPick {
  size_t index;
  float fraction;
  std::array<float, 3> localContact;
  std::array<float, 3> worldContact{};
};
struct CameraPickShape {
  const PhysicsBox* box = nullptr;
  const physics::Body* body = nullptr;
};
// FUN_0049deb0/0049f7b0: current-pose sphere, oriented box and mesh faces.
// The caller supplies traversal order; equal fractions retain the first hit.
// A null result with nonempty error means unavailable geometry, not a miss.
std::optional<CameraBoxPick> pickCameraShapes(
    const std::array<float, 2>& cursor, const ViewportInstance& viewport,
    const std::array<float, 16>& cameraWorld, const std::array<float, 16>& projection,
    const std::vector<CameraPickShape>& shapes, std::string& error);
// Ordered static boxes with identity orientation; coordinates use the stock
// renderer-to-physics 0.1 scale. The fraction is measured from the far end.
std::optional<CameraBoxPick> pickCameraBox(
    const std::array<float, 2>& cursor, const ViewportInstance& viewport,
    const std::array<float, 16>& cameraWorld, const std::array<float, 16>& projection,
    const std::vector<PhysicsBox>& boxes);
