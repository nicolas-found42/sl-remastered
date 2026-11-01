#include "part_physics.hpp"
#include "car_physics.hpp"
#include "part_alignment.hpp"
#include "part_config.hpp"
#include "physics_compound_finalize.hpp"
#include "run_log.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

namespace {
physics::Mat34 matrix(const std::array<float, 9> &a) {
  return {a[0], a[1], a[2], 0, a[3], a[4], a[5], 0, a[6], a[7], a[8], 0};
}
} // namespace
std::unique_ptr<PartPhysics>
buildPartPhysics(const CarConfig &config,
                 const std::vector<CarGeometryPrototype> &geometry,
                 std::string_view arguments, std::string &error,
                 CarGeometryPrototype *clickGeometry, float creationTime) {
  error.clear();
  if (config.click && (!clickGeometry || !clickGeometry->body)) {
    error = "missing click geometry";
    return {};
  }
  if (config.rootBody >= 0 &&
      (static_cast<size_t>(config.rootBody) >= config.bodies.size() ||
       config.bodies[static_cast<size_t>(config.rootBody)].kind !=
           CarBodyRecord::Body ||
       (config.steeringBody >= 0 &&
        (static_cast<size_t>(config.steeringBody) >= config.bodies.size() ||
         config.bodies[static_cast<size_t>(config.steeringBody)].kind !=
             CarBodyRecord::Body)))) {
    error = "invalid root or steering body index";
    return {};
  }
  PartSlotDirective args{};
  const auto parsed = stockScan(arguments, "%f,%f,%f,%f,%f,%f,%d");
  for (size_t i = 0; i < parsed.values.size(); ++i) {
    if (i < 3)
      args.position[i] = parsed.values[i].f;
    else if (i < 6)
      args.orientation[i - 3] = parsed.values[i].f;
    else
      args.id = parsed.values[i].i;
  }
  PartSlotState initial;
  if (!makePartSlotState(args, initial)) {
    error = "non-finite part transform";
    return {};
  }
  if (config.rootBody < 0) {
    if (!clickGeometry || !clickGeometry->body) {
      error = "missing click-only geometry";
      return {};
    }
    auto result = std::make_unique<PartPhysics>();
    result->rootPrototype = *clickGeometry->body;
    const auto rotation = physics::matrixMultiply(
        matrix(initial.rotation), matrix(clickGeometry->transform.rotation));
    result->root = std::make_unique<physics::Body>(physics::createBody(
        result->rootPrototype, initial.position, &rotation, creationTime));
    result->argument = args.id;
    if (result->rootPrototype.shape.core)
      result->bounds = *result->rootPrototype.shape.core;
    return result;
  }
  std::vector<const CarGeometryPrototype *> indexed(config.bodies.size());
  for (const auto &g : geometry) {
    if (g.bodyIndex >= indexed.size() || indexed[g.bodyIndex] ||
        config.bodies[g.bodyIndex].kind != CarBodyRecord::Body) {
      error = "invalid or duplicate geometry body index";
      return {};
    }
    if (!g.body) {
      error = "missing geometry prototype";
      return {};
    }
    if ((g.body->flags & physics::kProtoRoot) ||
        !g.body->massPropertiesSupported) {
      error = "unsupported root prototype or mass properties";
      return {};
    }
    indexed[g.bodyIndex] = &g;
  }
  for (size_t i = 0; i < indexed.size(); ++i)
    if (config.bodies[i].kind == CarBodyRecord::Body && !indexed[i]) {
      error = "missing geometry for body " + std::to_string(i);
      return {};
    }
  auto result = std::make_unique<PartPhysics>();
  auto root = physics::createBodyProto(1, 6, 0, 0, 0, nullptr, error);
  if (!root)
    return {};
  result->rootPrototype = std::move(*root);
  const auto rootRotation = matrix(initial.rotation);
  result->root = std::make_unique<physics::Body>(physics::createBody(
      result->rootPrototype, initial.position, &rootRotation, creationTime));
  result->root->flags |= physics::kBodyHasChildren;
  result->rootBodyIndex = config.rootBody;
  result->steeringBodyIndex = config.steeringBody;
  result->argument = args.id;
  for (size_t i = 0; i < indexed.size(); ++i) {
    const auto *g = indexed[i];
    if (!g)
      continue;
    const auto localRotation = matrix(g->transform.rotation);
    const auto combined = physics::matrixMultiply(rootRotation, localRotation);
    physics::Vec3 offset{g->body->shape.centerOffset[0],
                         g->body->shape.centerOffset[1],
                         g->body->shape.centerOffset[2]};
    const auto rotated = physics::matrixTransformRows(localRotation, offset);
    physics::Vec3 local{};
    for (size_t j = 0; j < 3; ++j)
      local[j] = g->transform.position[j] + rotated[j];
    const auto world =
        physics::transformPoint(initial.position, rootRotation, local);
    result->children.emplace_back();
    auto &child = result->children.back();
    child.bodyIndex = i;
    child.prototype = g->body;
    child.body = std::make_unique<physics::Body>(
        physics::createBody(*child.prototype, world, &combined, creationTime));
    child.body->flags |= physics::kBodyChild;
    if (g->bodyFlags & kBodyNoCollision)
      child.body->flags |= 0x20;
    if (config.click || (g->bodyFlags & kBodyNoClick))
      child.body->flags |= 0x4000;
    if (static_cast<int>(i) == config.steeringBody)
      child.body->flags |= 0x8020;
    if (!physics::attachBody(*child.body, *result->root)) {
      error = "invalid part attachment";
      return {};
    }
  }
  if (clickGeometry) {
    const auto &g = *clickGeometry;
    const auto combined =
        physics::matrixMultiply(rootRotation, matrix(g.transform.rotation));
    const auto world = physics::transformPoint(initial.position, rootRotation,
                                               g.transform.position);
    clickGeometry->body->flags &= ~physics::kProtoStatic;
    result->children.emplace_back();
    auto &child = result->children.back();
    child.bodyIndex = config.bodies.size();
    child.prototype = g.body;
    child.body = std::make_unique<physics::Body>(
        physics::createBody(*child.prototype, world, &combined, creationTime));
    result->clickBody = child.body.get();
    child.body->flags |= physics::kBodyChild;
    if (!physics::attachBody(*child.body, *result->root)) {
      error = "invalid click attachment";
      return {};
    }
  }
  std::vector<physics::Body *> children;
  for (auto &child : result->children)
    children.push_back(child.body.get());
  if (!finalizeCompound(*result->root, result->rootPrototype, children, error))
    return {};
  result->bounds = *result->rootPrototype.shape.core;
  return result;
}

