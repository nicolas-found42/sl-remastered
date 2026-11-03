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
  } else if (word == "height") {
    // FUN_0043a4b0: "%s %f" into +0xa8, then +0xbc = 0.3 unless it is zero.
    fields[0] = &state.height; format = "%*s %f";
  } else if (word == "maxpitch") {
    fields[0] = &state.maxPitch; format = "%*s %f";
  } else return false;
  const auto parsed = stockScan(command, format);
  for (size_t i = 0; i < parsed.values.size(); ++i)
    *fields[i] = parsed.values[i].f;
  if (word == "height") state.maxPitch = state.height == 0.0f ? 1.3f : 0.3f;
  return true;
}

// 2.3.1 004897b0: preserve the SSE single-precision operation grouping.
bool updateCameraControl(CameraCommandState& command, CameraControlState& control,
                         const std::array<float, 12>& axes, bool hasController,
                         bool hasLookTarget, float seconds, std::string& error) {
  error.clear();
  if (!std::isfinite(seconds) || seconds <= 0 ||
      !std::isfinite(command.zoomCurrent) || !std::isfinite(command.zoomTarget) ||
      !std::isfinite(command.zoomSpeed) || !std::isfinite(command.moveDistance) ||
      !std::isfinite(command.minDistance) || !std::isfinite(command.maxDistance) ||
      !std::isfinite(command.torque) || command.minDistance > command.maxDistance) {
    error = "camera control: invalid frame or command";
    return false;
  }
  auto next = command;
  auto sampled = control;
  sampled.localForce = {};
  sampled.localTorque = {};
  if (control.freeCamera) {
    if (!hasController) {
      error = "camera control: free camera controller unavailable";
      return false;
    }
    for (const size_t axis : {size_t{1}, size_t{2}, size_t{3}, size_t{4}, size_t{5},
                              size_t{6}, size_t{7}, size_t{8}, size_t{10}, size_t{11}})
      if (!std::isfinite(axes[axis])) {
        error = "camera control: invalid controller axis";
        return false;
      }
    sampled.localForce = {axes[4] * -2.0f, axes[5] * 2.0f,
                          axes[6] * -2.0f};
    sampled.localTorque = {axes[2] * 0.5f, axes[1] * 0.5f,
                           axes[3] * 0.5f};
    sampled.localTorque[0] -= axes[8] * 20.0f;
    sampled.localTorque[1] -= axes[7] * 20.0f;
    sampled.localForce[2] = axes[10] * 1000.0f + sampled.localForce[2];
    next.zoomTarget = (std::clamp(axes[11] * 0.5f, -0.1f, 0.1f) + 1.0f) * next.zoomTarget;
    next.zoomTarget = std::clamp(next.zoomTarget, 1.0f, 120.0f);
    for (auto& value : sampled.localForce)
      value *= 3.0f;
    const float angularScale = 0.001f / seconds;
    for (auto& value : sampled.localTorque)
      value *= angularScale;
  } else if (hasController) {
    if (!std::isfinite(axes[7]) || !std::isfinite(axes[8]) || !std::isfinite(axes[10])) {
      error = "camera control: invalid controller axis";
      return false;
    }
    const float horizontal = axes[7] * -400.0f;
    const float vertical = axes[8] * 400.0f;
    sampled.localForce = {horizontal, vertical, 0};
    const float squared = horizontal * horizontal + vertical * vertical;
    if (squared > 100.0f) {
      const float factor = 10.0f / std::sqrt(squared);
      for (auto& value : sampled.localForce)
        value *= factor;
    }
    if (hasLookTarget) {
      sampled.localTorque[0] = (vertical * -0.01f) * command.torque;
      sampled.localTorque[1] = (horizontal * 0.01f) * command.torque;
    }
    next.moveDistance = ((axes[10] * 100.0f) * seconds + 1.0f) * command.moveDistance;
    if (next.moveDistance < command.minDistance)
      next.moveDistance = command.minDistance;
    else if (next.moveDistance > command.maxDistance)
      next.moveDistance = command.maxDistance;
  }
  // All control branches converge here, including a detached controller.
  next.zoomCurrent = (next.zoomTarget - next.zoomCurrent) * next.zoomSpeed * seconds + next.zoomCurrent;
  const float fovScale = next.zoomCurrent * 0.011111111380159855f;
  for (auto& value : sampled.localTorque)
    value *= fovScale;
  const auto finite = [](const auto& values) {
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
  };
  if (!std::isfinite(next.moveDistance) || !std::isfinite(next.zoomCurrent) ||
      !finite(sampled.localForce) || !finite(sampled.localTorque)) {
    error = "camera control: nonfinite callback result";
    return false;
  }
  command = next;
  control = sampled;
  return true;
}

bool cameraControlHasForce(const CameraControlState& control) {
  const auto squared = [](const std::array<float, 3>& value) {
    return value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
  };
  return squared(control.localForce) > 1.0000001111620804e-06f ||
         squared(control.localTorque) > 9.999999439624929e-11f;
}
