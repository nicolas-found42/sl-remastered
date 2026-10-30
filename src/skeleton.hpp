#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Column-major affine matrices; translation is in elements 12..14.
using SkeletonMatrix = std::array<float, 16>;
struct SkeletonBone {
  std::string name;
  int32_t parent = -1;
  SkeletonMatrix local{}, bind{}, inverseBind{};
  std::array<float, 3> minimum{}, maximum{};
};
struct SkeletonResult {
  bool ok = false;
  std::vector<SkeletonBone> bones;
  std::string error;
};
// 00531e90: each record is a name, parent, four triples and three limit pairs.
// Translation is converted by 0.1. Non-six-value limits default to +/-pi.
// Invalid hierarchy, singular matrices, truncation and non-finite values fail.
SkeletonResult parseSkeleton(std::string_view text);
