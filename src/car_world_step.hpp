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
struct VmMeasurementState;

// The frame step of a world that contains a car: stock 004a5190 for the
// registered car trees and armed camera follows, with the car's per-frame
// work supplied through GameWorld::stepRegisteredCars hooks.
//
// Collision traversal covers the static ground the stepped roots stand in
// (the passive spatial tree of the body's sector, 004a1560 -> 004a0aa0 ->
// 0049fa60) plus body/body contacts between the stepped roots through the
// 0049b830 pending-contact path. Awake dynamic scene objects (cones, barrels,
// trash cans) are stepped roots like the cars: stock 004a5190 walks every
// registered root. Other unregistered dynamic bodies are still refused.
class CarWorldStepper {
public:
  CarWorldStepper(GameWorld &world, ResourceWorld &resources,
                  const VmMeasurementState *measurement = nullptr)
      : world_(world), resources_(resources), measurement_(measurement) {}
  bool step(float seconds, std::string &error);

  // Test/debug hook: number of retained body/ground-face pairs. The drive
  // path only builds wheel-contact witnesses unless a leaf body (e.g. the
  // chassis bottoming out) touches the ground, so a fixture asserts this to
  // prove the pair collector path ran.
  size_t debugPairCount() const { return pairs_.size(); }
  // Retained object body/ground-face pairs (settled cones, barrels, ...).
  size_t debugObjectPairCount() const { return objectPairs_.size(); }
  // Retained body/body contacts between stepped objects and cars/objects.
  size_t debugDynamicPairCount() const { return dynamicPairs_.size(); }

private:
  GameWorld &world_;
  ResourceWorld &resources_;
  const VmMeasurementState *measurement_;
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
    size_t bodyFeature = static_cast<size_t>(-1);
    size_t triangleFeature = static_cast<size_t>(-1);
    uint64_t serial = 0;
    bool operator<(const PairKey &o) const {
      return std::tie(body, leaf, face, bodyFeature, triangleFeature, serial) <
             std::tie(o.body, o.leaf, o.face, o.bodyFeature, o.triangleFeature, o.serial);
    }
  };
  std::map<PairKey, std::unique_ptr<CarGroundFacePair>> pairs_;
  // 00497280 closest-feature caches and 0049b830 admitted rows have separate
  // lifetimes. More than one feature pair can be admitted for one triangle.
  struct ObjectGroundFacePair {
    std::shared_ptr<SpatialGroundFace> face;
    physics::GroundFacePair pair;
    const physics::BodyProto *prototype = nullptr;
    bool correctedOnce = false;
    uint64_t serial = 0;
    bool valid() const {
      return face && face->valid() && pair.contact.a &&
             pair.contact.a->proto == prototype;
    }
  };
  std::map<PairKey, std::unique_ptr<ObjectGroundFacePair>> objectPairs_;
  std::map<PairKey, std::unique_ptr<ObjectGroundFacePair>> objectCandidates_;
  uint64_t nextObjectSerial_ = 0;
  // Retained body/body contact between a stepped object body and another
  // stepped root's body (stock queues these as 0049b830 pending records).
  // Wheel and swept replacement segments use the dynamic narrow phase.
  struct RetainedDynamicPair {
    physics::DynamicBodyPair pair;
    const physics::BodyProto *firstPrototype = nullptr;
    const physics::BodyProto *secondPrototype = nullptr;
  };
  struct DynamicPairKey {
    const physics::Body *first;
    const physics::Body *second;
    bool operator<(const DynamicPairKey &o) const {
      return std::tie(first, second) < std::tie(o.first, o.second);
    }
  };
  std::map<DynamicPairKey, std::unique_ptr<RetainedDynamicPair>> dynamicPairs_;
  // 0049c8f0 sphere/oriented-bound rejection plus 0049b830 pending-record
  // retention for one object body against one body of another stepped root.
  // Returns false (with why) on an unsupported correction path. Remaining
  // special body flags are reported explicitly rather than offered as boxes.
  bool considerDynamicPair(physics::Body *objectBody, physics::Body *other,
                           physics::ContactGeometryWorld &geometryWorld,
                           std::vector<physics::DirectContact> &contacts,
                           std::set<DynamicPairKey> &seen,
                           std::set<DynamicPairKey> &pendingErase,
                           std::string &why);
  // Pairs offered to the corrector in the current physics step (transactional:
  // applied only when the step succeeds, so a failed step retries with the
  // pairs still state 6).
  // Transactional collector state: applied after a successful step, restored
  // on failure (stepMultiRootWorld's rollback covers bodies/world only).
  std::set<PairKey> pendingErase_; // pairs scheduled for removal this step
  std::set<PairKey> pendingObjectErase_;
  std::set<DynamicPairKey> pendingDynamicErase_;
  std::map<PairKey, std::array<float, 5>>
      accumulatorSnapshot_; // normalImpulse, frictionForce, correctedOnce, normalForce, resting
  std::map<DynamicPairKey, std::array<float, 4>>
      dynamicAccumulatorSnapshot_; // normalImpulse, frictionForce, normalForce, resting
  // One entry per offered contact in collector order: the write-back target
  // for finishContacts (done[i] maps to entry i).
  struct OfferedContact {
    CarGroundFacePair *carPair = nullptr;
    ObjectGroundFacePair *objectPair = nullptr;
    RetainedDynamicPair *dynamicPair = nullptr;
};
  std::vector<OfferedContact> lastOffers_;
  // Last object registration generation seen; a change means retained object
  // and dynamic pairs may hold dangling bodies, so they are dropped.
  uint64_t lastObjectGeneration_ = 0;
  bool notedDynamicSkip_ = false;
  bool notedWheelEdgewalk_ = false;
};
