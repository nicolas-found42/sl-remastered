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
  // FUN_0043a4b0: `height X` stores X, then maxPitch = 1.3 when X == 0 else 0.3.
  if (!applyCameraCommand(state, "height 0") || state.height != 0.0f ||
      std::fabs(state.maxPitch - 1.3f) > 1e-6f) return 16;
  if (!applyCameraCommand(state, "height 1.75") || std::fabs(state.height - 1.75f) > 1e-6f ||
      std::fabs(state.maxPitch - 0.3f) > 1e-6f) return 17;
  if (!applyCameraCommand(state, "maxpitch 0.65") || std::fabs(state.maxPitch - 0.65f) > 1e-6f) return 18;
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
  {
    // 2.3.1 004897b0: follow-mode controller axes7/8 are scaled before
    // normalization; the torque uses the original scaled values.
    auto command = cameraCommandState(CameraDeviceConfig{55, 0.02f, 2, 1, -0.7f,
                                                          0.2f, 200, 20, true});
    command.moveDistance = 4;
    command.zoomTarget = 100;
    CameraControlState control;
    std::array<float, 12> axes{};
    axes[7] = 0.025f;
    axes[8] = 0.025f;
    axes[10] = 0.5f;
    std::string error;
    if (!updateCameraControl(command, control, axes, true, true, 0.02f, error) ||
        std::fabs(control.localForce[0] + 7.071068f) > 0.00001f ||
        std::fabs(control.localForce[1] - 7.071068f) > 0.00001f ||
        control.localForce[2] != 0 || command.moveDistance != 8 ||
        std::fabs(command.zoomCurrent - 57.7f) > 0.00001f ||
        std::fabs(control.localTorque[0] + 0.0012822223f) > 0.000001f ||
        std::fabs(control.localTorque[1] + 0.0012822223f) > 0.000001f)
      return 19;
    // Detached control clears the manual outputs while zoom continues.
    if (!updateCameraControl(command, control, {}, false, true, 0.02f, error) ||
        control.localForce != std::array<float, 3>{} ||
        control.localTorque != std::array<float, 3>{} ||
        command.moveDistance != 8 ||
        std::fabs(command.zoomCurrent - 60.238f) > 0.00001f)
      return 20;
  }
  {
    auto command = cameraCommandState(CameraDeviceConfig{55, 0.02f, 2, 1, -0.7f,
                                                          0.2f, 200, 20, true});
    command.zoomTarget = 100;
    CameraControlState control;
    control.freeCamera = true;
    std::array<float, 12> axes{};
    axes[1] = 0.4f;
    axes[2] = 0.2f;
    axes[3] = 0.6f;
    axes[4] = 0.5f;
    axes[5] = -0.5f;
    axes[6] = -0.25f;
    axes[7] = 0.01f;
    axes[8] = -0.02f;
    axes[10] = 0.003f;
    axes[11] = 1;
    std::string error;
    if (!updateCameraControl(command, control, axes, true, false, 0.02f, error) ||
        command.zoomTarget != 110 ||
        std::fabs(command.zoomCurrent - 58.3f) > 0.00001f ||
        control.localForce != std::array<float, 3>{-3, -3, 10.5f} ||
        std::fabs(control.localTorque[0] - 0.016194445f) > 0.000001f ||
        std::fabs(control.localTorque[1]) > 0.000001f ||
        std::fabs(control.localTorque[2] - 0.009716667f) > 0.000001f)
      return 21;
  }
  return 0;
}
