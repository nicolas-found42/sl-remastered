#pragma once
#include "physics_body.hpp"

namespace physics {
// Runs 004a3bc0 followed by 004a4ed0 for a tree with an awake active root.
// The caller supplies every member, root first; pointers must remain stable.
// Inactive and sleeping descendants follow the stock per-pass gates. World
// forces, contacts, joints and bit-0x10 bounding roots are not handled here.
enum class CompoundStepResult { Stepped, Unsupported, Slept };
// These operations require a detached tree with no world callbacks, contacts,
// joints, or world-list membership. Invalid trees are left unchanged.
bool sleepDetachedCompound(const std::vector<Body *> &bodies);
bool wakeDetachedCompound(const std::vector<Body *> &bodies);
// Same bounded tree as stepCompound, split so a caller can predict all roots
// before committing any. Neither function performs world forces or contacts.
bool predictCompound(const std::vector<Body *> &bodies, float dt);
CompoundStepResult commitCompound(const std::vector<Body *> &bodies);
CompoundStepResult stepCompound(const std::vector<Body *> &bodies, float dt);
} // namespace physics
