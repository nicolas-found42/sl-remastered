#pragma once

namespace physics {

// Fields written by the stock wheel's 00491f80 configuration routine.
struct SuspensionSettings {
  float force;       // +0x44
  float damping;     // +0x48
  float restLength;  // +0x34
  float maxLength;   // +0x38
  float minLength;   // +0x3c
  float auxiliaryLength; // +0x50
  float auxiliaryLimit;  // +0x40
  float reboundDamping;  // +0x4c
};

SuspensionSettings configureSuspension(float force, float damping, float restLength,
                                       float maxLength, float minLength,
                                       float reboundDamping, float auxiliaryLength);

// Scalar part of 00491100 after its geometry and velocity projections have
// been calculated. distanceSquared is the stored +0x9c, not a fresh distance.
// The caller must still perform the original geometry, body gates and force
// application; this function does not update the bodies.
struct SuspensionScalarInput {
  SuspensionSettings settings;
  float distanceSquared;
  float previousLength;
  float projectedVelocity; // stock +0xe0
  float accumulatedForce;  // stock +0x78
};
struct SuspensionScalarResult {
  float length;
  float nextDistanceSquared;
  float force;
  float dampingWork;
  bool active;
};
SuspensionScalarResult suspensionScalar(const SuspensionScalarInput& input);

} // namespace physics

#include "physics_body.hpp"

namespace physics {

// Caller-supplied surface record for an already resolved stock contact feature.
// The material word is retained as data; its interpretation is not assumed.
struct SuspensionContactSurface {
  uint32_t featureIndex = 0;
  uint32_t materialWord = 0;
};

// Bounded 00491100 update for awake bodies, including attached bodies.
// Sleeping/list handling and a minimum stop without a usable time step
// return Unsupported before changing either body.
struct SuspensionUpdate {
  SuspensionSettings settings;
  Body* wheelBody = nullptr;
  Body* contactBody = nullptr;
  Vec3 anchor{};       // +0x54, wheel-body local point
  Vec3 contactPoint{}; // +0xa0, contact-body local point
  Vec3 normal{};       // +0xac, world direction
  Vec3 lateralForce{}; // +0x6c, wheel-body local force
  float forcePointScale = 0; // +0x84
  float forcePointThreshold = 0; // +0x88
  float forcePointOffset = 0; // +0x8c
  float timeStep = 0; // world +0x0c; required when the minimum stop engages
  float accumulatedForce = 0; // +0x78
  float distanceSquared = 0; // +0x9c
  float length = 0;    // +0xd8
  float force = 0;     // +0xdc
  float projectedVelocity = 0; // +0xe0
  float dampingWork = 0; // +0xe4
  Vec3 wheelPoint{};   // +0xc0
  Vec3 normalForce{};  // +0xcc
  std::shared_ptr<const BodyProto> contactPrototype;
  std::shared_ptr<Body> contactOwner;
  std::shared_ptr<const SuspensionContactSurface> contactSurface;
  uint32_t contactFeature = 0;
};

struct SuspensionContactCandidate {
  Body* a = nullptr;
  Body* b = nullptr;
  Vec3 pointA{}, pointB{}, normal{};
  // The non-wheel body must be independently owned and have this prototype.
  std::shared_ptr<const BodyProto> otherPrototype;
  std::shared_ptr<Body> otherOwner;
  std::shared_ptr<const SuspensionContactSurface> surface;
};
enum class SuspensionContactResult { Selected, Rejected, Unsupported };
SuspensionContactResult selectSuspensionContact(SuspensionUpdate& suspension,
                                                const SuspensionContactCandidate& candidate);
enum class SuspensionUpdateResult { Unsupported, Inactive, Active };
SuspensionUpdateResult updateSuspension(SuspensionUpdate& suspension);

} // namespace physics
