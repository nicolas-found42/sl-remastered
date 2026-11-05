#include "object_physics.hpp"

#include "physics_compound.hpp"
#include "physics_compound_finalize.hpp"
#include "physics_mesh_mass.hpp"
#include "physics_polyhedron.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace object {
namespace {

physics::Vec3 toVec3(const object_config::Vec3 &v) { return {v.x, v.y, v.z}; }

// Collision core assignment is the caller's job in the car code (meshCore
// lives with the collision mesh loader); for object classes the shapes are
// analytic, so the cores are built here from the same primitives the stock
// collision path lazily builds (004ac1c0/004ac370/004ac440).
void assignCore(physics::BodyProto &proto) {
  physics::Shape &s = proto.shape;
  switch (s.type) {
  case physics::ShapeType::Box:
    s.core = std::make_shared<physics::CollisionCore>(
        physics::boxCore({s.dim[0], s.dim[1], s.dim[2]}));
    break;
  case physics::ShapeType::Sphere:
    s.core = std::make_shared<physics::CollisionCore>(physics::pointCore());
    break;
  case physics::ShapeType::CylinderX:
    s.core = std::make_shared<physics::CollisionCore>(
        physics::segmentCore({-s.dim[0], 0, 0}, {s.dim[0], 0, 0}));
    break;
  case physics::ShapeType::CylinderY:
    s.core = std::make_shared<physics::CollisionCore>(
        physics::segmentCore({0, -s.dim[0], 0}, {0, s.dim[0], 0}));
    break;
  case physics::ShapeType::CylinderZ:
    s.core = std::make_shared<physics::CollisionCore>(
        physics::segmentCore({0, 0, -s.dim[0]}, {0, 0, s.dim[0]}));
    break;
  default:
    break; // Mesh cores need the decoded mesh; the host collision mesh
           // loader owns meshCore for those, as with car geometry.
  }
}

} // namespace

// 0054ecd0. The stock routine writes the matrix with x87 intermediates; the
// host sin/cos can differ in the last bits.
physics::Mat34 matrixFromEuler(const physics::Vec3 &e) {
  const float sa = std::sin(e[0]), ca = std::cos(e[0]);
  const float sb = std::sin(e[1]), cb = std::cos(e[1]);
  const float sc = std::sin(e[2]), cc = std::cos(e[2]);
  physics::Mat34 m = physics::matrixIdentity();
  m[0] = cc * ca + sc * sb * sa;
  m[1] = cc * sb * sa - sc * ca;
  m[2] = cb * sa;
  m[4] = sc * cb;
  m[5] = cc * cb;
  m[6] = -sb;
  m[8] = sc * sb * ca - cc * sa;
  m[9] = sc * sa + cc * sb * ca;
  m[10] = cb * ca;
  return m;
}

int ObjectPhysics::addBody(const object_config::ObjectSlot &slot,
                           size_t slotIndex,
                           const std::vector<physics::Vec3> *meshVertices,
                           std::string &error) {
  error.clear();
  if (slot.kind != object_config::SlotKind::Body &&
      slot.kind != object_config::SlotKind::Trigger) {
    error = "slot kind carries no physics";
    return -1;
  }
  if (slotIndex >= kMaxSlots) {
    error = "object slot index outside the 16-slot stock table";
    return -1;
  }

  const auto &shape = slot.shape;
  if (shape.kind == object_config::ShapeKind::Mesh ||
      shape.kind == object_config::ShapeKind::ReuseRegistry || meshVertices) {
    error = "mesh body requires owned geometry and faces";
    return -1;
  }
  const float p4 = shape.params[0];
  const float p5 =
      shape.kind == object_config::ShapeKind::Sphere ? 0.0f : shape.params[1];
  const float p6 =
      shape.kind == object_config::ShapeKind::Box ? shape.params[2] : 0.0f;
  auto created =
      physics::createBodyProto(shape.mass, static_cast<uint32_t>(shape.kind),
                               p4, p5, p6, nullptr, error);
  if (!created)
    return -1;
  assignCore(*created);
  return insertBody(slot, slotIndex, std::move(*created), error);
}

