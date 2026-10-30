#pragma once

#include "physics_body.hpp"
#include <string>

namespace physics {
// Stock 004aa330: integrate the oriented faces, recenter the vertices, and
// populate the prototype's mass properties. Faces must have stock normals.
bool applyMeshMassProperties(BodyProto& body, const std::vector<MeshTriangle>& triangles,
                             Vec3& center, std::string& error);
}
