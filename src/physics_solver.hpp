#pragma once

#include "physics_body.hpp"
#include <vector>

namespace physics {

// The one-row type 6 contact created by 004922e0. The normal points toward the first body. The offsets are
// in each body's local coordinates.
struct Contact {
  Body* first;
  Body* second;
  Vec3 normal;
  float bounce;
  Vec3 firstOffset{};
  Vec3 secondOffset{};
  float target = 0; // row +0x20; stock retains it when separating
  float impulse = 0; // row +0x28
};

enum class ContactSolveResult { Solved, InvalidBody };

// 004a3900: prepare all rows before applying any. Predicted velocities are
// read from the other state; impulses change the current state.
ContactSolveResult solveContacts(std::vector<Contact>& contacts);

} // namespace physics
