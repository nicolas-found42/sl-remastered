#pragma once

#include "road_network.hpp"

#include <cmath>
#include <cstring>
#include <optional>

// Only fields established by 0057f1a0 and 00579800; traffic remains unmodeled.
struct RoadOtherInitialState {
  float time = 0;
  float curveLength = 0;
  uint8_t fieldC4 = 0;
  float fieldC8 = 0;
};

inline std::optional<RoadOtherInitialState> roadOtherInitialState(
    const RoadOtherRecord& record, float time) {
  if (!std::isfinite(time) || record.segments.empty()) return std::nullopt;
  float length = 0;
  for (const auto& segment : record.segments) {
    float value;
    static_assert(sizeof(value) == sizeof(segment.words[12]));
    std::memcpy(&value, &segment.words[12], sizeof(value));
    if (!std::isfinite(value)) return std::nullopt;
    length += value;
    if (!std::isfinite(length)) return std::nullopt;
  }
  return RoadOtherInitialState{time, length, 0, 0};
}
