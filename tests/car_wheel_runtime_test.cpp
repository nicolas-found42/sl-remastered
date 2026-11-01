#include "car_wheel_runtime.hpp"
#include <cassert>
#include <cmath>
#include <string>
#include <type_traits>
#include <utility>

static_assert(!std::is_move_constructible_v<CarWheelRuntime>);
static_assert(!std::is_copy_constructible_v<CarWheelRuntime>);

int main() {
  std::string error;
  auto rootProto = physics::createBodyProto(1, 5, 1, 1, 1, nullptr, error);
  assert(rootProto);
  auto root = physics::createBody(*rootProto, {10, 20, 30});
  CarWheelPrototype p;
  auto proto = physics::createBodyProto(2, 1, 0.4f, 0, 0, nullptr, error);
  assert(proto);
  p.wheelBody = *proto;
  p.boneBody = *proto;
  p.position = {1, 2, 3};
  p.spring = {0.1f, 0.2f, 0.05f};
  p.friction = {1, 1};
  p.instantCenterA = {4, 5, 6};
  auto invalid = p;
  invalid.spring[1] = 0;
  assert(buildCarWheelRuntime({invalid}, root, root, {2, 3, 4}, {0.5f, 1, 1.5f},
                              100, {0, -10, 0}, error)
             .empty());
  assert(!error.empty());
  auto wheels = buildCarWheelRuntime({p}, root, root, {2, 3, 4},
                                     {0.5f, 1, 1.5f}, 100, {0, -10, 0}, error);
  assert(error.empty() && wheels.size() == 1);
  const auto &w = *wheels[0];
  assert(w.suspensionBody && w.hubBody && w.armBody);
  assert(w.suspensionBody.get() != w.hubBody.get() &&
         w.hubBody.get() != w.armBody.get());
  assert(w.suspensionBody->proto == &w.suspensionPrototype);
  assert(w.suspensionBody->flags & physics::kBodyChild);
  assert(w.suspensionPrototype.shape.type == physics::ShapeType::CylinderY);
  assert(w.anchor[0] == 2.5f && std::fabs(w.anchor[1] - 4.3f) < 0.0001f);
  assert(std::fabs(w.suspensionBody->state[0].pos[1] - 24.8f) < 0.0001f);
  assert(std::fabs(w.springForce - 5000) < 0.001f);
  assert(w.suspension.wheelBody == w.suspensionBody.get());
  assert(w.suspension.settings.force == w.springForce);
  // FUN_00490dc0 stores half the probe length as the local anchor offset.
  assert(w.suspension.anchor == physics::Vec3({0, 0.5f, 0}));
  auto transferred = std::move(wheels);
  assert(transferred[0]->suspension.wheelBody ==
         transferred[0]->suspensionBody.get());
  auto &wheel = *transferred[0];
  const auto originalPosition =
      wheel.suspensionBody->attachment->relative.position;
  const auto originalWorld =
      wheel.suspensionBody->state[wheel.suspensionBody->current].pos;
  assert(applyWheelSteeringOrientation(wheel, 0));
  assert(wheel.suspensionBody->attachment->relative.rotation() ==
         physics::matrixIdentity());
  assert(applyWheelSteeringOrientation(wheel, 0.5f));
  const auto &yaw = wheel.suspensionBody->attachment->relative.rotation();
  assert(std::fabs(yaw[0] - std::cos(0.5f)) < 1e-6f &&
         std::fabs(yaw[2] - std::sin(0.5f)) < 1e-6f &&
         std::fabs(yaw[8] + std::sin(0.5f)) < 1e-6f);
  assert(wheel.suspensionBody->attachment->relative.position ==
             originalPosition &&
         wheel.suspensionBody->state[wheel.suspensionBody->current].pos ==
             originalWorld);
  wheel.suspension.wheelBody = nullptr;
  assert(!applyWheelSteeringOrientation(wheel, 1));
  wheel.suspension.wheelBody = wheel.suspensionBody.get();
  auto oriented = p;
  oriented.orientation = {0, 0, 1.57079632679f};
  auto tilted = buildCarWheelRuntime({oriented}, root, root, {2, 3, 4},
                                     {0.5f, 1, 1.5f}, 100, {0, -10, 0}, error);
  assert(tilted.size() == 1 && applyWheelSteeringOrientation(*tilted[0], 0.5f));
  const auto &combined =
      tilted[0]->suspensionBody->attachment->relative.rotation();
  assert(std::fabs(combined[1] + 1) < 1e-6f &&
         std::fabs(combined[4] - std::cos(0.5f)) < 1e-6f &&
         std::fabs(combined[6] - std::sin(0.5f)) < 1e-6f &&
         std::fabs(combined[8] + std::sin(0.5f)) < 1e-6f);
  assert(physics::updateSuspension(transferred[0]->suspension) ==
         physics::SuspensionUpdateResult::Inactive);
  assert(std::fabs(w.initialLength - 0.5f) < 0.0001f);
  assert(w.config.instantCenterA == p.instantCenterA);
  assert(w.hubBody->proto == &w.config.wheelBody &&
         w.armBody->proto == &w.config.wheelBody);
  assert(w.hubBody->flags & 0x8000);
  assert(w.hubBody->attachment->parent == &root &&
         w.armBody->attachment->parent == &root);
  assert(w.hubBody->state[0].pos == physics::Vec3({11, 22, 33}));
  assert(w.boneBody && w.boneBody->proto == &*w.config.boneBody);
  assert(w.boneBody->attachment->parent == &root);
  assert(std::fabs(w.boneBody->state[0].pos[1] - 25.2f) < 0.0001f);
  // A loaded wheel's hub follows spring travel and yaw, while the rim frame
  // also spins about its axle. The arm does not acquire steering or spin.
  {
    auto chassis = physics::createBody(*rootProto, {0, 0, 0});
    auto spec = p;
    spec.position = {0, 0, 0};
    auto live = buildCarWheelRuntime({spec}, chassis, chassis, {0, 0, 0},
                                     {0, 0, 0}, 100, {0, -10, 0}, error);
    assert(live.size() == 1);
    auto& moving = *live[0];
    // Contact at the collider center: the stock anchor is half a collider
    // length above it. Selection, spring force and visible wheel must agree.
    auto groundProto = std::make_shared<physics::BodyProto>(
        *physics::createBodyProto(0, 5, 1, 1, 1, nullptr, error));
    auto ground = std::make_shared<physics::Body>(physics::createBody(*groundProto, {0, 0, 0}));
    physics::SuspensionContactCandidate contact;
    contact.a = moving.suspensionBody.get();
    contact.b = ground.get();
    contact.otherOwner = ground;
    contact.otherPrototype = groundProto;
    contact.pointA = {0, 0, 0};
    contact.pointB = {0, -0.2f, 0};
    contact.normal = {0, 1, 0};
    assert(physics::selectSuspensionContact(moving.suspension, contact) ==
           physics::SuspensionContactResult::Selected);
    assert(physics::updateSuspension(moving.suspension) !=
           physics::SuspensionUpdateResult::Unsupported);
    assert(std::fabs(moving.suspension.length - 0.5f) < 1e-6f);
    assert(std::fabs(moving.suspension.force - 1000.0f) < 0.01f);
    assert(updateWheelPose(moving));
    assert(std::fabs(moving.hubBody->attachment->relative.position[1] -
                     moving.radius - contact.pointB[1]) < 1e-6f);
    moving.suspension.length = 0.45f;
    moving.angularSpeed = 2;
    assert(advanceWheelRotation(moving, 0.25f));
    assert(std::fabs(moving.rotationAngle - 0.5f) < 1e-6f);
    assert(applyWheelSteeringOrientation(moving, 0.4f));
    assert(updateWheelPose(moving));
    const auto& hubFrame = moving.hubBody->attachment->relative;
    const auto& rimFrame = moving.boneBody->attachment->relative;
    const auto& armFrame = moving.armBody->attachment->relative;
    assert(std::fabs(hubFrame.position[1] - 0.25f) < 1e-6f);
    assert(rimFrame.position == hubFrame.position);
    const auto axis = physics::matrixTransformRows(hubFrame.rotation(), {0, 0, 1});
    assert(std::fabs(axis[0] - std::sin(0.4f)) < 1e-6f);
    assert(std::fabs(axis[2] - std::cos(0.4f)) < 1e-6f);
    const auto rimUp = physics::matrixTransformRows(rimFrame.rotation(), {0, 1, 0});
    const auto expectedUp = physics::matrixTransformRows(hubFrame.rotation(),
                                                         {0, std::cos(0.5f), std::sin(0.5f)});
    for (size_t i = 0; i < 3; ++i) assert(std::fabs(rimUp[i] - expectedUp[i]) < 1e-6f);
    assert(armFrame.rotation() == physics::matrixIdentity());
    moving.suspension.length = 0.55f;
    assert(updateWheelPose(moving));
    assert(std::fabs(hubFrame.position[1] - 0.15f) < 1e-6f);
    // Droop stops at the configured maximum, as in 0044fa70.
    moving.suspension.length = 5;
    assert(updateWheelPose(moving));
    assert(std::fabs(hubFrame.position[1] - 0.1f) < 1e-6f);
    moving.rotationAngle = 3;
    assert(advanceWheelRotation(moving, 0.5f));
    assert(std::fabs(moving.rotationAngle - (4 - 6.2831854820251465f)) < 1e-6f);
    moving.angularSpeed = -2;
    moving.rotationAngle = -3;
    assert(advanceWheelRotation(moving, 0.5f));
    assert(std::fabs(moving.rotationAngle - (-4 + 6.2831854820251465f)) < 1e-6f);
    const float angleBefore = moving.rotationAngle;
    assert(!advanceWheelRotation(moving, -1));
    assert(moving.rotationAngle == angleBefore);
    // Explicit refusal preserves a custom linkage instead of overwriting it
    // with the default geometry branch.
    const auto pointBefore = hubFrame.position;
    moving.config.hubValue = 1;
    assert(!updateWheelPose(moving));
    assert(hubFrame.position == pointBefore);
  }
}
