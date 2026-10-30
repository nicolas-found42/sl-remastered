#pragma once
#include "camera_follow_force.hpp"
#include <string>
#include <vector>

// A bounded no-contact world slice. The caller must prove that no collision,
// constraint, water/clip-plane or unrepresented callback work exists. This
// is not a general world step and does not support compound or sleeping bodies.
struct CameraFollowStep {
  physics::Body *camera = nullptr, *look = nullptr, *move = nullptr;
  CameraFollowForceInput input;
  CameraFollowSmoothing *smoothing = nullptr;
};
bool stepCameraFollowWorld(physics::World &world, float dt,
                           const std::vector<CameraFollowStep> &callbacks,
                           bool noOtherPhysicsWork, std::string &error);
