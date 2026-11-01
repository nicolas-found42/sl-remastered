#include "physics_multi_root_step.hpp"
#include "physics_compound_step.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace physics {
namespace {
bool validate(const std::vector<BodyTree>& roots, std::string& error) {
  std::vector<const Body*> seen;
  for (const auto& tree : roots) {
    if (tree.empty()) {
      error = "multi-root: empty tree";
      return false;
    }
    for (size_t i = 0; i < tree.size(); ++i) {
      const auto* b = tree[i];
      if (!b || !b->proto || b->current > 1 || (b->flags & kBodyBit10) ||
          std::find(seen.begin(), seen.end(), b) != seen.end()) {
        error = "multi-root: unsupported or duplicate body";
        return false;
      }
      if (i == 0) {
        const char* why = nullptr;
        if (!(b->flags & kBodyActive))
          why = "inactive";
        else if (b->proto->flags & kProtoStatic)
          why = "static prototype";
        else if (b->attachment)
          why = "attached";
        else if (b->flags & kBodyChild)
          why = "child flag";
        else if (!std::isfinite(b->proto->invMass) || b->proto->invMass <= 0)
          why = "no mass";
        if (why) {
          error = std::string("multi-root: invalid root (") + why +
                  ", body flags 0x" + [&] {
                    char buffer[16];
                    std::snprintf(buffer, sizeof buffer, "%x", b->flags);
                    return std::string(buffer);
                  }() +
                  ", tree size " + std::to_string(tree.size()) + ")";
          return false;
        }
      } else if (!(b->flags & kBodyChild) || !b->attachment ||
                 std::find(tree.begin(), tree.begin() + i,
                           b->attachment->parent) == tree.begin() + i) {
        error = "multi-root: invalid child";
        return false;
      }
      bool hasChild = false;
      for (size_t j = i + 1; j < tree.size(); ++j)
        if (tree[j] && tree[j]->attachment && tree[j]->attachment->parent == b)
          hasChild = true;
      if (hasChild != bool(b->flags & kBodyHasChildren)) {
        error = "multi-root: incomplete tree";
        return false;
      }
      seen.push_back(b);
    }
  }
  return true;
}
struct Snapshot {
  std::vector<Body*> pointers;
  std::vector<Body> bodies;
  explicit Snapshot(const std::vector<BodyTree>& roots) {
    for (const auto& tree : roots)
      for (auto* body : tree) {
        pointers.push_back(body);
        bodies.push_back(*body);
      }
  }
  void restore() const {
    for (size_t i = 0; i < pointers.size(); ++i)
      *pointers[i] = bodies[i];
  }
};
} // namespace
bool discoverDynamicRootPairs(const World& world,
                              const std::vector<BodyTree>& roots,
                              std::vector<DynamicRootPair>& pairs,
                              std::string& error) {
  if (roots.size() != world.bodies.size()) {
    error = "dynamic pairs: registration mismatch";
    return false;
  }
  for (size_t i = 0; i < roots.size(); ++i) {
    if (roots[i].empty() || !roots[i][0] || roots[i][0] != world.bodies[i] ||
        !roots[i][0]->proto || roots[i][0]->current > 1 ||
        roots[i][0]->attachment || (roots[i][0]->flags & kBodyChild) ||
        std::find(world.bodies.begin(), world.bodies.begin() + i,
                  roots[i][0]) != world.bodies.begin() + i) {
      error = "dynamic pairs: invalid registration";
      return false;
    }
  }
  const auto eligible = [](const Body& body) {
    return !(body.flags & 0x20u) &&
           body.proto->shape.type != ShapeType::Empty &&
           !(body.state[body.current ^ 1].stepFlags & 2u);
  };
  std::vector<DynamicRootPair> found;
  for (size_t i = 0; i < roots.size(); ++i) {
    Body* first = roots[i][0];
    if (!eligible(*first))
      continue;
    for (size_t j = i + 1; j < roots.size(); ++j) {
      Body* second = roots[j][0];
      if (eligible(*second))
        found.push_back({first, second});
    }
  }
  pairs = std::move(found);
  error.clear();
  return true;
}
bool discoverDynamicLeafPairs(const BodyTree& first, const BodyTree& second,
                              std::vector<DynamicRootPair>& pairs,
                              std::string& error) {
  const std::vector<BodyTree> trees{first, second};
  if (!validate(trees, error))
    return false;
  std::vector<DynamicRootPair> found;
  const auto usable = [](const Body& body) {
    return !(body.flags & 0x20u) &&
           body.proto->shape.type != ShapeType::Empty &&
           body.proto->shape.type != ShapeType::Hull;
  };
  const auto validBounds = [](const Body& body) {
    const auto& shape = body.proto->shape;
    const auto& state = body.state[body.current ^ 1];
    if (!std::isfinite(shape.boundRadius) || shape.boundRadius < 0)
      return false;
    for (size_t i = 0; i < 3; ++i)
      if (!std::isfinite(shape.extent[i]) || shape.extent[i] < 0 ||
          !std::isfinite(state.pos[i]))
        return false;
    for (float value : state.rot)
      if (!std::isfinite(value))
        return false;
    for (size_t i = 0; i < 3; ++i)
      for (size_t j = 0; j < 3; ++j) {
        float dot = 0;
        for (size_t k = 0; k < 3; ++k)
          dot += state.rot[4 * k + i] * state.rot[4 * k + j];
        if (std::fabs(dot - (i == j ? 1.0f : 0.0f)) > 0.0001f)
          return false;
      }
    return true;
  };
  std::function<bool(Body*, Body*)> visit = [&](Body* a, Body* b) {
    if (!usable(*a) || !usable(*b))
      return true;
    if ((a->flags | b->flags) & 0x800u) {
      error = "dynamic pairs: swept body unsupported";
      return false;
    }
    if (!validBounds(*a) || !validBounds(*b)) {
      error = "dynamic pairs: invalid predicted bounds";
      return false;
    }
    const auto& as = a->state[a->current ^ 1];
    const auto& bs = b->state[b->current ^ 1];
    const auto& ap = a->proto->shape;
    const auto& bp = b->proto->shape;
    const Vec3 delta{as.pos[0] - bs.pos[0], as.pos[1] - bs.pos[1],
                     as.pos[2] - bs.pos[2]};
    const float radius = ap.boundRadius + bp.boundRadius + k::skin;
    const float distance2 =
        delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
    if (!std::isfinite(radius) || !std::isfinite(distance2)) {
      error = "dynamic pairs: invalid bound distance";
      return false;
    }
    // Mesh extents can be deferred until the stock core is built. In that
    // case retain the pair rather than using potentially incomplete bounds.
    if (ap.type != ShapeType::Mesh && bp.type != ShapeType::Mesh) {
      if (radius * radius < distance2)
        return true;
      for (size_t axis = 0; axis < 3; ++axis) {
        const float ad = delta[0] * as.rot[axis] + delta[1] * as.rot[4 + axis] +
                         delta[2] * as.rot[8 + axis];
        const float bd = delta[0] * bs.rot[axis] + delta[1] * bs.rot[4 + axis] +
                         delta[2] * bs.rot[8 + axis];
        if (bp.boundRadius < std::fabs(ad) - ap.extent[axis] ||
            ap.boundRadius < std::fabs(bd) - bp.extent[axis])
          return true;
      }
    }
    if ((a->flags | b->flags) & (0x40u | 0x400u | 0x10000u | 0x20000u)) {
      error = "dynamic pairs: special collision path unsupported";
      return false;
    }
    bool childrenA = false, childrenB = false;
    for (Body* child : first)
      if (child->attachment && child->attachment->parent == a)
        childrenA = true;
    for (Body* child : second)
      if (child->attachment && child->attachment->parent == b)
        childrenB = true;
    if (childrenA) {
      for (Body* child : first)
        if (child->attachment && child->attachment->parent == a &&
            !visit(child, b))
          return false;
    } else if (childrenB) {
      for (Body* child : second)
        if (child->attachment && child->attachment->parent == b &&
            !visit(a, child))
          return false;
    } else
      found.push_back({a, b});
    return true;
  };
  if (!visit(first[0], second[0]))
    return false;
  pairs = std::move(found);
  error.clear();
  return true;
}
bool predictRootSet(const std::vector<BodyTree>& roots, float dt,
                    std::string& error) {
  error.clear();
  if (!std::isfinite(dt) || dt <= 0 || !validate(roots, error)) {
    if (error.empty())
      error = "multi-root: invalid prediction time";
    return false;
  }
  Snapshot snapshot(roots);
  for (const auto& tree : roots) {
    if (tree[0]->flags & kBodyAsleep)
      continue;
    const bool ok =
        tree.size() == 1 ? stepBody(*tree[0], dt) : predictCompound(tree, dt);
    if (!ok) {
      snapshot.restore();
      error = "multi-root: prediction unsupported";
      return false;
    }
  }
  return true;
}
FreeBodyStepResult stepMultiRootWorld(World& world,
                                      const std::vector<BodyTree>& roots,
                                      float dt, const MultiRootWork& work,
                                      std::string& error) {
  error.clear();
  const auto fail = [&](const char* why) {
    error = why;
    return FreeBodyStepResult::Unsupported;
  };
  if ((!work.collisionFreeProven && !work.gatherContacts) ||
      (work.contacts && !work.gatherContacts) || work.joints ||
      (work.wheelConstraints && work.suspensions.empty()) || work.callbacks ||
      work.waterOrClipPlanes)
    return fail("multi-root: physics work unsupported");
  if (!std::isfinite(dt) || dt <= 0 || !std::isfinite(world.pending) ||
      world.pending < 0 || !std::isfinite(world.time) ||
      !std::isfinite(dt + world.pending) ||
      !std::isfinite(world.params.airDensity) ||
      !std::all_of(
          world.params.gravity.begin(), world.params.gravity.end(),
          [](float v) { return std::isfinite(v); }))
    return fail("multi-root: invalid world time or forces");
  if (!validate(roots, error))
    return FreeBodyStepResult::Unsupported;
  if (roots.size() != world.bodies.size())
    return fail("multi-root: registration mismatch");
  for (size_t i = 0; i < roots.size(); ++i)
    if (roots[i][0] != world.bodies[i])
      return fail("multi-root: registration mismatch");
  const auto isRoot = [&](const Body* body) {
    return body &&
           std::any_of(roots.begin(), roots.end(),
                       [&](const BodyTree& tree) { return tree[0] == body; });
  };
  const auto isRegistered = [&](const Body* body) {
    for (const auto& tree : roots)
      if (std::find(tree.begin(), tree.end(), body) != tree.end())
        return true;
    return false;
  };
  const auto usableContactBody = [&](const Body* body) {
    return body && body->proto &&
           (isRegistered(body) ||
            (!body->attachment && !(body->flags & kBodyChild) &&
             (body->proto->flags & kProtoStatic)));
  };
  for (size_t i = 0; i < work.suspensions.size(); ++i) {
    const auto* s = work.suspensions[i];
    if (!s || !isRegistered(s->wheelBody) ||
        (s->contactBody && !usableContactBody(s->contactBody)) ||
        std::find(work.suspensions.begin(), work.suspensions.begin() + i, s) !=
            work.suspensions.begin() + i)
      return fail("multi-root: unsupported suspension");
  }
  // A follow's look/move targets are only read (pose copies below), never
  // stepped: stock's camera force (00438b60) runs while the world step
  // skips sleeping trees in every pass, reading their resting pose. A
  // follow may therefore target an asleep body that is not a root -- e.g.
  // the chase camera of a sleeping car beside awake scene objects.
  const auto followTargetUsable = [&](const Body* body) {
    return isRoot(body) || (body && (body->flags & kBodyAsleep) != 0);
  };
  for (const auto& cb : work.cameraCallbacks) {
    if (!isRoot(cb.camera) || !followTargetUsable(cb.look) ||
        !followTargetUsable(cb.move) || !cb.smoothing ||
        !cb.input.otherBranchesInactive)
      return fail("multi-root: unsupported camera callback");
  }
  const float total = dt + world.pending;
  if (total < k::minimumStep) {
    world.pending = total;
    return FreeBodyStepResult::Accumulated;
  }
  if (!std::isfinite(world.time + total))
    return fail("multi-root: time overflow");
  Snapshot snapshot(roots);
  const World saved = world;
  std::vector<CameraFollowSmoothing> smoothing;
  for (const auto& cb : work.cameraCallbacks)
    smoothing.push_back(*cb.smoothing);
  std::vector<SuspensionUpdate> suspensions;
  for (const auto* s : work.suspensions)
    suspensions.push_back(*s);
  const auto rollback = [&] {
    snapshot.restore();
    world = saved;
    for (size_t i = 0; i < work.cameraCallbacks.size(); ++i)
      *work.cameraCallbacks[i].smoothing = smoothing[i];
    for (size_t i = 0; i < work.suspensions.size(); ++i)
      *work.suspensions[i] = suspensions[i];
  };
  world.pending = 0;
  world.lastStep = total;
  for (const auto& tree : roots)
    for (auto* body : tree) {
      // 004a3f90 stops at any inactive, asleep or static ancestor.
      bool reachable = true;
      for (const Body* p = body; p;
           p = p->attachment ? p->attachment->parent : nullptr)
        if (!(p->flags & kBodyActive) || (p->flags & kBodyAsleep) ||
            (p->proto->flags & kProtoStatic)) {
          reachable = false;
          break;
        }
      if (reachable && !accumulateWorldForces(world.params, *body)) {
        rollback();
        return fail("multi-root: force pass unsupported");
      }
    }
  for (const auto& cb : work.cameraCallbacks) {
    auto input = cb.input;
    input.camera = cb.camera->state[cb.camera->current];
    input.lookTarget = cb.look->state[cb.look->current];
    input.moveTarget = cb.move->state[cb.move->current];
    const auto follow = cameraFollowForce(input, *cb.smoothing);
    if (!follow) {
      rollback();
      return fail("multi-root: camera callback unsupported");
    }
    *cb.smoothing = follow->smoothing;
    addForce(*cb.camera, follow->force, nullptr);
    auto& state = cb.camera->state[cb.camera->current];
    for (size_t i = 0; i < 3; ++i)
      state.torque[i] += follow->torque[i];
  }
  // FUN_004a5190 calls the +0xc slot on world+0x9c before driving
  // callbacks and prediction. The suspension table at 005f0fcc maps that
  // slot to FUN_00491100; world+0xb8 contains different constraint objects.
  // Newly selected wheel contacts feed the next step.
  for (auto* s : work.suspensions) {
    s->timeStep = total;
    // Preserve the adapter's existing first-contact gate. Stock registration
    // and initially airborne suspension lifecycle still need corroboration.
    if (!s->contactBody)
      continue;
    if (updateSuspension(*s) == SuspensionUpdateResult::Unsupported) {
      rollback();
      return fail("multi-root: suspension update unsupported");
    }
  }
  if (work.beforePrediction && !work.beforePrediction(error)) {
    rollback();
    if (error.empty())
      error = "multi-root: pre-prediction callback unsupported";
    return FreeBodyStepResult::Unsupported;
  }
  if (!predictRootSet(roots, total, error)) {
    rollback();
    return FreeBodyStepResult::Unsupported;
  }
  if (work.gatherContacts) {
    DirectContactWorld contactWorld{0, total};
    for (unsigned iteration = 0; iteration < 20; ++iteration) {
      std::vector<DirectContact> contacts;
      // 004a5190 pass order: FUN_004a3900 solves the +0xb8 rows BEFORE
      // 004a2d10 wakes bodies (all.c 121076-121087). The collector's update
      // (the 004a2d10 port) clears the predicted-state flag inside
      // gatherContacts, so remember the pre-gather flags here and restore
      // them for the row solve; the wakes are re-applied below, before the
      // +0x10 direct corrections that gate on the predicted flags.
      std::vector<std::pair<Body*, uint32_t>> predictionFlags;
      for (const auto& tree : roots)
        for (auto* body : tree)
          predictionFlags.push_back(
              {body, body->state[body->current ^ 1].stepFlags});
      if (!work.gatherContacts(contacts, error)) {
        rollback();
        if (error.empty())
          error = "multi-root: incomplete collision traversal";
        return FreeBodyStepResult::Unsupported;
      }
      std::vector<Body*> woken;
      for (const auto& entry : predictionFlags) {
        Body* body = entry.first;
        if ((entry.second & 1u) != 0 &&
            (body->state[body->current ^ 1].stepFlags & 1u) == 0) {
          woken.push_back(body);
          body->state[body->current ^ 1].stepFlags = entry.second;
        }
      }
      for (const auto* s : work.suspensions) {
        if (!isRegistered(s->wheelBody) ||
            (s->contactBody && !usableContactBody(s->contactBody))) {
          rollback();
          return fail("multi-root: unsupported selected suspension contact");
        }
      }
      for (auto& contact : contacts) {
        contact.processed = false;
        contact.correctionResult = 0;
      }
      bool corrected = false;
      // Stock FUN_004a5190 repeats the outer loop only while FUN_004a2540/
      // FUN_004a35d0 admit new pending impacts; retained resting rows that
      // keep correcting do not extend it.
      bool admittedNew = false;
      // Contacts that may be corrected in this pass (indexes into `contacts`).
      std::vector<bool> allowed(contacts.size(), true);
      if (work.onePendingContactPerRoot) {
        const auto rootOf = [](const Body* body) {
          while (body && body->attachment && body->attachment->parent)
            body = body->attachment->parent;
          return body;
        };
        std::fill(allowed.begin(), allowed.end(), false);
        // Smallest separation first (0049b830 param_12 = the core distance
        // minus radii): a pending contact is displaced only by a smaller one.
        // Collector order breaks exact ties; stock uses a normal/position dot
        // test there, which is not ported. A contact is taken only if neither
        // of its dynamic roots already has one.
        std::vector<size_t> order(contacts.size());
        for (size_t i = 0; i < order.size(); ++i)
          order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t l, size_t r) {
          return contacts[l].separation < contacts[r].separation;
        });
        std::set<const Body*> used;
        for (const size_t i : order) {
          // State 7 resting constraints do not own the one-pending-impact
          // mark (FUN_004a35d0 only consumes state 6).
          if (contacts[i].resting) {
            allowed[i] = true;
            continue;
          }
          // Static roots (the ground) never hold a pending mark: 0049b830
          // checks the dynamic body's root only.
          const auto dynamicRoot = [&](const Body* body) -> const Body* {
            const Body* r = rootOf(body);
            return r && r->proto && !(r->proto->flags & kProtoStatic) ? r
                                                                      : nullptr;
          };
          const Body* first = dynamicRoot(contacts[i].a);
          const Body* second = dynamicRoot(contacts[i].b);
          if ((first && used.count(first)) || (second && used.count(second)))
            continue;
          if (first)
            used.insert(first);
          if (second)
            used.insert(second);
          allowed[i] = true;
        }
      }
      // FUN_004a3900: compute all resting RHS/diagonal results before
      // applying any force. Pending impacts (004a35d0) follow this batch.
      for (const auto& contact : contacts) {
        if (!usableContactBody(contact.a) || !usableContactBody(contact.b)) {
          rollback();
          return fail("multi-root: unsupported contact body");
        }
      }
      if (solveRestingContacts(contacts, contactWorld) < 0) {
        rollback();
        return fail("multi-root: resting contact unsupported");
      }
      // 004a2d10 wake band, applied after the row solve as in 004a5190: the
      // predicted-state flags drop here, not before FUN_004a3900.
      for (Body* body : woken)
        body->state[body->current ^ 1].stepFlags &= ~1u;
      // 004a5190 (all.c 121090-121110): when 004a2d10 cleared the settled
      // flag, the +0x10 slot (00496fe0 -> 00496050) sweeps every retained
      // record (5 < state < 8). Resting records receive the drive support
      // ((0.03-sep)*10 force when the pair is not both predicted) and the
      // positional shift; impact records are left to the 004a35d0 stage
      // below. Stock contact records never enter the world+0xb8 row list,
      // so FUN_004a3900 applies nothing for them in this adapter.
      bool swept = false;
      if (!woken.empty()) {
        for (auto& contact : contacts) {
          if (!contact.resting)
            continue;
          if (!usableContactBody(contact.a) || !usableContactBody(contact.b)) {
            rollback();
            return fail("multi-root: unsupported contact body");
          }
          const int result = correctDirectContact(contact, contactWorld);
          if (result < 0) {
            rollback();
            return fail("multi-root: contact correction unsupported");
          }
          contact.processed = true;
          contact.correctionResult = result;
          swept |= result != 0;
        }
      }
      // FUN_004a5190 (LAB_004a53cb, all.c 121096-121147): the pending stage
      // (004a2540 discovery, 004a35d0 state-6 correction) runs only while the
      // settled flag is still set -- 004a2d10 woke nothing, or it woke
      // bodies but the +0x10 sweep re-settled (sum == 0). When the sweep
      // corrected something the pass jumps straight to re-prediction; the
      // count-19 escape (004a53c5) forces the pending stage on the last
      // pass. 004a35d0 admitting a new impact is the only result that
      // clears the flag and extends the loop.
      const bool runPending =
          woken.empty() || !swept || iteration == 18;
      for (unsigned stage = 1; stage < 2 && runPending; ++stage) {
        // The shipped count-19 escape (004a53c5 -> 004a53cb) still
        // discovers new impacts even when retained rows keep correcting.
        if (stage != 0 && corrected && iteration != 18)
          break;
        for (size_t contactIndex = 0; contactIndex < contacts.size();
             ++contactIndex) {
          auto& contact = contacts[contactIndex];
          if (contact.resting != (stage == 0) || contact.constraintRow ||
              !allowed[contactIndex])
            continue;
          if (!usableContactBody(contact.a) || !usableContactBody(contact.b)) {
            rollback();
            return fail("multi-root: unsupported contact body");
          }
          contact.processed = true;
          const int result = correctDirectContact(contact, contactWorld);
          if (result < 0) {
            rollback();
            return fail("multi-root: contact correction unsupported");
          }
          contact.correctionResult = result;
          corrected |= result != 0;
          if (stage != 0)
            admittedNew |= result != 0;
          contact.resting = true;
        }
      }
      if (work.finishContacts)
        work.finishContacts(contacts);
      // 004a3bc0 predicts only invalidated roots after each solve pass.
      size_t snapshotIndex = 0;
      for (const auto& tree : roots) {
        const bool rootWasAsleep = snapshot.bodies[snapshotIndex].flags & kBodyAsleep;
        snapshotIndex += tree.size();
        Body& root = *tree[0];
        if (root.flags & kBodyAsleep)
          continue;
        // FUN_004a7990 recursively wakes descendants. The direct corrector
        // wakes the root; complete its registered tree before prediction.
        if (rootWasAsleep)
          for (Body* member : tree) member->flags &= ~kBodyAsleep;
        if (root.state[root.current ^ 1].stepFlags & 1u)
          continue;
        const bool ok = tree.size() == 1 ? stepBody(root, total)
                                         : predictCompound(tree, total);
        if (!ok) {
          rollback();
          return fail("multi-root: repeat prediction unsupported");
        }
      }
      // 004a5190 leaves the loop when the settled flag is set at pass end:
      // 004a2d10 woke nothing this pass, or it woke bodies but the +0x10
      // sweep corrected nothing (all.c 121090-121098, 121141-121147).
      // 004a2540/004a35d0 admitting new pending impacts clears the flag.
      if ((woken.empty() || !swept) && !admittedNew)
        break;
    }
  } else {
    // Suspension forces can invalidate prediction even without contact work.
    for (const auto& tree : roots) {
      Body& root = *tree[0];
      if (root.flags & kBodyAsleep)
        continue;
      if (root.state[root.current ^ 1].stepFlags & 1u)
        continue;
      const bool ok = tree.size() == 1 ? stepBody(root, total)
                                       : predictCompound(tree, total);
      if (!ok) {
        rollback();
        return fail("multi-root: repeat prediction unsupported");
      }
    }
  }
  world.stepsDone += static_cast<uint32_t>(std::count_if(roots.begin(), roots.end(),
                                                         [](const BodyTree& tree) { return !(tree[0]->flags & kBodyAsleep); }));
  for (const auto& tree : roots) {
    if (tree[0]->flags & kBodyAsleep)
      continue;
    if (tree.size() == 1) {
      const auto status = commit(*tree[0]);
      if (status == CommitResult::WentToSleep)
        sleepBodyState(*tree[0]);
      else if (status != CommitResult::Flipped) {
        rollback();
        return fail("multi-root: commit unsupported");
      }
    } else if (commitCompound(tree) == CompoundStepResult::Unsupported) {
      rollback();
      return fail("multi-root: compound commit unsupported");
    }
  }
  world.time += total;
  return FreeBodyStepResult::Advanced;
}
} // namespace physics
