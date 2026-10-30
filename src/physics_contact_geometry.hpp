#pragma once
#include "physics_friction.hpp"
#include <functional>
#include <optional>
namespace physics {
// The non-overlapping branch of 004a2d10, after closest-core evaluation.
// The search overload below also handles close and overlapping contacts.
enum class ContactGeometryResult { Updated, Dropped, Unsupported };
ContactGeometryResult updateDirectContactGeometry(DirectContact& contact, const CorePair& pair,
                                                   const State& predictedA, const State& predictedB,
                                                   float radiusA, float radiusB, Vec3& worldPoint);
// 004a8410 interpolation used by the search; body.current must be 0 or 1.
State interpolateContactPose(const Body& body, float fraction);
// Evaluates intermediate poses during the stock ten-step search. Return nullopt
// when the underlying closest-core query cannot evaluate the supplied poses.
using ContactCoreQuery = std::function<std::optional<CorePair>(const State&, const State&)>;
struct ContactGeometryWorld { uint32_t queries = 0; uint32_t stepReady = 0; };
ContactGeometryResult updateDirectContactGeometryWithSearch(DirectContact& contact,
    const CorePair& predictedPair, float radiusA, float radiusB, Vec3& worldPoint,
    ContactGeometryWorld& world, const ContactCoreQuery& query);
}
