#pragma once
#include "physics_body.hpp"

namespace physics {
// Runs 004a3bc0 followed by 004a4ed0 for one awake tree. The caller supplies
// every member, root first, in sibling order; pointers must remain stable.
// World forces, contacts, joints and the bit-0x10 bounding-root path are not
// handled here. On rejection no body is changed.
enum class CompoundStepResult { Stepped, Unsupported, Slept };
// These operations require a detached tree with no world callbacks, contacts,
// joints, or world-list membership. Invalid trees are left unchanged.
bool sleepDetachedCompound(const std::vector<Body*>& bodies);
bool wakeDetachedCompound(const std::vector<Body*>& bodies);
// Same bounded tree as stepCompound, split so a caller can predict all roots
// before committing any. Neither function performs world forces or contacts.
bool predictCompound(const std::vector<Body*>& bodies, float dt);
CompoundStepResult commitCompound(const std::vector<Body*>& bodies);
CompoundStepResult stepCompound(const std::vector<Body*>& bodies, float dt);
}
