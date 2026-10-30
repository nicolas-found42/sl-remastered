#pragma once

#include <array>
#include <optional>

// Stock 00498240 measures the fraction from end toward start.
std::optional<float> physicsBoxRayFraction(const std::array<float, 3>& start,
                                           const std::array<float, 3>& end,
                                           const std::array<float, 3>& halfExtents);
