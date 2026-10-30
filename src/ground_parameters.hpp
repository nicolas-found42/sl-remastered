#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

struct GroundParameters {
  uint32_t resourceId = 0;
  std::array<float, 3> position{};
  std::array<float, 3> extraScalars{};
  std::string materialPath;
  std::string roadPath;
};

// Bounded complete-input subset of the scanner used by stock 00463220.
// Floats are limited to exact integers and halves with magnitude below 8388608.
// The final road path may be omitted; other missing fields, overflowing
// numbers and paths longer than 255 bytes fail.
bool parseGroundParameters(std::string_view text, GroundParameters& out, std::string& error);
