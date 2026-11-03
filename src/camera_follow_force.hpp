#pragma once

#include "camera_device_command.hpp"
#include "physics_body.hpp"
#include <optional>

// Stock camera force callback: manual local controls and common roll/height,
// plus either free-mode velocity damping or direct-physics-target follow.
// The caller establishes that unrepresented plane/angle branches are inactive.
struct CameraFollowForceInput {
  physics::State camera, lookTarget, moveTarget;
  CameraCommandState command;
  CameraControlState control;
  physics::Vec3 cameraLookOffset{};
  float fieldOfView = 0;
  float maxForce = 1;
  bool otherBranchesInactive = false;
  bool hasLookTarget = true, hasMoveTarget = true;
};
struct CameraFollowSmoothing {
  physics::Vec3 look{}, move{};
  bool initializeLook = true, initializeMove = true;
};
struct CameraFollowForceResult {
  CameraFollowSmoothing smoothing;
  physics::Vec3 force{}, torque{};
};
// Only live, unattached direct physics follow targets are represented. Free
// mode needs neither target. A zero follow look
// direction or non-finite data is unsupported, with no partial state change.
// When reject is set and the result is empty, *reject names the first failed
// check with a 4-character code. Success clears it.
std::optional<CameraFollowForceResult> cameraFollowForce(
    const CameraFollowForceInput& input, const CameraFollowSmoothing& previous,
    const char** reject = nullptr);
