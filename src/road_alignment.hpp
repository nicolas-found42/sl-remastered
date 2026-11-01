#pragma once
#include "road_network.hpp"

#include <array>
#include <optional>

// Snapped road-spline pose for FUN_00583a00: the query point projected onto
// the nearest road spline in the XZ plane (stock GroundMap_lookupPathNearXZ
// 0x00581BF0 ignores y for the lookup), with the unit spline tangent negated
// when the spline parameter is at or past half the owning spline's length
// (RouteSpline_evalTangent 0x0057EB00 half-path flip). Each on-disk
// RoadSegment is a cubic Hermite piece: words[0..2] start point, words[3..5]
// end point, words[6..8] and words[9..11] the start/end tangents, words[12]
// the segment length. Non-finite inputs or a network without usable segments
// return nullopt.
struct RoadAlignment {
  std::array<float, 3> position{};
  std::array<float, 3> direction{};
};

std::optional<RoadAlignment> roadAlignToRoad(const RoadNetwork& network,
                                             const std::array<float, 3>& position);
