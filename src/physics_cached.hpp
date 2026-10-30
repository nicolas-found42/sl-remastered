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
