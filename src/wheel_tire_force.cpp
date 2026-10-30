#include "wheel_tire_force.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

std::optional<float> tireEffectiveLoad(const TireEffectiveLoadInput& in) {
  const float values[] = {in.suspensionLoad, in.maxLoad, in.smoothing,
                          in.localNormal[0], in.localNormal[1], in.localNormal[2]};
  for (float value : values) if (!std::isfinite(value)) return std::nullopt;
  if (in.suspensionLoad < 0 || in.maxLoad <= 0) return std::nullopt;
  constexpr float halfPi = 0x1.921fb6p+0f; // 005f0cc4
  constexpr float twoOverPi = 0x1.45f306p-1f; // 005f110c
  const float slope = (std::sqrt(in.localNormal[1]*in.localNormal[1] +
                                 in.localNormal[2]*in.localNormal[2]) + 1.0f) * 0.5f;
  const float load = slope * ((1.0f-in.smoothing)*std::min(in.suspensionLoad,in.maxLoad) +
      std::atan(in.suspensionLoad*halfPi/in.maxLoad)*in.maxLoad*twoOverPi*in.smoothing);
  if (!std::isfinite(load)) return std::nullopt;
  return load;
}

bool routeTireForce(physics::Body& wheelBody, const physics::Vec3& localForce,
                    const physics::Vec3& localPoint, TireGeometryCorrections corrections) {
  using namespace physics;
  if (corrections != TireGeometryCorrections::None) return false;
  for (float v : localForce) if (!std::isfinite(v)) return false;
  for (float v : localPoint) if (!std::isfinite(v)) return false;
  Body* root = &wheelBody;
  Vec3 point = localPoint;
  std::unordered_set<Body*> seen;
  while (true) {
    if (!seen.insert(root).second || !root->proto || root->current > 1 ||
        bool(root->flags & kBodyChild) != bool(root->attachment)) return false;
    for (float v : root->state[root->current].rot) if (!std::isfinite(v)) return false;
    if (!root->attachment) break;
    const auto& attachment = *root->attachment;
    if (!attachment.parent) return false;
    for (float v : attachment.relative.rotation()) if (!std::isfinite(v)) return false;
    for (float v : attachment.relative.position) if (!std::isfinite(v)) return false;
    point = matrixTransformRows(attachment.relative.rotation(), point);
    for (int i = 0; i < 3; ++i) point[i] += attachment.relative.position[i];
    root = attachment.parent;
  }
  if ((root->flags & kBodyAsleep) || (root->proto->flags & kProtoStatic)) return false;
  const Vec3 world = matrixTransformRows(wheelBody.state[wheelBody.current].rot, localForce);
  const Mat34& rotation = root->state[root->current].rot;
  const Vec3 rootLocal{world[0]*rotation[0]+world[1]*rotation[4]+world[2]*rotation[8],
                       world[0]*rotation[1]+world[1]*rotation[5]+world[2]*rotation[9],
                       world[0]*rotation[2]+world[1]*rotation[6]+world[2]*rotation[10]};
  const Vec3 applied = matrixTransformRows(rotation, rootLocal);
  const Vec3 offset = matrixTransformRows(rotation, point);
  for (float v : applied) if (!std::isfinite(v)) return false;
  for (float v : offset) if (!std::isfinite(v)) return false;
  const Vec3 torque{offset[1]*applied[2]-offset[2]*applied[1],
                    offset[2]*applied[0]-offset[0]*applied[2],
                    offset[0]*applied[1]-offset[1]*applied[0]};
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(torque[i]) || !std::isfinite(root->state[root->current].force[i] + applied[i]) ||
        !std::isfinite(root->state[root->current].torque[i] + torque[i])) return false;
  }
  root->state[root->current ^ 1].stepFlags &= ~1u;
  addForce(*root, applied, &offset);
  return true;
}
