#include "camera_follow_force.hpp"
#include <cmath>

static bool close(float a, float b) { return std::fabs(a-b) < 1e-5f; }
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
  if (!moving || !close(moving->torque, {2.0f/length, -x/length, 0})) return 4;
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
  return 0;
}
