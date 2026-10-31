#include "car_wheel_runtime.hpp"
#include "part_config.hpp"
#include "run_log.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
physics::Mat34 rotationMatrix(const std::array<float, 9> &r) {
  return {r[0], r[1], r[2], 0, r[3], r[4], r[5], 0, r[6], r[7], r[8], 0};
}
} // namespace

bool applyWheelSteeringOrientation(CarWheelRuntime &wheel, float angle) {
  auto *body = wheel.suspensionBody.get();
  if (!std::isfinite(angle) || !body || wheel.suspension.wheelBody != body ||
      !(body->flags & physics::kBodyChild) || !body->attachment ||
      !body->attachment->parent)
    return false;
  PartSlotDirective turn{};
  turn.orientation[0] = angle;
  PartSlotState state;
  if (!makePartSlotState(turn, state))
    return false;
  const auto composed = physics::matrixMultiply(wheel.baseSteeringRotation,
                                                rotationMatrix(state.rotation));
  body->attachment->relative.setRotation(composed);
  return true;
}

std::vector<std::unique_ptr<CarWheelRuntime>>
buildCarWheelRuntime(const std::vector<CarWheelPrototype> &prototypes,
                     physics::Body &carRoot, const physics::Body &partBody,
                     const physics::Vec3 &partCenter,
                     const physics::Vec3 &partOffset, float mass,
                     const physics::Vec3 &gravity, std::string &error) {
  error.clear();
  std::vector<std::unique_ptr<CarWheelRuntime>> result;
  result.reserve(prototypes.size());
  const auto &root = carRoot.state[carRoot.current];
  const float gravityLength =
      std::sqrt(gravity[2] * gravity[2] + gravity[1] * gravity[1] +
                gravity[0] * gravity[0]);
  for (const auto &p : prototypes) {
    if (!std::isfinite(p.spring[1]) || p.spring[1] <= 0) {
      error = "invalid wheel spring travel";
      return {};
    }
    auto wheel = std::make_unique<CarWheelRuntime>();
    wheel->config = p;
    // 004433e0 sets wheel record +0x28 to 1 (the extra sscanf
    // destination is not present in the ten-conversion format); 004493c0
    // copies that field to live wheel +0xc4.
    wheel->transferCoefficient = p.hasWheel ? 1.0f : 0.0f;
    PartSlotDirective base{};
    for (size_t i = 0; i < 3; ++i)
      base.orientation[i] = p.orientation[i];
    PartSlotState baseState;
    if (!makePartSlotState(base, baseState)) {
      error = "invalid wheel base orientation";
      return {};
    }
    wheel->baseSteeringRotation = rotationMatrix(baseState.rotation);
    const float radius = p.wheelBody.shape.dim[0];
    wheel->radius = radius;
    wheel->restLength = radius + p.spring[1] + p.spring[0];
    const physics::Vec3 offset{partCenter[0] - partOffset[0],
                               partCenter[1] - partOffset[1] +
                                   (wheel->restLength - radius),
                               partCenter[2] - partOffset[2]};
    for (size_t i = 0; i < 3; ++i) {
      wheel->anchor[i] = offset[i] + p.position[i];
      wheel->instantCenterA[i] = offset[i] + p.instantCenterA[i];
      wheel->instantCenterB[i] = offset[i] + p.instantCenterB[i];
    }
    physics::Vec3 local{wheel->anchor[0] + partOffset[0],
                        wheel->anchor[1] + partOffset[1] - p.friction[0] * 0.5f,
                        wheel->anchor[2] + partOffset[2]};
    // Ride-height data capture (one-shot): the placement chain per wheel.
    if (runlog::active()) {
      static unsigned placementLogged = 0;
      if (placementLogged < 4) {
        ++placementLogged;
        runlog::event(
            runlog::EventKind::Note,
            "wheel placement %u: cfgPos=%.3f,%.3f,%.3f partCenter=%.3f,%.3f,"
            "%.3f partOffset=%.3f,%.3f,%.3f anchor=%.3f,%.3f,%.3f "
            "local=%.3f,%.3f,%.3f restLength=%.3f radius=%.3f friction0=%.3f",
            placementLogged, p.position[0], p.position[1], p.position[2],
            partCenter[0], partCenter[1], partCenter[2], partOffset[0],
            partOffset[1], partOffset[2], wheel->anchor[0], wheel->anchor[1],
            wheel->anchor[2], local[0], local[1], local[2], wheel->restLength,
            radius, p.friction[0]);
      }
    }
    auto bodyProto = physics::createBodyProto(0.01f, 3, p.friction[0] * 0.5f,
                                              0.05f, 0, nullptr, error);
    if (!bodyProto)
      return {};
    wheel->suspensionPrototype = std::move(*bodyProto);
    wheel->suspensionBody = std::make_unique<physics::Body>(
        physics::createBody(wheel->suspensionPrototype,
                            physics::transformPoint(root.pos, root.rot, local),
                            &root.rot, root.time));
    wheel->suspensionBody->flags |= physics::kBodyChild | 0x40;
    if (!physics::attachBody(*wheel->suspensionBody, carRoot)) {
      error = "invalid suspension attachment";
      return {};
    }
    const auto &partState = partBody.state[partBody.current];
    const auto hubPosition =
        physics::transformPoint(partState.pos, partState.rot, p.position);
    wheel->hubBody = std::make_unique<physics::Body>(physics::createBody(
        wheel->config.wheelBody, hubPosition, &partState.rot, root.time));
    wheel->armBody = std::make_unique<physics::Body>(physics::createBody(
        wheel->config.wheelBody, hubPosition, &partState.rot, root.time));
    wheel->hubBody->flags |= 0x8020;
    wheel->armBody->flags |= 0x8020;
    if (!physics::attachBody(*wheel->hubBody, carRoot) ||
        !physics::attachBody(*wheel->armBody, carRoot)) {
      error = "invalid wheel helper attachment";
      return {};
    }
    wheel->springForce =
        ((gravityLength * mass) / static_cast<float>(prototypes.size())) /
        p.spring[1];
    wheel->damping = wheel->springForce * p.spring[2];
    if (!std::isfinite(wheel->springForce) || !std::isfinite(wheel->damping)) {
      error = "invalid wheel suspension force";
      return {};
    }
    wheel->reboundDamping = wheel->damping * 0.7f;
    wheel->minimumLength = std::max(0.0f, radius - 1.0f);
    const float maxLength = p.spring[0] + 0.1f + radius;
    wheel->maximumLength = maxLength > 0 ? maxLength : wheel->restLength;
    wheel->initialLength = p.spring[0] + radius;
    wheel->suspension.settings = physics::configureSuspension(
        wheel->springForce, wheel->damping, wheel->restLength,
        wheel->maximumLength, wheel->minimumLength, wheel->reboundDamping,
        radius);
    wheel->suspension.wheelBody = wheel->suspensionBody.get();
    // FUN_0049f4d0 measures anchor -> contact point with the anchor taken
    // from the wheel's predicted world state (state pos, i.e. the hub
    // origin). A wheel-local Y offset here mixes frames: the distance then
    // far exceeds maxLength, the suspension never extends, and no spring
    // or tire force can exist.
    wheel->suspension.anchor = {0, 0, 0};
    wheel->suspension.length = wheel->initialLength;
    const float constructorLength =
        p.friction[0] > 0 ? p.friction[0] : 0.9000000357627869f;
    wheel->suspension.distanceSquared =
        constructorLength * constructorLength + 0.1f;
    if (wheel->config.boneBody) {
      PartSlotDirective angles{};
      for (size_t i = 0; i < 3; ++i)
        angles.orientation[i] = p.orientation[i];
      PartSlotState orientation;
      if (!makePartSlotState(angles, orientation)) {
        error = "invalid wheel bone orientation";
        return {};
      }
      const float shift =
          radius - std::min(wheel->maximumLength, wheel->suspension.length);
      physics::Vec3 point{};
      for (size_t i = 0; i < 3; ++i)
        point[i] = orientation.rotation[i * 3 + 1] * shift + wheel->anchor[i] +
                   partOffset[i];
      wheel->boneBody = std::make_unique<physics::Body>(physics::createBody(
          *wheel->config.boneBody,
          physics::transformPoint(partState.pos, partState.rot, point),
          &partState.rot, root.time));
      wheel->boneBody->flags |= 0x8020;
      if (!physics::attachBody(*wheel->boneBody, carRoot)) {
        error = "invalid wheel bone attachment";
        return {};
      }
    }
    result.push_back(std::move(wheel));
  }
  return result;
}
