#pragma once

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <optional>
#include <string>

// FUN_00446180 defaults; FUN_0043c0a0 applies the material gate before
// scale/offset and clamping. The default gear table is NabcdefR, not a
// transmission-mode switch: osd_gear can replace the table (FUN_00445120).
struct CarHudElement {
  unsigned material = 0;
  int mode = 2;
  float x = 0, y = 0;
  unsigned color = 0xff4fe015;
  const char* format = "%03.0f";
  float offset = 0, scale = 1, minimum = 0, maximum = 999;
};

inline std::string carHudValue(const CarHudElement& element, double value) {
  if (!element.material || element.mode != 2)
    return {};
  value = std::clamp(value * element.scale + element.offset,
                     double(element.minimum), double(element.maximum));
  char text[32];
  std::snprintf(text, sizeof text, element.format, value);
  return text;
}

inline std::string carHudGear(int gear, const char* table = "NabcdefR") {
  if (gear < 0 || gear > 7)
    return {};
  char text[2];
  std::snprintf(text, sizeof text, "%c", table[gear]);
  return text;
}

inline CarHudElement carHudSpeed() {
  return {0x58, 2, 0.3f, 0.86f, 0xff4fe015, "%03.0f", 0, 1, 0, 999};
}
inline float carHudNeedle(float rpm) {
  return std::clamp(0.48f - std::fabs(rpm) * 0.0001f, -0.48f, 0.48f);
}
inline bool carHudLamp(float value) {
  return value > 0.01f; // FUN_0043c0a0 mode 4, DAT_005f0c20.
}
inline bool carHudWarning(float temperature, float rpm, float maximumRpm, float limit) {
  return temperature > 100.0f || std::fabs(rpm) > limit || std::fabs(rpm) > maximumRpm;
}

inline CarHudElement carHudScore() {
  return {0x58, 2, -0.5f, 0.75f, 0xff4fe015, "%07.0f", 0, 1, 0, 999999};
}

// FUN_004518c0 skips the update entirely without an installed system. Nullopt
// means preserve the text node, whose creation string is empty, not "0".
struct CarHudOptionalValues {
  std::optional<float> nitro, turbo;
};

struct CarHudTurbo {
  float rpmMultiplier = 0, optimumRpm = 0, plateau = 0, range = 0;
  float maximumPressure = 0, wastePressure = 0;
};

inline std::optional<float> carHudNitro(float amount, float capacity) {
  if (capacity > 0)
    return amount / capacity;
  return std::nullopt;
}

// FUN_004b6ad0. The HUD installation gate deliberately excludes wastePressure:
// an installed turbo with a closed/zero waste setting still updates to zero.
inline std::optional<float> carHudTurboValue(const CarHudTurbo& turbo, float rpm) {
  if (!(turbo.rpmMultiplier > 0 && turbo.range > 0 && turbo.maximumPressure > 0))
    return std::nullopt;
  if (!(turbo.wastePressure > 0))
    return 0.0f;
  const double distance = std::fabs(double(rpm) * turbo.rpmMultiplier - turbo.optimumRpm);
  const double envelope = distance < turbo.plateau ? 1.0 :
      std::max(0.0, 1.0 - (distance - turbo.plateau) / (turbo.range - turbo.plateau));
  const double pressure = std::min(envelope * turbo.maximumPressure, double(turbo.wastePressure));
  return float((std::sqrt(pressure + 1.0) - 1.0) /
               (std::sqrt(std::min(turbo.maximumPressure, turbo.wastePressure)) - 1.0));
}
