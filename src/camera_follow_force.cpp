#include "camera_follow_force.hpp"
#include <algorithm>
#include <cmath>

namespace {
using physics::Vec3;
Vec3 add(const Vec3 &a, const Vec3 &b) { return {a[0]+b[0], a[1]+b[1], a[2]+b[2]}; }
Vec3 sub(const Vec3 &a, const Vec3 &b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
Vec3 mul(const Vec3 &a, float s) { return {a[0]*s, a[1]*s, a[2]*s}; }
float dot(const Vec3 &a, const Vec3 &b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Vec3 cross(const Vec3 &a, const Vec3 &b) {
  return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
bool finite(const Vec3 &v) { return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); }); }
bool valid(const physics::State &s) {
  return finite(s.pos) && finite(s.vel) && finite(s.omega) &&
      std::all_of(s.rot.begin(), s.rot.end(), [](float x) { return std::isfinite(x); });
}
Vec3 pointVelocity(const physics::State &s, const Vec3 &offset) {
  return add(s.vel, cross(s.omega, physics::matrixTransformRows(s.rot, offset)));
}
Vec3 smooth(const Vec3 &old, const Vec3 &point, float factor, bool initialize) {
  return initialize ? point : add(old, mul(sub(point, old), factor));
}
}

std::optional<CameraFollowForceResult> cameraFollowForce(
    const CameraFollowForceInput &in, const CameraFollowSmoothing &previous,
    const char **reject) {
  if (reject) *reject = nullptr;
  const auto fail = [&](const char *code) {
    if (reject) *reject = code;
    return std::optional<CameraFollowForceResult>{};
  };
  const auto &c = in.command;
  if (!in.otherBranchesInactive) return fail("foth");
  if (!valid(in.camera)) return fail("fcam");
  if (!valid(in.lookTarget)) return fail("flok");
  if (!valid(in.moveTarget)) return fail("fmov");
  if (!finite(in.cameraLookOffset)) return fail("flof");
  if (!finite(c.lookOffset)) return fail("flko");
  if (!finite(c.lookDirection)) return fail("fldi");
  if (!finite(c.moveOffset)) return fail("fmof");
  if (!finite(c.moveRotation)) return fail("fmro");
  if (!finite(previous.look)) return fail("fslk");
  if (!finite(previous.move)) return fail("fsmv");
  if (!std::isfinite(in.fieldOfView)) return fail("ffov");
  if (!std::isfinite(in.maxForce) || in.maxForce <= 0) return fail("fmax");
  if (!std::isfinite(c.smoothPosition)) return fail("fsmp");
  if (!std::isfinite(c.smoothRotation)) return fail("fsro");
  if (!std::isfinite(c.torque)) return fail("ftrq");
  if (!std::isfinite(c.force)) return fail("ffrc");
  if (!std::isfinite(c.damping)) return fail("fdmp");
  if (!std::isfinite(c.perpendicularDamping)) return fail("fprp");
  if (!std::isfinite(c.moveDistance)) return fail("fdst");
  CameraFollowForceResult out;
  const auto &m = in.lookTarget.rot;
  // 00551c90's gimbal branch uses atan2(-m01,m00); normal branch atan2(m02,m22).
  const float yaw = (m[10] == 0 && m[2] == 0) ? std::atan2(-m[1], m[0]) : std::atan2(m[2], m[10]);
  const float s = std::sin(yaw), co = std::cos(yaw);
  const Vec3 yawOffset{co*c.lookOffset[0]+s*c.lookOffset[2], c.lookOffset[1],
                       -s*c.lookOffset[0]+co*c.lookOffset[2]};
  out.smoothing.look = smooth(previous.look, add(in.lookTarget.pos, yawOffset), c.smoothPosition, previous.initializeLook);
  out.smoothing.move = smooth(previous.move, physics::transformPoint(in.moveTarget.pos, in.moveTarget.rot, c.moveOffset), c.smoothRotation, previous.initializeMove);
  out.smoothing.initializeLook = false;
  out.smoothing.initializeMove = false;
  Vec3 look = sub(out.smoothing.look, physics::transformPoint(in.camera.pos, in.camera.rot, in.cameraLookOffset));
  const float lookDistance = std::sqrt(dot(look, look));
  look = add(look, mul(pointVelocity(in.lookTarget, c.lookOffset), lookDistance * in.fieldOfView * 0.000055555552535224706f));
  const float predictedLength = std::sqrt(dot(look, look));
  if (!(predictedLength > 0) || !std::isfinite(predictedLength)) return fail("flen");
  const Vec3 direction = mul(look, 1.0f / predictedLength);
  const Vec3 forward = physics::matrixTransformRows(in.camera.rot, c.lookDirection);
  const Vec3 torque = mul(cross(forward, direction), c.torque);
  if (dot(torque, torque) > 1.0e-8f) out.torque = torque;
  const Vec3 delta = sub(out.smoothing.move, physics::transformPoint(in.camera.pos, in.camera.rot, c.moveRotation));
  const float squared = dot(delta, delta);
  if (c.moveDistance > 0 && squared > 0.0001f) {
    const float distance = std::sqrt(squared);
    const Vec3 n = mul(delta, 1.0f / distance);
    const Vec3 relativeVelocity = sub(pointVelocity(in.moveTarget, c.moveOffset), pointVelocity(in.camera, c.moveRotation));
    Vec3 force = mul(n, (distance-c.moveDistance)*c.force + dot(relativeVelocity,n)*c.damping);
    const float magnitude = std::sqrt(dot(force,force));
    if (magnitude > in.maxForce) force = mul(force, in.maxForce/magnitude);
    force = add(force, mul(sub(in.camera.vel, mul(n, dot(in.camera.vel,n))), c.perpendicularDamping));
    if (dot(force,force) > 0.0001f) out.force = force;
  }
  if (!finite(out.force) || !finite(out.torque) || !finite(out.smoothing.look) || !finite(out.smoothing.move)) return fail("fout");
  return out;
}