namespace {
void movePartTree(PartPhysics &part, const physics::Vec3 &position,
                  const physics::Mat34 &rotation) {
  auto &root = *part.root;
  for (auto &state : root.state) {
    state.pos = position;
    state.quat = physics::quatFromMatrix(rotation);
    state.rot = physics::matrixFromQuat(state.quat);
    physics::updateWorldInertia(*root.proto, state);
  }
  for (auto &child : part.children) {
    if (!child.body || !child.body->attachment)
      continue;
    for (auto &state : child.body->state) {
      physics::predictChildPose(root.state[root.current],
                                child.body->attachment->relative, state);
      physics::updateWorldInertia(*child.body->proto, state);
    }
  }
  for (auto *child : part.installedChildren) {
    if (!child || !child->root || !child->root->attachment)
      continue;
    physics::State pose{};
    auto &parentBody = *child->root->attachment->parent;
    physics::predictChildPose(parentBody.state[parentBody.current],
                              child->root->attachment->relative, pose);
    movePartTree(*child, pose.pos, pose.rot);
  }
}
} // namespace

namespace {
bool finalizeInstalledParent(PartPhysics &target, physics::Body &parent,
                             std::string &error) {
  std::vector<physics::Body *> bodies;
  physics::BodyProto *prototype = nullptr;
  if (target.indexedCar) {
    auto &car = *target.indexedCar;
    const auto found = car.bodyChildren.find(&parent);
    if (found == car.bodyChildren.end()) {
      error = "physical parent is not indexed";
      return false;
    }
    bodies = found->second;
    if (&parent == car.root.get())
      prototype = &car.rootPrototype;
  }
  auto *top = &target;
  while (top->installedParent)
    top = top->installedParent;
  auto collect = [&](auto &self, PartPhysics &part) -> void {
    if (part.root.get() == &parent)
      prototype = &part.rootPrototype;
    if (!target.indexedCar) {
      for (auto &child : part.children)
        if (child.body && child.body->attachment &&
            child.body->attachment->parent == &parent)
          bodies.push_back(child.body.get());
      for (auto *child : part.installedChildren)
        if (child->root && child->root->attachment &&
            child->root->attachment->parent == &parent)
          bodies.push_back(child->root.get());
    }
    for (auto *child : part.installedChildren)
      self(self, *child);
  };
  collect(collect, *top);
  if (!prototype) {
    error = "physical parent prototype is unavailable";
    return false;
  }
  if (!finalizeCompound(parent, *prototype, bodies, error))
    return false;
  if (parent.attachment && parent.attachment->parent)
    return finalizeInstalledParent(target, *parent.attachment->parent, error);
  return true;
}

void setIndexedCar(PartPhysics &part, CarPhysics *car) {
  part.indexedCar = car;
  for (auto *child : part.installedChildren)
    setIndexedCar(*child, car);
}
} // namespace

void detachPartPhysics(PartPhysics &part) {
  while (!part.installedChildren.empty())
    detachPartPhysics(*part.installedChildren.back());
  if (!part.installedParent)
    return;
  auto *car = part.indexedCar;
  auto *target = part.installedParent;
  auto *bodyParent = part.root && part.root->attachment
                         ? part.root->attachment->parent
                         : nullptr;
  auto &siblings = part.installedParent->installedChildren;
  siblings.erase(std::remove(siblings.begin(), siblings.end(), &part),
                 siblings.end());
  part.installedParent = nullptr;
  if (part.root) {
    part.root->attachment.reset();
    part.root->flags &= ~physics::kBodyChild;
  }
  setIndexedCar(part, nullptr);
  if (car) {
    std::string error;
    if (!indexCarBodies(*car, error)) {
      for (auto &observation : car->bodyObservations) {
        setLinkOwner(observation.second->resource, nullptr);
        observation.second->body = nullptr;
      }
      car->bodyChildren.clear();
      car->bodyObservations.clear();
      car->teardownOrder.clear();
    }
  }
  if (bodyParent) {
    std::string error;
    finalizeInstalledParent(*target, *bodyParent, error);
  }
}

