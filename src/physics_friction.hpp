#pragma once
#include "physics_body.hpp"
#include <cstdint>
#include <optional>
#include <vector>
namespace physics {
// Type 6 impact and type 7 resting contacts resolved to root bodies. Unsupported
// inputs are left untouched.
struct DirectContact {
  Body* a = nullptr;
  Body* b = nullptr;
  Vec3 normal{};
  float separation = 0;  // stock contact +0x8c
  Vec3 pointA{}, pointB{}; // local offsets, +0x5c and +0x68
  float friction = 0; // +0x94
  float bounce = 0; // +0x98
  float normalImpulse = 0; // +0xa0
  float frictionForce = 0; // +0xa8
  bool resting = false; // stock contact +0x14 == 7
  float normalForce = 0; // +0xa4, force (not impact impulse)
  bool constraintRow = false; // explicitly registered in world+0xb8, not contact list+0x118
  bool processed = false; // transient: selected by the pending-contact pass
  int correctionResult = 0; // transient: actual direct-corrector return in this pass
  // 0049b830 +0xac is the sampled time fraction, not separation. Older
  // collectors without a new-candidate search retain their separation order.
  std::optional<float> pendingPriority = std::nullopt;
  Vec3 pendingPoint{};    // +0x80: world point used by the exact-priority tie test
  float featureCross = 0; // +0x90: edge/edge cross product projected on normal
};
struct DirectContactWorld { uint32_t corrections = 0; float step = 0; }; // world +0x14, +0x0c
// -1: unsupported; otherwise the original routine's result (0, 1 or 2).
int solveRestingContacts(std::vector<DirectContact>& contacts, DirectContactWorld& world);
int correctDirectContact(DirectContact& contact, DirectContactWorld& world);
}
