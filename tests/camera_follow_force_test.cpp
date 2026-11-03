#include "camera_follow_force.hpp"
#include <cmath>

static bool close(float a, float b) { return std::fabs(a - b) < 1e-5f; }
static bool close(const physics::Vec3 &a, const physics::Vec3 &b) {
  return close(a[0], b[0]) && close(a[1], b[1]) && close(a[2], b[2]);
}
int main() {
  CameraFollowForceInput in;
  in.otherBranchesInactive = true;
  in.command.torque = 1;
  in.command.force = 2;
  in.command.damping = 1;
  in.command.perpendicularDamping = -0.5f;
  in.command.moveDistance = 2;
  in.maxForce = 3;
  in.lookTarget.pos = {10, 0, 0};
  in.moveTarget.pos = {10, 0, 0};
  in.moveTarget.vel = {1, 0, 0};
  in.camera.vel = {0, 2, 0};
  const auto first = cameraFollowForce(in, {});
  if (!first || !close(first->torque, {0,-1,0}) || !close(first->force, {3,-1,0}) ||
      first->smoothing.initializeLook || first->smoothing.initializeMove) return 1;
  in.moveTarget.pos = {14,0,0};
  in.lookTarget.pos = {14,0,0};
  const auto second = cameraFollowForce(in, first->smoothing);
  if (!second || !close(second->smoothing.look, {12,0,0}) ||
      !close(second->smoothing.move, {12,0,0})) return 2;
  // Yaw pi/2, pitch pi/4: direct physics look ignores pitch for its point.
  const float h = std::sqrt(0.5f);
  in.lookTarget.rot = {0,h,h,0, 0,h,-h,0, -1,0,0,0};
  in.lookTarget.pos = {0,0,10};
  in.command.lookOffset = {1,2,3};
  const auto yaw = cameraFollowForce(in, {});
  if (!yaw || !close(yaw->smoothing.look, {3,2,9})) return 3;
  // Nonzero target angular velocity is evaluated at the full rotated offset,
  // even though its look position uses yaw only.
  in.lookTarget.omega = {0,1,0};
  in.fieldOfView = 90;
  const auto moving = cameraFollowForce(in, {});
  const float scale = std::sqrt(94.0f) * 90.0f / 18000.0f;
  const float x = 3.0f - scale, z = 9.0f - 5.0f*h*scale;
  const float length = std::sqrt(x*x + 4.0f + z*z);
  if (!moving || !close(moving->torque, {2.0f / length, -x / length, 0})) return 4;
  in.otherBranchesInactive = false;
  if (cameraFollowForce(in, {})) return 5;
  in.otherBranchesInactive = true;
  in.lookTarget.pos = {0,0,0};
  in.command.lookOffset = {0,0,0};
  if (cameraFollowForce(in, {})) return 6;
  in.lookTarget.pos = {1,0,0};
  in.moveTarget.pos = {0.001f,0,0};
  in.command.moveOffset = {};
  const auto near = cameraFollowForce(in, {});
  if (!near || !close(near->force, {0,0,0})) return 7;
  // Identity camera: local axes equal world axes, so the body-state terms
  // land directly on world components. Alignment torque is zero because the
  // camera sits on the -z look axis of its (0,0,-1) look direction.
  CameraFollowForceInput body;
  body.otherBranchesInactive = true;
  body.maxForce = 3;
  body.camera.pos = {0, 0, 5};
  body.camera.rot = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  body.lookTarget.pos = {0, 0, 0};
  body.moveTarget.pos = {0, 0, 0};
  body.command.torque = 1;
  body.command.force = 2;
  body.command.damping = 1;
  // Roll damper: rollRate = w[2] = 1, roll = 0, c.roll = 0 ->
  // (1*0.03 + 0) * -0.3 = -0.009 about local z.
  body.camera.omega = {0, 0, 1};
  const auto rollDamper = cameraFollowForce(body, {});
  if (!rollDamper || !close(rollDamper->torque, {0, 0, -0.009f})) return 8;
  // Roll spring: zero rate, commanded roll 0.2 -> (0 - 0.2) * -0.3 = +0.06.
  body.camera.omega = {0, 0, 0};
  body.command.roll = 0.2f;
  const auto rollSpring = cameraFollowForce(body, {});
  if (!rollSpring || !close(rollSpring->torque, {0, 0, 0.06f})) return 9;
  // Pitch limit: predicted = 0.1*w[0] + pitch(0); maxPitch 0.1.
  // w[0] = 2 -> predicted 0.2 exceeds the limit: (0.2 - 0.1) * -0.1 = -0.01.
  body.command.roll = 0.0f;
  body.command.maxPitch = 0.1f;
  body.camera.omega = {2, 0, 0};
  const auto pitchHi = cameraFollowForce(body, {});
  if (!pitchHi || !close(pitchHi->torque, {-0.01f, 0, 0})) return 10;
  // Negative side: w[0] = -2 -> limit -0.1: (-0.2 - -0.1) * -0.1 = +0.01.
  body.camera.omega = {-2, 0, 0};
  const auto pitchLo = cameraFollowForce(body, {});
  if (!pitchLo || !close(pitchLo->torque, {0.01f, 0, 0})) return 11;
  // Exactly at the limit is not clamped: w[0] = 1 -> predicted 0.1 == maxPitch.
  body.camera.omega = {1, 0, 0};
  const auto pitchEdge = cameraFollowForce(body, {});
  if (!pitchEdge || !close(pitchEdge->torque, {0, 0, 0})) return 12;
  // Height spring toward moveTarget.y + height: dy = 0 + 2 - 0 = 2 ->
  // force.y += 2*30 - 2*vel[1] = 60.
  body.camera.omega = {0, 0, 0};
  body.command.maxPitch = 1.3f;
  body.command.height = 2.0f;
  const auto heightUp = cameraFollowForce(body, {});
  if (!heightUp || !close(heightUp->force, {0, 60, 0})) return 13;
  // Upward velocity damps the spring: 60 - 2*10 = 40.
  body.camera.vel = {0, 10, 0};
  const auto heightDamped = cameraFollowForce(body, {});
  if (!heightDamped || !close(heightDamped->force, {0, 40, 0})) return 14;
  // Camera above the target height pulls down: dy = 0 + 2 - 5 = -3 -> -90.
  body.camera.vel = {0, 0, 0};
  body.camera.pos = {0, 5, 5};
  const auto heightDown = cameraFollowForce(body, {});
  if (!heightDown || !close(heightDown->force, {0, -90, 0})) return 15;
  body.camera.pos = {0, 0, 5};
  body.camera.vel = {};
  body.camera.omega = {};
  body.command.height = 0;
  body.control.localForce = {2, -3, 4};
  body.control.localTorque = {0.2f, 0.3f, 0.4f};
  const auto manual = cameraFollowForce(body, {});
  if (!manual || !close(manual->force, {2, -3, 4}) ||
      !close(manual->torque, {0.2f, 0.3f, 0.4f}))
    return 16;
  // 2.3.1 00504e70/00505090 rotate manual forces and torques from camera
  // local axes. The target stays aligned with the turned camera's forward.
  body.camera.rot = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0};
  body.lookTarget.pos = {-5, 0, 5};
  const auto turned = cameraFollowForce(body, {});
  if (!turned || !close(turned->force, {4, -3, -2}) ||
      !close(turned->torque, {0.4f, 0.3f, -0.2f}))
    return 17;
  body.control.freeCamera = true;
  body.hasLookTarget = false;
  body.hasMoveTarget = false;
  body.camera.rot = physics::State{}.rot;
  body.camera.vel = {1, 2, 3};
  body.camera.omega = {};
  body.command.maxPitch = 0;
  const auto free = cameraFollowForce(body, {});
  if (!free || !close(free->force, {1, -5, 1}) ||
      !close(free->torque, {0.2f, 0.3f, 0.4f}) ||
      !free->smoothing.initializeLook || !free->smoothing.initializeMove)
    return 18;
  return 0;
}
