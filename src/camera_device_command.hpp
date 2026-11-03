#pragma once

#include "camera_device_config.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

struct CarDriver;
struct CarCameraSettings;

struct CameraCommandState {
  float minDistance = 2.5f;
  float maxDistance = 10.0f;
  float smoothPosition = 0.5f;
  float smoothRotation = 0.5f;
  float torque;
  float force;
  float damping;
  float perpendicularDamping;
  float roll = 0.0f;
  // +0xa8 height above the move target (0 = off) and +0xbc pitch limit; the
  // `height` command resets the limit to 0.3 (nonzero) or 1.3 (zero).
  float height = 0.0f;
  float maxPitch = 1.3f;
  std::array<float, 3> lookOffset{}, lookDirection{0, 0, -1};
  std::array<float, 3> moveOffset{}, moveRotation{};
  float moveDistance = 0;
  float zoomCurrent = 0, zoomTarget = 0, zoomSpeed = 3;
};

std::array<std::string, 3> seatCameraCommands(uint32_t target, const CarDriver& driver,
                                              const CarCameraSettings& settings);

// Per-frame controller outputs consumed by the camera physics callback.
// freeCamera is stock camera flag +0x10 bit0, independent of world stepping.
struct CameraControlState {
  bool freeCamera = false;
  std::array<float, 3> localForce{}, localTorque{};
};

// 2.3.1 0048a573: either manual accumulator crossing its threshold runs both.
bool cameraControlHasForce(const CameraControlState& control);

// Newer 004897b0 (older 004386a0). Axes are normalized player values;
// only axes read by the selected Stock branch must be supplied and finite.
// Refusal leaves command and control state unchanged.
bool updateCameraControl(CameraCommandState& command, CameraControlState& control,
                         const std::array<float, 12>& axes, bool hasController,
                         bool hasLookTarget, float seconds, std::string& error);

struct CameraFollowCommand {
  bool look = false;
  uint32_t target = 0;
  std::array<float, 3> offset{}, direction{};
  float value = 0;
};

bool parseCameraFollowCommand(std::string_view text, float currentTorque,
                              CameraFollowCommand& command);
void applyCameraFollowCommand(CameraCommandState& state, const CameraFollowCommand& command);

CameraCommandState cameraCommandState(const CameraDeviceConfig& config);
bool applyCameraCommand(CameraCommandState& state, std::string_view command);
