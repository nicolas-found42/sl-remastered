#pragma once

#include "car_config.hpp"
#include "physics_body.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

// A parsed wheel and its collision prototypes. This does not create live bodies
// or simulate suspension. Positions and angles remain in configuration space.
struct CarWheelPrototype {
  size_t wheelIndex = 0;
  physics::BodyProto wheelBody;
  std::optional<physics::BodyProto> boneBody;
  CarVec3 position{};
  CarVec3 orientation{};
  std::array<float, 2> friction{};
  std::array<float, 3> spring{};
  std::array<float, 4> wheelParameters{};
  CarVec3 bonePosition{};
  float wheelDeform = 1;
  float armValue = -1;
  CarVec3 armAnchor{};
  CarVec3 armDirection{};
  float hubValue = -1;
  std::array<float, 6> hubValues{};
  CarVec3 hubAnchor{};
  CarVec3 instantCenterA{};
  CarVec3 instantCenterB{};
  bool hasWheel = false;
};

// 00446180 multiplies wheel parameters 3 and 4 by the owning part's
// +0xd8 and +0xdc, respectively, then resets those fields to one.
// The caller supplies their values before that reset.
bool buildCarWheelGeometry(const CarConfig& config, const std::array<float, 2>& partWheelFactors,
                           std::vector<CarWheelPrototype>& result, std::string& error);
