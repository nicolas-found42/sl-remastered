#pragma once

#include "physics_body.hpp"

namespace physics {

// 0049a840 establishes the relative pose without changing either world pose.
bool attachBody(Body& child, Body& parent);

// Writes only the fields written by the child branch of 004a7c10. The caller
// supplies the parent's predicted state, which may itself be a child prediction.
void predictChildPose(const State& parent, const RelativeBodyPose& relative, State& out);

}  // namespace physics

