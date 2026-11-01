#pragma once

#include "physics_polyhedron.hpp"
#include <optional>

namespace physics {
// The point/face case of 004af970, when the point remains inside the face's
// side planes. The point core is the origin; featureIndex is the existing
// cached face. Other cases return nullopt without changing the cache.
std::optional<CorePair> cachedPointFace(const State& pointPose,
    const CollisionCore& polyhedron, const State& polyPose,
    std::size_t featureIndex, bool pointFirst = true);
}

namespace physics {
// FUN_004af970's 0x103/0x301 case: 004af4f0 checks the vertex against the
// cached face's side planes; 004ad0a0 keeps that vertex as the local point.
// It does not rewalk the vertex core's neighbors. nullopt leaves the caller's
// retained row untouched and denotes an invalid or unsupported fixed pair.
std::optional<CorePair> cachedVertexFace(const CollisionCore& vertexCore,
    const State& vertexPose, std::size_t vertexFeature,
    const CollisionCore& faceCore, const State& facePose,
    std::size_t faceFeature, bool vertexFirst = true);
}
