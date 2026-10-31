#pragma once
#include "physics_body.hpp"
#include <optional>

struct TireEffectiveLoadInput {
  float suspensionLoad, maxLoad, smoothing;
  physics::Vec3 localNormal;
};
// 00454500: caller supplies the actual loaded contact normal and wheel data.
std::optional<float> tireEffectiveLoad(const TireEffectiveLoadInput &input);

// 00454500/004a66b0: point and input force are wheel-body-local.
// Resolved applies the stock instant-center corrections in root coordinates;
// RequiredOrUnknown refuses to apply a force.
enum class TireGeometryCorrections { None, Resolved, RequiredOrUnknown };
// Coordinates in the car root's local frame, matching wheel record +0x78
// and +0x27c..+0x290 in 00454500.
struct TireForceGeometry {
  physics::Vec3 anchor, instantCenterA, instantCenterB;
  float radius, suspensionLength, maximumLength;
};
bool routeTireForce(physics::Body &wheelBody, const physics::Vec3 &localForce,
                    const physics::Vec3 &localPoint,
                    TireGeometryCorrections corrections,
                    const TireForceGeometry *geometry = nullptr);