PartPhysics::~PartPhysics() {
  while (!installedChildren.empty())
    detachPartPhysics(*installedChildren.back());
  detachPartPhysics(*this);
}

bool wheelReferencePose(const PartPhysics &target, int32_t slotType,
                        physics::State &out) {
  const uint32_t type = static_cast<uint32_t>(slotType) & 0xffffu;
  if (type <= 100u || type >= 0x191u)
    return false;
  const char *decline = nullptr;
  size_t wheels = 0;
  size_t index = 0;
  if (!target.indexedCar) {
    decline = "no_indexed_car";
  } else {
    index = (type - 1u) % 100u;
    wheels = target.indexedCar->wheels.size();
    if (index >= wheels || !target.indexedCar->wheels[index] ||
        !target.indexedCar->wheels[index]->hubBody)
      decline = "no_wheel_record";
    else if (!target.indexedCar->wheels[index]->hubBody->proto)
      decline = "record_without_proto";
  }
  if (decline) {
    // The override declines toward the geometry fallback: a runtime parity
    // divergence point that must stay visible in the run log.
    runlog::event(runlog::EventKind::Note,
                  "wheelref {\"v\":1,\"slotType\":%u,\"index\":%zu,"
                  "\"wheels\":%zu,\"reason\":\"%s\"}",
                  type, index, wheels, decline);
    return false;
  }
  const auto& wheel = *target.indexedCar->wheels[index];
  const auto &body = *wheel.hubBody;
  runlog::event(runlog::EventKind::Note,
                "wheelref pose: slot=%u i=%zu anchor=%.3f,%.3f,%.3f bone=%.3f,%.3f,%.3f center=%.3f,%.3f,%.3f hubY=%.3f rootY=%.3f",
                type, index, wheel.anchor[0], wheel.anchor[1], wheel.anchor[2],
                wheel.config.bonePosition[0], wheel.config.bonePosition[1], wheel.config.bonePosition[2],
                target.indexedCar->center[0], target.indexedCar->center[1], target.indexedCar->center[2],
                body.state[body.current].pos[1], target.indexedCar->root->state[target.indexedCar->root->current].pos[1]);
  out = body.state[body.current];
  return true;
}

bool slotReferencePose(const PartPhysics &target, int32_t slotType,
                       physics::State &out) {
  if (wheelReferencePose(target, slotType, out))
    return true;
  const auto *body = target.rootGeometryBody();
  if (!body)
    body = target.root.get();
  if (!body || !body->proto)
    return false;
  out = body->state[body->current];
  return true;
}

physics::Vec3 slotReferencePosition(const PartPhysics &target, int32_t slotType,
                                    const PartSlotState &slot) {
  physics::State pose{};
  if (!slotReferencePose(target, slotType, pose))
    return {0, 0, 0};
  return physics::transformPoint(pose.pos, pose.rot, slot.position);
}

bool attachPartPhysics(PartPhysics &part, const PartSlotState &partSlot,
                       PartPhysics &target, const PartSlotState &targetSlot,
                       int32_t targetSlotType) {
  if (!part.root || !target.root || part.installedParent ||
      part.root->attachment)
    return false;
  for (auto *ancestor = &target; ancestor; ancestor = ancestor->installedParent)
    if (ancestor == &part)
      return false;
  auto *slotBody = target.rootGeometryBody();
  if (!slotBody)
    slotBody = target.root.get();
  physics::State targetPose = slotBody->state[slotBody->current];
  physics::State wheelPose{};
  if (wheelReferencePose(target, targetSlotType, wheelPose))
    targetPose = wheelPose;
  const auto aligned = alignPartSlots(targetPose, targetSlot, partSlot);
  movePartTree(part, aligned.position, aligned.rotation);
  auto *bodyParent = slotBody;
  if ((bodyParent->flags & 0x8000u) == 0 && bodyParent->attachment)
    bodyParent = bodyParent->attachment->parent;
  if (!physics::attachBody(*part.root, *bodyParent))
    return false;
  static std::atomic<uint64_t> nextSequence{1};
  part.attachmentSequence =
      nextSequence.fetch_add(1, std::memory_order_relaxed);
  part.installedParent = &target;
  target.installedChildren.push_back(&part);
  setIndexedCar(part, target.indexedCar);
  if (part.indexedCar) {
    std::string error;
    if (!indexCarBodies(*part.indexedCar, error)) {
      detachPartPhysics(part);
      return false;
    }
  }
  std::string error;
  if (!finalizeInstalledParent(target, *bodyParent, error)) {
    detachPartPhysics(part);
    return false;
  }
  return true;
}
