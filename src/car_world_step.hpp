#pragma once

#include "game_spatial_payload.hpp"
#include "world_model.hpp"
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

class ResourceWorld;

// The frame step of a world that contains a car: stock 004a5190 for the
// registered car trees and armed camera follows, with the car's per-frame
// work supplied through GameWorld::stepRegisteredCars hooks.
//
// Collision traversal covers the single static ground the car stands in (the
// passive spatial tree of its sector, 004a1560 -> 004a0aa0 -> 0049fa60). Other
// dynamic roots and other spatial roots are not modeled; a world that has any
// other dynamic body is refused instead of stepped.
class CarWorldStepper {
public:
  CarWorldStepper(GameWorld &world, ResourceWorld &resources)
      : world_(world), resources_(resources) {}
  bool step(float seconds, std::string &error);

  // Test/debug hook: number of retained body/ground-face pairs. The drive
  // path only builds wheel-contact witnesses unless a leaf body (e.g. the
  // chassis bottoming out) touches the ground, so a fixture asserts this to
  // prove the pair collector path ran.
  size_t debugPairCount() const { return pairs_.size(); }

private:
  GameWorld &world_;
  ResourceWorld &resources_;
  // Last frame's wheel contact witnesses, consumed by the next frame's tire
  // step (stock selects a contact in traversal and uses it in the callback).
  std::map<const physics::Body *, std::unique_ptr<WheelGroundContact>>
      witnesses_;
  // Previous player axes: 0044e690 wakes a sleeping car when one changes.
  std::optional<CarControlSnapshot> lastControls_;
  uint64_t tireSteps_ = 0, tireRejects_ = 0;
  // Step history (see runlog trace channel "cartrace"): one structured line
  // per step, tick-numbered, with wall time and delta-time variance.
  uint64_t tick_ = 0;
  double lastWall_ = 0;
  unsigned anomalyDumps_ = 0;
  // Body/face pairs persist across gathers and frames so their impulse and
  // feature accumulators survive (004a2d10 keeps the pair until it drops).
  struct PairKey {
    const physics::Body *body;
    const LinkOwner *leaf;
    size_t face;
    bool operator<(const PairKey &o) const {
      return std::tie(body, leaf, face) < std::tie(o.body, o.leaf, o.face);
    }
  };
  std::map<PairKey, std::unique_ptr<CarGroundFacePair>> pairs_;
  // Pairs offered to the corrector in the current physics step (transactional:
  // applied only when the step succeeds, so a failed step retries with the
  // pairs still state 6).
  // Transactional collector state: applied after a successful step, restored
  // on failure (stepMultiRootWorld's rollback covers bodies/world only).
  std::set<PairKey> pendingErase_; // pairs scheduled for removal this step
  std::map<PairKey, std::array<float, 3>>
      accumulatorSnapshot_; // normalImpulse, frictionForce, correctedOnce
  std::vector<CarGroundFacePair *> lastPairs_; // collector order of the gather
};
