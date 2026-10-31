#include "physics_compound_step.hpp"
#include "physics_compound.hpp"
#include <algorithm>
#include <functional>

namespace physics {
namespace {
bool validTree(const std::vector<Body *> &bodies) {
  if (bodies.empty() || !bodies[0])
    return false;
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    const Body *b = bodies[i];
    if (!b || b->current > 1 ||
        std::find(bodies.begin(), bodies.begin() + i, b) != bodies.begin() + i)
      return false;
    if (i == 0) {
      if (b->attachment || (b->flags & kBodyChild))
        return false;
    } else if (!(b->flags & kBodyChild) || !b->attachment ||
               std::find(bodies.begin(), bodies.begin() + i,
                         b->attachment->parent) == bodies.begin() + i)
      return false;
    bool hasChild = false;
    for (std::size_t j = 1; j < bodies.size(); ++j)
      if (bodies[j] && bodies[j]->attachment &&
          bodies[j]->attachment->parent == b)
        hasChild = true;
    if (hasChild != bool(b->flags & kBodyHasChildren))
      return false;
  }
  return true;
}
void sleepTree(const std::vector<Body *> &bodies, Body *b) {
  if (b->flags & kBodyAsleep)
    return;
  sleepBodyState(*b);
  for (std::size_t i = 1; i < bodies.size(); ++i)
    if (bodies[i]->attachment->parent == b)
      sleepTree(bodies, bodies[i]);
}
void wakeTree(const std::vector<Body *> &bodies, Body *b) {
  if (!(b->flags & kBodyAsleep))
    return;
  b->flags &= ~kBodyAsleep;
  if (!(b->flags & kBodyChild))
    b->activity = {0, 100000, 0};
  for (std::size_t i = 1; i < bodies.size(); ++i)
    if (bodies[i]->attachment->parent == b)
      wakeTree(bodies, bodies[i]);
}
} // namespace
bool sleepDetachedCompound(const std::vector<Body *> &bodies) {
  if (!validTree(bodies))
    return false;
  sleepTree(bodies, bodies[0]);
  return true;
}
bool wakeDetachedCompound(const std::vector<Body *> &bodies) {
  if (!validTree(bodies))
    return false;
  wakeTree(bodies, bodies[0]);
  return true;
}
bool predictCompound(const std::vector<Body *> &bodies, float dt) {
  if (bodies.empty() || !bodies[0])
    return false;
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    const Body *b = bodies[i];
    if (!b || !b->proto || b->current > 1 || (b->flags & kBodyBit10) ||
        std::find(bodies.begin(), bodies.begin() + i, b) != bodies.begin() + i)
      return false;
    if (i == 0) {
      if (!(b->flags & kBodyActive) || (b->flags & kBodyAsleep) ||
          b->attachment || (b->flags & kBodyChild) ||
          (b->proto->flags & kProtoStatic) || b->proto->invMass == 0 ||
          (b->state[b->current ^ 1].stepFlags & 1))
        return false;
    } else {
      if (!(b->flags & kBodyChild) || !b->attachment ||
          !b->attachment->parent ||
          std::find(bodies.begin(), bodies.begin() + i,
                    b->attachment->parent) == bodies.begin() + i)
        return false;
    }
    bool hasChild = false;
    for (std::size_t j = 1; j < bodies.size(); ++j)
      if (bodies[j] && bodies[j]->attachment &&
          bodies[j]->attachment->parent == b)
        hasChild = true;
    if (hasChild != bool(b->flags & kBodyHasChildren))
      return false;
  }
  Body &root = *bodies[0];
  State &predicted = root.state[root.current ^ 1];
  if (!integrate(*root.proto, root.flags, root.state[root.current], predicted,
                 dt))
    return false;
  predicted.stepFlags = (predicted.stepFlags & ~2u) | 1u;
  std::function<void(Body *)> predict = [&](Body *parent) {
    for (std::size_t i = 1; i < bodies.size(); ++i) {
      Body &child = *bodies[i];
      if (child.attachment->parent != parent)
        continue;
      if (!(child.flags & kBodyActive) || (child.flags & kBodyAsleep))
        continue;
      const State &p = parent->state[parent->current ^ 1];
      State &out = child.state[child.current ^ 1];
      predictChildPose(p, child.attachment->relative, out);
      Vec3 point = child.attachment->relative.position;
      Body *ancestor = parent;
      while (ancestor != &root) {
        const auto &rel = ancestor->attachment->relative;
        point = matrixTransformRows(rel.rotation(), point);
        for (std::size_t k = 0; k < 3; ++k)
          point[k] += rel.position[k];
        ancestor = ancestor->attachment->parent;
      }
      const Vec3 offset = matrixTransformRows(predicted.rot, point);
      const Vec3 &w = predicted.omega;
      out.vel = {offset[2] * w[1] - offset[1] * w[2] + predicted.vel[0],
                 offset[0] * w[2] - offset[2] * w[0] + predicted.vel[1],
                 offset[1] * w[0] - offset[0] * w[1] + predicted.vel[2]};
      out.omega = p.omega;
      out.stepFlags = (out.stepFlags & ~2u) | 1u;
      predict(&child);
    }
  };
  predict(&root);
  return true;
}

CompoundStepResult commitCompound(const std::vector<Body *> &bodies) {
  if (!validTree(bodies))
    return CompoundStepResult::Unsupported;
  for (const auto *body : bodies)
    if (!body->proto || (body->flags & kBodyBit10))
      return CompoundStepResult::Unsupported;
  if (!(bodies[0]->flags & kBodyActive) || (bodies[0]->flags & kBodyAsleep) ||
      !(bodies[0]->state[bodies[0]->current ^ 1].stepFlags & 1))
    return CompoundStepResult::Unsupported;
  Body &root = *bodies[0];
  if ((root.proto->flags & kProtoStatic) || root.proto->invMass == 0)
    return CompoundStepResult::Unsupported;
  const auto result = commit(root);
  if (result == CommitResult::WentToSleep) {
    sleepTree(bodies, &root);
    return CompoundStepResult::Slept;
  }
  if (result != CommitResult::Flipped) {
    return CompoundStepResult::Unsupported;
  }
  std::function<void(Body *)> commitChildren = [&](Body *parent) {
    for (std::size_t i = 1; i < bodies.size(); ++i) {
      Body &child = *bodies[i];
      if (child.attachment->parent != parent)
        continue;
      if (!(child.flags & kBodyActive))
        continue;
      commit(child);
      commitChildren(&child);
    }
  };
  commitChildren(&root);
  return CompoundStepResult::Stepped;
}

CompoundStepResult stepCompound(const std::vector<Body *> &bodies, float dt) {
  std::vector<Body> backup;
  backup.reserve(bodies.size());
  for (const Body *b : bodies) {
    if (!b)
      return CompoundStepResult::Unsupported;
    backup.push_back(*b);
  }
  if (!predictCompound(bodies, dt))
    return CompoundStepResult::Unsupported;
  const auto result = commitCompound(bodies);
  if (result == CompoundStepResult::Unsupported)
    for (std::size_t i = 0; i < bodies.size(); ++i)
      *bodies[i] = backup[i];
  return result;
}
} // namespace physics