int ObjectPhysics::addMeshBody(const object_config::ObjectSlot &slot,
                               size_t slotIndex,
                               const std::vector<physics::Vec3> &vertices,
                               std::vector<physics::MeshTriangle> triangles,
                               std::string &error) {
  if (slot.shape.kind != object_config::ShapeKind::Mesh || vertices.empty() ||
      triangles.empty()) {
    error = "object mesh requires decoded vertices and triangles";
    return -1;
  }
  auto created =
      physics::createBodyProto(slot.shape.mass, 6, 1, 1, 1, &vertices, error);
  if (!created)
    return -1;
  created->shape.triangles = std::move(triangles);
  auto low = vertices.front(), high = low;
  // 004b5c30 includes the first through the penultimate vertex.
  for (size_t i = 0; i + 1 < vertices.size(); ++i)
    for (size_t axis = 0; axis < 3; ++axis) {
      low[axis] = std::min(low[axis], vertices[i][axis]);
      high[axis] = std::max(high[axis], vertices[i][axis]);
    }
  for (size_t axis = 0; axis < 3; ++axis) {
    created->shape.extent[axis] = (high[axis] - low[axis]) * 0.5f;
    created->shape.centerOffset[axis] = low[axis] + created->shape.extent[axis];
  }
  if (!created->massPropertiesSupported) {
    physics::Vec3 center{};
    if (!physics::applyMeshMassProperties(*created, created->shape.triangles,
                                          center, error))
      return -1;
    float radiusSquared = 0;
    for (const auto &v : created->shape.vertices)
      radiusSquared =
          std::max(radiusSquared, v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    created->shape.boundRadius = std::sqrt(radiusSquared) + physics::k::skin;
  }
  std::vector<std::vector<size_t>> faces;
  faces.reserve(created->shape.triangles.size());
  for (const auto &triangle : created->shape.triangles)
    faces.push_back(
        {triangle.indices[0], triangle.indices[1], triangle.indices[2]});
  auto core = std::make_shared<physics::CollisionCore>();
  if (!physics::meshCore(created->shape.vertices, faces, *core, error))
    return -1;
  created->shape.core = std::move(core);
  return insertBody(slot, slotIndex, std::move(*created), error);
}

int ObjectPhysics::addResourceBody(const object_config::ObjectSlot &slot,
                                   size_t slotIndex,
                                   physics::BodyProto prototype,
                                   std::string &error) {
  if (slot.shape.kind != object_config::ShapeKind::ReuseRegistry) {
    error = "object slot is not a geometry resource";
    return -1;
  }
  return insertBody(slot, slotIndex, std::move(prototype), error);
}

int ObjectPhysics::insertBody(const object_config::ObjectSlot &slot,
                              size_t slotIndex, physics::BodyProto created,
                              std::string &error) {
  error.clear();
  if ((slot.kind != object_config::SlotKind::Body &&
       slot.kind != object_config::SlotKind::Trigger) ||
      slotIndex >= kMaxSlots) {
    error = "invalid object body slot";
    return -1;
  }
  created.flags &= ~physics::kProtoCreated;
  if (created.invMass == physics::k::zero)
    created.flags |= physics::kProtoStatic;
  if (slot.modifiers.nocollision)
    created.flags |= physics::kProtoBit8;
  if (slot.modifiers.buoy)
    created.flags |= physics::kProtoWater;
  if (slot.modifiers.hasFriction)
    created.shape.friction = slot.modifiers.friction;
  if (defaultBody_ < 0)
    defaultBody_ = static_cast<int>(slots_.size());
  protos_.push_back(std::move(created));
  Slot entry;
  entry.kind = slot.kind;
  entry.position = toVec3(slot.shape.position);
  entry.rotation = matrixFromEuler(toVec3(slot.shape.orientation));
  entry.proto = static_cast<uint32_t>(protos_.size() - 1);
  slots_.push_back(entry);
  return static_cast<int>(slots_.size() - 1);
}

const physics::BodyProto *ObjectPhysics::protoForBody(size_t body) const {
  if (body >= slots_.size() || slots_[body].proto == kNoProto)
    return nullptr;
  return &protos_[slots_[body].proto];
}

std::optional<ObjectPhysics::Instance>
ObjectPhysics::instantiate(const physics::Vec3 &position,
                           const physics::Vec3 &euler,
                           std::string &error) const {
  error.clear();
  const physics::Mat34 placement = matrixFromEuler(euler);

  // 00465bd0: child world pose is R*slotLocal + position with rotation
  // R*slotRot, built in slot order before the root exists.
  struct ChildPose {
    const physics::BodyProto *proto;
    object_config::SlotKind kind;
    physics::Vec3 world;
    physics::Mat34 rotation;
  };
  std::vector<ChildPose> poses;
  for (const Slot &slot : slots_) {
    if (slot.proto == kNoProto)
      continue;
    const physics::BodyProto &proto = protos_[slot.proto];
    ChildPose pose;
    pose.proto = &proto;
    pose.kind = slot.kind;
    pose.world = physics::transformPoint(position, placement, slot.position);
    pose.rotation = physics::matrixMultiply(placement, slot.rotation);
    poses.push_back(pose);
  }

  Instance out;
  out.owner = shared_from_this();
  // 00465bd0 creates the compound root as a mass-1 unit Box. 0049a050
  // replaces aggregate mass/bounds while preserving its constructor volume.
  auto rootProto = physics::createBodyProto(
      1, static_cast<uint32_t>(physics::ShapeType::Box), 1, 1, 1, nullptr,
      error);
  if (!rootProto) {
    error = "object root prototype: " + error;
    return std::nullopt;
  }
  out.rootPrototype =
      std::make_unique<physics::BodyProto>(std::move(*rootProto));
  out.root = std::make_unique<physics::Body>(
      physics::createBody(*out.rootPrototype, position, &placement));
  out.root->flags |= physics::kBodyHasChildren;
  out.children.reserve(poses.size());
  for (const ChildPose &pose : poses) {
    out.children.push_back(std::make_unique<physics::Body>(
        physics::createBody(*pose.proto, pose.world, &pose.rotation)));
    physics::Body &child = *out.children.back();
    child.flags |= physics::kBodyChild;
    if (pose.kind == object_config::SlotKind::Trigger)
      child.flags |= 0x11040; // 00465e2c trigger body flags
    if (!physics::attachBody(child, *out.root)) {
      error = "invalid object body attachment";
      return std::nullopt;
    }
  }
  std::vector<physics::Body *> childPtrs;
  childPtrs.reserve(out.children.size());
  for (const auto &child : out.children)
    childPtrs.push_back(child.get());
  // 0049a050: mass aggregate, center shift, static fixup.
  if (!finalizeCompound(*out.root, *out.rootPrototype, childPtrs, error))
    return std::nullopt;
  out.tree.push_back(out.root.get());
  for (physics::Body *child : childPtrs)
    out.tree.push_back(child);
  return out;
}

// ------------------------------------------------------------- host handle
struct InstanceHandle::Storage {
  explicit Storage(ObjectPhysics::Instance &&value)
      : instance(std::move(value)) {}
  ObjectPhysics::Instance instance;
};

InstanceHandle::InstanceHandle() = default;
InstanceHandle::~InstanceHandle() = default;
InstanceHandle::InstanceHandle(InstanceHandle &&) noexcept = default;
InstanceHandle &InstanceHandle::operator=(InstanceHandle &&) noexcept = default;

InstanceHandle adoptInstance(ObjectPhysics::Instance &&instance) {
  InstanceHandle handle;
  handle.owner_ = instance.owner;
  handle.storage_ =
      std::make_unique<InstanceHandle::Storage>(std::move(instance));
  handle.bodies_ = handle.storage_->instance.tree;
  handle.root_ = handle.storage_->instance.root.get();
  return handle;
}

} // namespace object
