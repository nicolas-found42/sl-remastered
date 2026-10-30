#include "camera_device_command.hpp"
#include "car_config.hpp"
#include "car_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

std::array<std::string, 3> seatCameraCommands(uint32_t target, const CarDriver &driver,
                                              const CarCameraSettings &settings) {
  const auto &v = driver.extCamera;
  char look[256], move[256], zoom[64];
  std::snprintf(look, sizeof look, "look 0x%x %f,%f,%f 0,0,-1 %f", target,
                static_cast<double>(v[1]), static_cast<double>(v[2]), static_cast<double>(v[3]), -1.0);
  std::snprintf(move, sizeof move, "move 0x%x %f,%f,%f %f %f,%f,%f", target,
                static_cast<double>(v[4]), static_cast<double>(v[5]), static_cast<double>(v[6]),
                static_cast<double>(v[10] * settings.distanceScale),
                static_cast<double>(v[7]), static_cast<double>(v[8]), static_cast<double>(v[9]));
  const float targetZoom = v[0] * settings.fieldOfViewScale;
  if (targetZoom > 0) std::snprintf(zoom, sizeof zoom, "zoom %f", static_cast<double>(targetZoom));
  else zoom[0] = '\0';
  return {look, move, zoom};
}

bool parseCameraFollowCommand(std::string_view text, float currentTorque,
                              CameraFollowCommand &command) {
  const auto name = stockScan(text, "%s");
  if (name.values.empty() || (name.values[0].s != "look" && name.values[0].s != "move")) return false;
  command = {};
  command.look = name.values[0].s == "look";
  command.direction = command.look ? std::array<float, 3>{0, 0, -1} : std::array<float, 3>{};
  command.value = command.look ? currentTorque : 0;
  const auto parsed = stockScan(text, command.look
      ? "%*s %d %f,%f,%f %f,%f,%f %f %f,%f,%f"
      : "%*s %d %f,%f,%f %f %f,%f,%f");
  if (!parsed.values.empty()) command.target = static_cast<uint32_t>(parsed.values[0].i);
  for (size_t i = 0; i < 3 && i + 1 < parsed.values.size(); ++i) command.offset[i] = parsed.values[i + 1].f;
  if (command.look) {
    for (size_t i = 0; i < 3 && i + 4 < parsed.values.size(); ++i) command.direction[i] = parsed.values[i + 4].f;
    if (parsed.values.size() > 7) command.value = parsed.values[7].f;
  } else {
    if (parsed.values.size() > 4) command.value = parsed.values[4].f;
    for (size_t i = 0; i < 3 && i + 5 < parsed.values.size(); ++i) command.direction[i] = parsed.values[i + 5].f;
  }
  return true;
}

void applyCameraFollowCommand(CameraCommandState &state, const CameraFollowCommand &command) {
  if (command.look) {
    state.lookOffset = command.offset;
    const auto &v = command.direction;
    const float length = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    state.lookDirection = length > 0 ? std::array<float, 3>{v[0]/length, v[1]/length, v[2]/length}
                                     : std::array<float, 3>{0, 0, -1};
    if (command.value > 0) state.torque = command.value;
  } else {
    state.moveOffset = command.offset;
    state.moveRotation = command.direction;
    state.moveDistance = command.value;
    if (command.value < state.minDistance) state.minDistance = command.value;
    if (command.value > state.maxDistance) state.maxDistance = command.value;
  }
}

CameraCommandState cameraCommandState(const CameraDeviceConfig &config) {
  CameraCommandState state{};
  state.torque = config.torque;
  state.force = config.force;
  state.damping = config.damping;
  state.perpendicularDamping = config.perpendicularDamping;
  state.zoomCurrent = state.zoomTarget = config.fieldOfView;
  return state;
}

bool applyCameraCommand(CameraCommandState &state, std::string_view command) {
  const auto name = stockScan(command, "%s");
  if (name.values.empty()) return false;
  const auto &word = name.values[0].s;
  if (word == "zoom") {
    const auto parsed = stockScan(command, "%*s %f %f");
    if (!parsed.values.empty()) state.zoomTarget = parsed.values[0].f;
    if (parsed.values.size() > 1) state.zoomSpeed = parsed.values[1].f;
    state.zoomTarget = std::clamp(state.zoomTarget, 5.0f, 120.0f);
    if (state.zoomSpeed < 0) state.zoomCurrent = state.zoomTarget;
    return true;
  }
  float *fields[3]{};
  const char *format = nullptr;
  if (word == "dist") {
    fields[0] = &state.minDistance; fields[1] = &state.maxDistance;
    format = "%*s %f %f";
  } else if (word == "smooth") {
    fields[0] = &state.smoothPosition; fields[1] = &state.smoothRotation;
    format = "%*s %f %f";
  } else if (word == "force") {
    fields[0] = &state.force; fields[1] = &state.damping;
    fields[2] = &state.perpendicularDamping;
    format = "%*s %f %f %f";
  } else if (word == "torque") {
    fields[0] = &state.torque; format = "%*s %f";
  } else if (word == "roll") {
    fields[0] = &state.roll; format = "%*s %f";
  } else return false;
  const auto parsed = stockScan(command, format);
  for (size_t i = 0; i < parsed.values.size(); ++i)
    *fields[i] = parsed.values[i].f;
  return true;
}
