#include "camera_follow_step.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

bool stepCameraFollowWorld(physics::World &world, float dt,
                           const std::vector<CameraFollowStep> &callbacks,
                           bool noOtherPhysicsWork, std::string &error) {
  error.clear();
  const auto fail = [&](const char *reason) { error = reason; return false; };
  if (!noOtherPhysicsWork || !std::isfinite(dt) || dt <= 0 ||
      !std::isfinite(world.pending) || world.pending < 0 ||
      !std::isfinite(world.time) || !std::isfinite(world.pending + dt))
    return fail("camera step: unsupported world");
  const auto present = [&](const physics::Body *b) {
    return b && std::find(world.bodies.begin(), world.bodies.end(), b) != world.bodies.end();
  };
  for (size_t i=0; i<world.bodies.size(); ++i) {
    const auto *b = world.bodies[i];
    const auto current = world.bodies.begin() + static_cast<std::ptrdiff_t>(i);
    if (!b || !b->proto || b->current > 1 || b->attachment ||
        !(b->flags & physics::kBodyActive) ||
        (b->flags & (physics::kBodyChild | physics::kBodyHasChildren | physics::kBodyBit10 |
                     physics::kBodyAsleep)) ||
        std::find(world.bodies.begin(), current, b) != current)
      return fail("camera step: unsupported body");
  }
  const float total=world.pending+dt;
  for (const auto &cb : callbacks) {
    if (!present(cb.camera) || (cb.input.hasLookTarget && !present(cb.look)) || (cb.input.hasMoveTarget && !present(cb.move)) || !cb.smoothing ||
        (cb.camera->proto->flags & physics::kProtoStatic) || cb.camera->proto->invMass <= 0 ||
        !cb.input.otherBranchesInactive)
      return fail("camera step: unavailable target or unsupported callback");
    auto input=cb.input;
    input.camera=cb.camera->state[cb.camera->current];
    if (input.hasLookTarget) input.lookTarget=cb.look->state[cb.look->current];
    if (input.hasMoveTarget) input.moveTarget=cb.move->state[cb.move->current];
    if (!cameraFollowForce(input,*cb.smoothing)) return fail("camera step: unsupported follow");
  }
  if (total < physics::k::minimumStep) { world.pending=total; return true; }
  // Back up all mutable world/body/smoothing state; later checks may still fail.
  const auto oldWorld=world;
  std::vector<physics::Body> oldBodies;
  for (const auto *b : world.bodies) oldBodies.push_back(*b);
  std::vector<CameraFollowSmoothing> oldSmoothing;
  for (const auto &cb : callbacks) oldSmoothing.push_back(*cb.smoothing);
  const auto rollback = [&] {
    for (size_t i=0;i<world.bodies.size();++i) *world.bodies[i]=oldBodies[i];
    for (size_t i=0;i<callbacks.size();++i) *callbacks[i].smoothing=oldSmoothing[i];
    world=oldWorld;
  };
  world.pending=0;
  world.lastStep=total;
  for (auto *b : world.bodies) physics::accumulateWorldForces(world.params,*b);
  for (const auto &cb : callbacks) {
    auto input=cb.input;
    input.camera=cb.camera->state[cb.camera->current];
    if (input.hasLookTarget) input.lookTarget=cb.look->state[cb.look->current];
    if (input.hasMoveTarget) input.moveTarget=cb.move->state[cb.move->current];
    const auto follow=cameraFollowForce(input,*cb.smoothing);
    if (!follow) { rollback(); return fail("camera step: callback failed"); }
    *cb.smoothing=follow->smoothing;
    physics::addForce(*cb.camera,follow->force,nullptr);
    auto &state=cb.camera->state[cb.camera->current];
    for (size_t i=0;i<3;++i) state.torque[i]+=follow->torque[i];
  }
  for (auto *b : world.bodies) if (physics::stepBody(*b,total)) ++world.stepsDone;
  for (auto *b : world.bodies)
    if (physics::commit(*b)==physics::CommitResult::WentToSleep) physics::sleepBodyState(*b);
  world.time+=total;
  if (!std::isfinite(world.time)) { rollback(); return fail("camera step: time overflow"); }
  return true;
}
