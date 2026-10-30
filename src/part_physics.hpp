#pragma once
#include "car_geometry.hpp"
#include "physics_compound.hpp"
#include "physics_polyhedron.hpp"
#include <deque>
#include <memory>
#include <string_view>

struct CarPhysics;

struct PartPhysicsChild {
  size_t bodyIndex = 0;
  std::shared_ptr<const physics::BodyProto> prototype;
  std::unique_ptr<physics::Body> body;
};
struct PartPhysics {
  PartPhysics() = default;
  ~PartPhysics();
  PartPhysics(const PartPhysics &) = delete;
  PartPhysics &operator=(const PartPhysics &) = delete;
  physics::BodyProto rootPrototype;
  std::unique_ptr<physics::Body> root;
  std::deque<PartPhysicsChild> children;
  physics::CollisionCore bounds;
  int rootBodyIndex = -1;
  int steeringBodyIndex = -1;
  int argument = 0;
  uint64_t attachmentSequence = 0;
  CarPhysics *indexedCar = nullptr;
  PartPhysics *installedParent = nullptr;
  std::vector<PartPhysics *> installedChildren;
  physics::Body *rootGeometryBody() {
    for (auto &child : children)
      if (static_cast<int>(child.bodyIndex) == rootBodyIndex)
        return child.body.get();
    return nullptr;
  }
  const physics::Body *rootGeometryBody() const {
    for (const auto &child : children)
      if (static_cast<int>(child.bodyIndex) == rootBodyIndex)
        return child.body.get();
    return nullptr;
  }
};

// Detached compound only. No world registration or scene nodes are created.
std::unique_ptr<PartPhysics> buildPartPhysics(
    const CarConfig &config, const std::vector<CarGeometryPrototype> &geometry,
    std::string_view arguments, std::string &error,
    CarGeometryPrototype *clickGeometry = nullptr, float creationTime = 0);

// Align slot frames and link the independently owned part compounds. A target
// slot whose `slottype` names a wheel (101..400) is aligned to that wheel's
// record pose instead of the target's root body; targetSlotType carries the
// runtime slot's slottype for that case.
bool attachPartPhysics(PartPhysics &part, const PartSlotState &partSlot,
                       PartPhysics &target, const PartSlotState &targetSlot,
                       int32_t targetSlotType = 0);

// Stock getInfo(0x2f) on a car replaces the slot reference pose with the wheel
// record at car+0x13e4 + ((slottype - 1) % 100) * 0x2b4 when the slot's
// `slottype` is in (100, 0x191) (all.c:68278-68310). The record holds the wheel
// anchor (+0x78), the bone offset (+0x84) and the wheel orientation matrix
// (+0x90); the car center is subtracted from the anchor. False for every other
// slot type and when the target has no wheel record.
bool wheelReferencePose(const PartPhysics &target, int32_t slotType,
                        physics::State &out);

// The pose stock getInfo(0x2f) hands back for a slot: the wheel override when
// the slot type names a wheel, otherwise the target's root geometry body (the
// `part_comp` fallback is the compound root).
bool slotReferencePose(const PartPhysics &target, int32_t slotType,
                       physics::State &out);

// World position of a slot under that reference pose. Stock rotates the slot's
// own offset by the reference rotation and adds the reference position
// (all.c:86070, 00473790).
physics::Vec3 slotReferencePosition(const PartPhysics &target, int32_t slotType,
                                    const struct PartSlotState &slot);
void detachPartPhysics(PartPhysics &part);
