#pragma once

#include "physics_polyhedron.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace physics {
// 004af970 for isolated point and segment cores. Points are in each core's
// local coordinates; normal points from B to A in world coordinates. Other
// topologies return nullopt, including segment/segment and polyhedra.
std::optional<CorePair> closestPointSegmentCores(const CollisionCore& a, const State& poseA,
                                                  const CollisionCore& b, const State& poseB);
}

namespace physics {
// 004ae9c0 for a single point against a convex core. The cache is an index
// into the polyhedron's creation-ordered features; nullopt starts at feature 0.
// An unsupported or invalid core, or a walk that does not converge, returns
// nullopt without changing the cache.
std::optional<CorePair> closestPointPolyhedron(const State& pointPose,
    const CollisionCore& polyhedron, const State& polyPose,
    std::optional<std::size_t>& featureCache, bool pointFirst = true);
}

namespace physics {
// 004ae9c0 for two segment cores. Cached feature indices refer to each core's
// creation-ordered feature array.
std::optional<CorePair> closestSegmentCores(const CollisionCore& a, const State& poseA,
    const CollisionCore& b, const State& poseB,
    std::optional<std::size_t>& featureA, std::optional<std::size_t>& featureB);
}

namespace physics {
// 004ae9c0 for a segment and a convex polyhedron. Unsupported transitions
// return nullopt without changing either cached feature.
std::optional<CorePair> closestSegmentPolyhedron(const CollisionCore& segment, const State& segmentPose,
    const CollisionCore& polyhedron, const State& polyPose,
    std::optional<std::size_t>& segmentFeature, std::optional<std::size_t>& polyFeature,
    bool segmentFirst = true);
}

namespace physics {
// 004ae9c0 for convex cores. Unsupported feature transitions return nullopt
// without changing either cache; each index refers to core.features.
std::optional<CorePair> closestPolyhedronCores(const CollisionCore& a, const State& poseA,
    const CollisionCore& b, const State& poseB,
    std::optional<std::size_t>& featureA, std::optional<std::size_t>& featureB);
}

namespace physics {
// Diagnostics for the core-pair walks. Every nullopt return is a site (file id
// and source line) that is counted; the walk loop cap of 004ae9c0 (stock prints
// "Voronoi error (infinite loop)") is counted separately. The car step reports
// changes through runlog.
enum class PairSiteFile : unsigned { CorePair = 0, ContactGeometry = 1 };
std::optional<CorePair> corePairUnavailable(PairSiteFile file, unsigned line);
void corePairCountLoopCap();
// Empty when nothing changed since the previous call.
std::string corePairDiagnosticsDelta();
}
