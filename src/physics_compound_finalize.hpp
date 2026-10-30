#pragma once
#include "physics_body.hpp"

// 0049a050/00498f50 for mesh/box compound roots with represented children.
// Children are in insertion order and must retain stable addresses.
bool finalizeCompound(physics::Body& root, physics::BodyProto& prototype,
                      const std::vector<physics::Body*>& children, std::string& error);
