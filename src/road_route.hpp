#pragma once
#include "road_network.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// FUN_00483400 bounded pass: the route start spline tangent at the start
// position, oriented along the road toward the finish and negated as the
// stock handler does. Null when either endpoint cannot be aligned to a road
// or the tangent is degenerate.
std::optional<std::array<float, 3>> roadStartDirection(const RoadNetwork& network,
                                                       const std::array<float, 3>& start, const std::array<float, 3>& finish);

// FUN_00583fd0 bounded pass: per-"other" halt flags for the route between the
// crossings nearest start and finish. The flags are 1 on the road records of
// one shortest crossing-to-crossing path and 0 elsewhere. Null when no route
// exists or the inputs are unsupported.
std::optional<std::vector<uint8_t>> roadHaltTrafficPath(const RoadNetwork& network,
                                                        const std::array<float, 3>& start, const std::array<float, 3>& finish);
