#pragma once
#include "physics_friction.hpp"
#include "physics_polyhedron.hpp"
#include <functional>
#include <optional>
namespace physics {
// The non-overlapping branch of 004a2d10, after closest-core evaluation.
// The search overload below also handles close and overlapping contacts.
enum class ContactGeometryResult { Updated, Dropped, Unsupported };
ContactGeometryResult
updateDirectContactGeometry(DirectContact &contact, const CorePair &pair,
                            const State &predictedA, const State &predictedB,
                            float radiusA, float radiusB, Vec3 &worldPoint);
// 004a8410 interpolation used by the search; body.current must be 0 or 1.
State interpolateContactPose(const Body &body, float fraction);
// Evaluates intermediate/current poses for retained pairs (004a2d10), including
// the current-pose recheck when the ten-step search cannot separate them.
// This is not the new-pair creation/rejection path in 0049c8f0. Return nullopt
// when the underlying closest-core query cannot evaluate the supplied poses.
using ContactCoreQuery =
    std::function<std::optional<CorePair>(const State &, const State &)>;
struct ContactGeometryWorld {
  uint32_t queries = 0;
  uint32_t stepReady = 0;
};
ContactGeometryResult updateDirectContactGeometryWithSearch(
    DirectContact &contact, const CorePair &predictedPair, float radiusA,
    float radiusB, Vec3 &worldPoint, ContactGeometryWorld &world,
    const ContactCoreQuery &query);
// A retained 0049f4d0 body/face pair. The caller owns both bodies and their
// prototypes, keeps them alive, and removes the pair when the face unloads.
// This represents one pair only, not collision traversal or clearance.
struct GroundFacePair {
  CollisionCore triangle;
  Vec3 faceNormal{}; // stored PHYS triangle normal; not a separating-normal constraint
  std::optional<std::size_t> bodyFeature, triangleFeature;
  DirectContact contact;
};
bool createGroundFacePair(Body &body, Body &ground,
                          const std::array<Vec3, 3> &vertices,
                          const Vec3 &normal, GroundFacePair &out,
                          std::string &error);
// 0049ec00: search a creation-ordered candidate list against one static ground
// body. Candidates own closest-feature caches independently of admitted contact
// rows. Success preserves all queried caches, including when no row is selected;
// Unsupported leaves every candidate unchanged. selected names the one proposed
// row, whose contact carries the time-fraction priority and world point.
ContactGeometryResult selectGroundFaceCandidate(
    const std::vector<GroundFacePair*>& candidates,
    ContactGeometryWorld& world, std::optional<std::size_t>& selected);
// Legacy alignment utility, retained for callers that explicitly request face
// alignment. This is not the Stock retained-contact normal rule: 004a2d10
// consumes the separating normal directly, without this 45-degree gate.
std::optional<Vec3> groundFaceContactNormal(const Vec3 &coreNormal,
                                            const Vec3 &faceNormal);
// Updates the tracked pair through the 004a2d10 closest-core/search path.
// A Dropped pair must be unregistered by the caller; Unsupported is not a miss.
ContactGeometryResult updateGroundFacePair(GroundFacePair &pair,
                                           ContactGeometryWorld &world,
                                           Vec3 &worldPoint);
// Retained dynamic leaf pair; caller retains both bodies/prototypes and owns
// rollback of this record. The special wheel/swept branches are unsupported.
struct DynamicBodyPair {
  std::optional<std::size_t> firstFeature, secondFeature;
  DirectContact contact;
};
bool createDynamicBodyPair(Body& first, Body& second, DynamicBodyPair& out,
                           std::string& error);
// Candidate/renewal query. Its feature cache can move; it does not establish
// that the candidate has become a 0049b830 admitted row.
ContactGeometryResult updateDynamicBodyPair(DynamicBodyPair& pair,
                                            ContactGeometryWorld& world,
                                            Vec3& worldPoint);
// Fixed 004af970 refresh of an admitted row (state 6 or 7), independently
// of contact.resting. Invalid fixed regions drop even with positive radii.
// Unported ordinary fixed feature combinations return Unsupported. Wheel and
// swept replacement cores keep their separately qualified specialized paths.
ContactGeometryResult updateRetainedDynamicBodyPair(DynamicBodyPair& pair,
                                                    ContactGeometryWorld& world,
                                                    Vec3& worldPoint);
} // namespace physics
