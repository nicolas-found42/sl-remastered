#pragma once
#include "physics_body.hpp"
#include "camera_follow_step.hpp"
#include <string>
#include <vector>

namespace physics {
// Each tree lists the root first and each child after its parent. Work that is
// not represented by this detached unit must be explicitly absent.
struct MultiRootWork {
  bool collisionFreeProven = false;
  bool contacts = false;
  bool joints = false;
  bool wheelConstraints = false;
  bool callbacks = false;
  bool waterOrClipPlanes = false;
  std::vector<CameraFollowStep> cameraCallbacks;
};
using BodyTree = std::vector<Body*>;
// Exposes the global prediction phase for testing and future world scheduling.
// All trees remain uncommitted. On failure no body changes.
bool predictRootSet(const std::vector<BodyTree>& roots, float dt, std::string& error);
// Performs the bounded no-contact world pass. No live resource links or
// registrations are created; unsupported work leaves all state unchanged.
FreeBodyStepResult stepMultiRootWorld(World& world, const std::vector<BodyTree>& roots,
                                     float dt, const MultiRootWork& work,
                                     std::string& error);
}
