#pragma once
#include "camera_follow_step.hpp"
#include "physics_body.hpp"
#include "physics_friction.hpp"
#include "physics_suspension.hpp"
#include <functional>
#include <string>
#include <vector>

namespace physics {
// Each tree lists the root first and each child after its parent. Work that is
// not represented by this detached unit must be explicitly absent.
struct MultiRootWork {
  bool collisionFreeProven = false;
  bool contacts = false;
  bool joints = false;
  bool wheelConstraints = false;
  bool callbacks = false;
  bool waterOrClipPlanes = false;
  // 0049b830 keeps one pending contact per root body: while a root (resolved
  // through its parents) already has a contact in the current pass, a later
  // candidate replaces it only if its separation is smaller (a tie goes to a
  // position dot test that is not ported). 004a35d0 then corrects the
  // survivors and clears the pending marks, and the next pass detects the
  // remaining pairs against the re-predicted bodies. Contacts that lose are
  // left uncorrected this pass.
  bool onePendingContactPerRoot = false;
  // Called after prediction. It must enumerate all relevant non-wheel contacts,
  // select wheel candidates into suspensions, and return false if coverage is
  // incomplete. Contact bodies must be registered or immutable static roots.
  // The callback may update suspension records and invalidate registered root
  // predictions; it must keep bodies and prototypes alive through the step.
  // Any other collector state is managed transactionally by the caller.
  std::function<bool(std::vector<DirectContact> &, std::string &)>
      gatherContacts;
  // Receives corrected contact records in collector order so retained pairs
  // preserve their impulse/force accumulators between iterations.
  std::function<void(const std::vector<DirectContact> &)> finishContacts;
  // 00426f60 phase after world forces and callbacks, before prediction.
  // Caller owns rollback of any callback state outside registered bodies.
  std::function<bool(std::string &)> beforePrediction;
  std::vector<SuspensionUpdate *> suspensions;
  std::vector<CameraFollowStep> cameraCallbacks;
};
using BodyTree = std::vector<Body *>;
// Conservative registered-root pairs after the 004a1920 body/shape gates.
// Pairs may still be rejected by resource bounds, filters or narrowphase.
// Only supplied registrations are covered; this is not world clearance.
struct DynamicRootPair {
  Body *first;
  Body *second;
};
bool discoverDynamicRootPairs(const World &world,
                              const std::vector<BodyTree> &roots,
                              std::vector<DynamicRootPair> &pairs,
                              std::string &error);
// Conservative leaf candidates using 0049c8f0 sphere/oriented-bound rejection
// and child recursion, including ordinary wheel candidates. Body flag 0x800
// remains unsupported; flag 0x20 and Empty/Hull shapes are excluded by gates.
// Leaves still require swept/closest-core narrowphase and caller-owned retention;
// candidate discovery alone does not establish clearance.
bool discoverDynamicLeafPairs(const BodyTree &first, const BodyTree &second,
                              std::vector<DynamicRootPair> &pairs,
                              std::string &error);
// Exposes the global prediction phase for testing and future world scheduling.
// All trees remain uncommitted. On failure no body changes.
bool predictRootSet(const std::vector<BodyTree> &roots, float dt,
                    std::string &error);
// Runs the bounded world pass. A collector is required unless clearance is
// independently proven. Unsupported work leaves registered state unchanged.
FreeBodyStepResult stepMultiRootWorld(World &world,
                                      const std::vector<BodyTree> &roots,
                                      float dt, const MultiRootWork &work,
                                      std::string &error);
} // namespace physics
