#include "camera_device_command.hpp"
#include "car_runtime.hpp"
#include <cmath>

int main() {
  const CameraDeviceConfig config{34.375f, 0.02f, 2.0f, 1.0f, -0.7f,
                                  0.2f, 200.0f, 20.0f, true};
  auto state = cameraCommandState(config);
  if (state.minDistance != 2.5f || state.maxDistance != 10.0f ||
      state.smoothPosition != 0.5f || state.smoothRotation != 0.5f ||
      state.torque != 0.02f || state.force != 2.0f || state.damping != 1.0f ||
      state.perpendicularDamping != -0.7f || state.roll != 0.0f) return 1;
  if (!applyCameraCommand(state, "dist 3.0 9.0") || state.minDistance != 3 || state.maxDistance != 9) return 2;
  if (!applyCameraCommand(state, "dist 2.5") || state.minDistance != 2.5f || state.maxDistance != 9) return 3;
  if (!applyCameraCommand(state, "smooth 0.5 0.5") ||
      !applyCameraCommand(state, "force 1.6 0.5 -0.7") ||
      !applyCameraCommand(state, "torque 0.1") || !applyCameraCommand(state, "roll 0")) return 4;
  if (std::fabs(state.force - 1.6f) > 1e-6f || state.damping != 0.5f ||
      std::fabs(state.perpendicularDamping + 0.7f) > 1e-6f ||
      std::fabs(state.torque - 0.1f) > 1e-6f || state.roll != 0) return 5;
  if (!applyCameraCommand(state, "roll -0.25") || std::fabs(state.roll + 0.25f) > 1e-6f) return 6;
  if (!applyCameraCommand(state, "roll +1") || std::fabs(state.roll + 0.25f) > 1e-6f) return 7;
  if (applyCameraCommand(state, "render 1") || applyCameraCommand(state, "unknown")) return 8;
  CameraFollowCommand follow;
  if (!parseCameraFollowCommand("look 0xffff0123 1,2,3 0,0,-2 -1", state.torque, follow) ||
      follow.target != 0xffff0123u || !follow.look) return 9;
  applyCameraFollowCommand(state, follow);
  if (state.lookOffset != std::array<float, 3>{1, 2, 3} ||
      state.lookDirection != std::array<float, 3>{0, 0, -1} ||
      std::fabs(state.torque - 0.1f) > 1e-6f) return 10;
  if (!parseCameraFollowCommand("move 23 4,5,6 12 7,8,9", state.torque, follow)) return 11;
  applyCameraFollowCommand(state, follow);
  if (follow.target != 23 || state.moveOffset != std::array<float, 3>{4, 5, 6} ||
      state.moveRotation != std::array<float, 3>{7, 8, 9} || state.moveDistance != 12 ||
      state.maxDistance != 12) return 12;
  if (!parseCameraFollowCommand("look 23 1,2", state.torque, follow) ||
      follow.offset != std::array<float, 3>{1, 2, 0} ||
      follow.direction != std::array<float, 3>{0, 0, -1}) return 13;
  CarDriver driver;
  driver.hasExtCamera = true;
  driver.extCamera = {120, 0, 1.8f, 0, 0, 1, -2, 0, 0, 0, 5};
  const auto commands = seatCameraCommands(0xffff0123u, driver, {0.4f, 1.25f});
  if (commands[0] != "look 0xffff0123 0.000000,1.800000,0.000000 0,0,-1 -1.000000" ||
      commands[1] != "move 0xffff0123 0.000000,1.000000,-2.000000 2.000000 0.000000,0.000000,0.000000" ||
      commands[2] != "zoom 150.000000") return 14;
  const float current = state.zoomCurrent;
  if (!applyCameraCommand(state, commands[2]) || state.zoomTarget != 120 || state.zoomCurrent != current) return 15;
  return 0;
}
