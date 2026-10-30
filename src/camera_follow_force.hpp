#pragma once

#include "camera_device_command.hpp"
#include "physics_body.hpp"
#include <optional>

// Bounded direct-physics-target part of stock 00438b60. The caller must establish
// that the callback's other force, roll, height, plane, angle and zoom branches
// are inactive; this unit does not step bodies or wake them.
struct CameraFollowForceInput {
  physics::State camera, lookTarget, moveTarget;
  CameraCommandState command;
  physics::Vec3 cameraLookOffset{};
  float fieldOfView = 0;
  float maxForce = 1;
  bool otherBranchesInactive = false;
};
struct CameraFollowSmoothing {
  physics::Vec3 look{}, move{};
  bool initializeLook = true, initializeMove = true;
};
struct CameraFollowForceResult {
  CameraFollowSmoothing smoothing;
  physics::Vec3 force{}, torque{};
};
// Only live, unattached direct physics targets are represented. A zero look
// direction or non-finite data is unsupported, with no partial state change.
// When reject is set and the result is empty, *reject names the first failed
// check with a 4-character code. Success clears it.
std::optional<CameraFollowForceResult> cameraFollowForce(
    const CameraFollowForceInput &input, const CameraFollowSmoothing &previous,
    const char **reject = nullptr);
