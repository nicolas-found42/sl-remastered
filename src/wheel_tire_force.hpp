#pragma once
#include "physics_body.hpp"
#include <optional>

struct TireEffectiveLoadInput {
  float suspensionLoad, maxLoad, smoothing;
  physics::Vec3 localNormal;
};
// 00454500: caller supplies the actual loaded contact normal and wheel data.
std::optional<float> tireEffectiveLoad(const TireEffectiveLoadInput& input);

// 00454500/004a66b0: point is wheel-body-local; force is in the wheel
// body's local frame. The caller must have resolved whether stock's two
// geometry-dependent force corrections apply; they are not modeled here.
enum class TireGeometryCorrections { None, RequiredOrUnknown };
bool routeTireForce(physics::Body& wheelBody, const physics::Vec3& localForce,
                    const physics::Vec3& localPoint, TireGeometryCorrections corrections);
