#pragma once

#include "physics_body.hpp"

namespace physics {

// A child pose is relative to the compound before its center is moved.
struct CompoundMassChild {
  BodyProto prototype;
  Vec3 position{};
  Mat34 rotation = matrixIdentity();
};

struct CompoundMass {
  Vec3 center{};
  BodyProto prototype;
};

// Mass and inertia portion of 0049a050. Input is insertion order; stock
// prepends each child and traverses the resulting list in reverse order.
// Bounds, child pose adjustment and the special body flag 0x8000 are separate.
CompoundMass aggregateCompoundMass(const std::vector<CompoundMassChild>& children);

}  // namespace physics
