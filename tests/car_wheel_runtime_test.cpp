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
}
