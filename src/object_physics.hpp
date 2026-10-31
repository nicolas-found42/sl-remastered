#pragma once

// Object body construction and ownership for the Free Ride object classes,
// following the stock object class body table and instance construction:
//   00464500 (body slot) and 00464900 (trigger slot) register one shape slot:
//     a slot-local position and euler orientation, a shape type word
//     (ball/sphere -> Sphere, box -> Box, rodx/rody/rodz -> CylinderX/Y/Z,
//     a mesh name or numeric id -> Mesh loaded from a resource), a mass and
//     up to three shape parameters. The class table holds at most 16 slots
//     across all slot kinds; the first slot is the default (stock class
//     +0x8b0). The parser (object_config) enforces the table cap and keeps
//     slot order; this class only stores the physics of Body and Trigger
//     slots.
//   00465bd0 builds an instance: the header line carries a position and three
//     euler angles (0054ecd0 builds the rotation matrix). Every body slot
//     becomes a child at R*slotLocal + position with rotation R*slotRot,
//     attached to a compound root; 0049a050 then finalizes the root (mass
//     aggregate, center shift, static when massless).
// The physics itself lives in physics_body/physics_compound_*; this file
// only owns the class-level prototypes and performs the stock construction
// order.

#include "object_config.hpp"
#include "physics_body.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace object {

// 0054ecd0: rotation matrix from three euler angles, in the stock operation
// order (x87 in the original; host sin/cos can round differently). Rows are
// stored with the stock stride of four floats (Mat34).
physics::Mat34 matrixFromEuler(const physics::Vec3 &euler);

// Owns the prototypes and body slots of one object class (the physics half of
// the stock slot table at class +0x128, 0x74 bytes per slot). Not copyable;
// prototypes have stable addresses so instances can hold const pointers.
class ObjectPhysics : public std::enable_shared_from_this<const ObjectPhysics> {
public:
  static constexpr size_t kMaxSlots = 0x10; // stock table cap, all kinds

  // Hosts must keep class physics alive through std::shared_ptr: every
  // instance body points into protos_, and Instance stashes a copy so the
  // prototypes survive transfer into the world registry.
  static std::shared_ptr<ObjectPhysics> create() {
    return std::shared_ptr<ObjectPhysics>(new ObjectPhysics);
  }
  ObjectPhysics(const ObjectPhysics &) = delete;
  ObjectPhysics &operator=(const ObjectPhysics &) = delete;
  ObjectPhysics(ObjectPhysics &&) = delete;
  ObjectPhysics &operator=(ObjectPhysics &&) = delete;

  // 00464500/00464900 slot registration. `slot` must have kind Body or
  // Trigger (Render/Part slots carry no physics; the caller skips them);
  // `slotIndex` is its index into ObjectConfig::slots and must be below the
  // 16-slot stock table cap. Primitive shapes use addBody; file-backed
  // meshes require real vertices and triangles through addMeshBody, and
  // resource-backed shapes use addResourceBody. No stand-in shapes are used.
  int addBody(const object_config::ObjectSlot &slot, size_t slotIndex,
              const std::vector<physics::Vec3> *meshVertices,
              std::string &error);
  int addMeshBody(const object_config::ObjectSlot &slot, size_t slotIndex,
                  const std::vector<physics::Vec3> &vertices,
                  std::vector<physics::MeshTriangle> triangles,
                  std::string &error);
  int addResourceBody(const object_config::ObjectSlot &slot, size_t slotIndex,
                      physics::BodyProto prototype, std::string &error);

  size_t bodyCount() const { return slots_.size(); }
  // Stock class +0x8b0: body-table index of the first registered slot, -1
  // when the class has no body or trigger slot.
  int defaultBody() const { return defaultBody_; }
  const physics::BodyProto *protoForBody(size_t body) const;

  // The bodies of one spawned instance. The caller owns this; `tree` is
  // non-owning, root first then children in body-table order, and is exactly
  // what physics::stepCompound / predictCompound / commitCompound and
  // physics::World::bodies consume.
  struct Instance {
    // Keeps the class prototypes (which every body in this tree references)
    // alive for as long as the instance, wherever the host moves it.
    std::shared_ptr<const ObjectPhysics> owner;
    std::unique_ptr<physics::BodyProto> rootPrototype; // 0049a050 aggregate
    std::unique_ptr<physics::Body> root;
    std::vector<std::unique_ptr<physics::Body>> children; // stable storage
    std::vector<physics::Body *> tree; // root first, slot order
  };

  // 00465bd0 physics portion. `position`/`euler` are the six floats of the
  // instance header line. Fails only when a slot prototype is missing or the
  // compound finalize rejects the tree; the instance is then untouched.
  std::optional<Instance> instantiate(const physics::Vec3 &position,
                                      const physics::Vec3 &euler,
                                      std::string &error) const;

private:
  ObjectPhysics() = default;
  int insertBody(const object_config::ObjectSlot &slot, size_t slotIndex,
                 physics::BodyProto prototype, std::string &error);

  struct Slot {
    object_config::SlotKind kind = object_config::SlotKind::Body;
    physics::Vec3 position{};                            // slot-local offset
    physics::Mat34 rotation = physics::matrixIdentity(); // 0054ecd0 euler
    uint32_t proto = kNoProto;
  };
  static constexpr uint32_t kNoProto = 0xffffffff;

  std::deque<physics::BodyProto> protos_; // stable storage
  std::vector<Slot> slots_;
  int defaultBody_ = -1;
};

// Move-only host handle over a spawned Instance, for registries (GameWorld)
// that store object physics without needing the construction machinery.
// The bodies stay valid for as long as the handle lives; destroying the
// handle destroys the bodies and releases the class prototypes.
class InstanceHandle {
public:
  InstanceHandle();
  ~InstanceHandle();
  InstanceHandle(InstanceHandle &&) noexcept;
  InstanceHandle &operator=(InstanceHandle &&) noexcept;
  InstanceHandle(const InstanceHandle &) = delete;
  InstanceHandle &operator=(const InstanceHandle &) = delete;

  bool valid() const { return storage_ != nullptr; }
  // Root-first, slot-order body tree for the stepper: exactly the input
  // physics::stepCompound / predictCompound / commitCompound take.
  const std::vector<physics::Body *> &bodies() const { return bodies_; }
  physics::Body *root() const { return root_; }
  size_t bodyCount() const { return bodies_.size(); }

private:
  friend class ObjectPhysics;
  friend InstanceHandle adoptInstance(ObjectPhysics::Instance &&);
  struct Storage; // defined in object_physics.cpp; holds Instance
  std::unique_ptr<Storage> storage_;
  std::vector<physics::Body *> bodies_; // non-owning view into storage_
  physics::Body *root_ = nullptr;
  std::shared_ptr<const ObjectPhysics> owner_;
};

// Moves a spawned Instance into a host-storable handle.
InstanceHandle adoptInstance(ObjectPhysics::Instance &&instance);

} // namespace object
