#include "part_physics.hpp"
#include "car_physics.hpp"
#include "part_alignment.hpp"
#include "part_config.hpp"
#include "physics_compound_finalize.hpp"
#include "run_log.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <string>

namespace {
physics::Mat34 matrix(const std::array<float, 9>& a) {
  return {a[0], a[1], a[2], 0, a[3], a[4], a[5], 0, a[6], a[7], a[8], 0};
}

// Newer 00567020/004e9730 use SSE's x,y,z accumulation, unlike the older
// x87 helpers used elsewhere in the physics implementation.
physics::Mat34 placementProduct(const physics::Mat34& a, const physics::Mat34& b) {
  physics::Mat34 result{};
  for (size_t row = 0; row < 3; ++row)
    for (size_t column = 0; column < 3; ++column)
      result[row * 4 + column] =
          (a[row * 4] * b[column] + a[row * 4 + 1] * b[4 + column]) +
          a[row * 4 + 2] * b[8 + column];
  return result;
}
physics::Vec3 placementVector(const physics::Mat34& rotation, const physics::Vec3& point) {
  physics::Vec3 result{};
  for (size_t row = 0; row < 3; ++row)
    result[row] = (rotation[row * 4] * point[0] + rotation[row * 4 + 1] * point[1]) +
                  rotation[row * 4 + 2] * point[2];
  return result;
}
physics::Quat placementQuaternion(const physics::Quat& a, const physics::Quat& b) {
  // 00567b40: retain its half-product grouping when publishing child poses.
  const float sum = (b[0] + b[1]) * (a[0] + a[2]);
  const float difference = (b[0] - b[1]) * (a[0] - a[2]);
  const float positive = (b[3] - b[2]) * (a[3] + a[1]);
  const float negative = (b[3] + b[2]) * (a[3] - a[1]);
  const float halfSum = (difference + sum) * 0.5f;
  const float halfNegative = negative * 0.5f;
  return {
      (b[0] + b[3]) * (a[0] + a[3]) - (positive * 0.5f + halfSum + halfNegative),
      ((sum - difference + positive) - negative) * 0.5f + (a[3] - a[0]) * (b[2] + b[1]),
      ((sum - difference) - positive) * 0.5f + halfNegative + (b[3] - b[0]) * (a[1] + a[2]),
      (positive * 0.5f - halfSum) + halfNegative + (b[1] - b[2]) * (a[2] - a[1])};
}
} // namespace
std::unique_ptr<PartPhysics>
buildPartPhysics(const CarConfig& config,
                 const std::vector<CarGeometryPrototype>& geometry,
                 std::string_view arguments, std::string& error,
                 CarGeometryPrototype* clickGeometry, float creationTime) {
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
  std::vector<const CarGeometryPrototype*> indexed(config.bodies.size());
  for (const auto& g : geometry) {
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
    const auto* g = indexed[i];
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
    auto& child = result->children.back();
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
    const auto& g = *clickGeometry;
    const auto combined =
        physics::matrixMultiply(rootRotation, matrix(g.transform.rotation));
    const auto world = physics::transformPoint(initial.position, rootRotation,
                                               g.transform.position);
    clickGeometry->body->flags &= ~physics::kProtoStatic;
    result->children.emplace_back();
    auto& child = result->children.back();
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
  std::vector<physics::Body*> children;
  for (auto& child : result->children)
    children.push_back(child.body.get());
  if (!finalizeCompound(*result->root, result->rootPrototype, children, error))
    return {};
  result->bounds = *result->rootPrototype.shape.core;
  return result;
}

namespace {
void movePartTree(PartPhysics& part, const physics::Vec3& position,
                  const physics::Mat34& rotation) {
  auto& root = *part.root;
  for (auto& state : root.state) {
    state.pos = position;
    state.quat = physics::quatFromMatrix(rotation);
    state.rot = physics::matrixFromQuat(state.quat);
    physics::updateWorldInertia(*root.proto, state);
  }
  for (auto& child : part.children) {
    if (!child.body || !child.body->attachment)
      continue;
    for (auto& state : child.body->state) {
      physics::predictChildPose(root.state[root.current],
                                child.body->attachment->relative, state);
      physics::updateWorldInertia(*child.body->proto, state);
    }
  }
  for (auto* child : part.installedChildren) {
    if (!child || !child->root || !child->root->attachment)
      continue;
    physics::State pose{};
    auto& parentBody = *child->root->attachment->parent;
    physics::predictChildPose(parentBody.state[parentBody.current],
                              child->root->attachment->relative, pose);
    movePartTree(*child, pose.pos, pose.rot);
  }
}
} // namespace

namespace {
bool finalizeInstalledParent(PartPhysics& target, physics::Body& parent,
                             std::string& error) {
  // FUN_0049a050's 0x8000 branch retains the helper prototype's own mass
  // and center, updates inertia, then continues up to its physical parent.
  if (parent.flags & 0x8000) {
    for (auto& state : parent.state) physics::updateWorldInertia(*parent.proto, state);
    return parent.attachment && parent.attachment->parent &&
           finalizeInstalledParent(target, *parent.attachment->parent, error);
  }
  std::vector<physics::Body*> bodies;
  physics::BodyProto* prototype = nullptr;
  if (target.indexedCar) {
    auto& car = *target.indexedCar;
    const auto found = car.bodyChildren.find(&parent);
    if (found == car.bodyChildren.end()) {
      error = "physical parent is not indexed";
      return false;
    }
    bodies = found->second;
    if (&parent == car.root.get())
      prototype = &car.rootPrototype;
  }
  auto* top = &target;
  while (top->installedParent)
    top = top->installedParent;
  auto collect = [&](auto& self, PartPhysics& part) -> void {
    if (part.root.get() == &parent)
      prototype = &part.rootPrototype;
    if (!target.indexedCar) {
      for (auto& child : part.children)
        if (child.body && child.body->attachment &&
            child.body->attachment->parent == &parent)
          bodies.push_back(child.body.get());
      for (auto* child : part.installedChildren)
        if (child->root && child->root->attachment &&
            child->root->attachment->parent == &parent)
          bodies.push_back(child->root.get());
    }
    for (auto* child : part.installedChildren)
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

void setIndexedCar(PartPhysics& part, CarPhysics* car) {
  part.indexedCar = car;
  for (auto* child : part.installedChildren)
    setIndexedCar(*child, car);
}
} // namespace

bool detachPartPhysics(PartPhysics& part, std::string* error) {
  if (error)
    error->clear();
  auto failed = [&](const std::string& detail) {
    if (error)
      *error = detail;
    return false;
  };
  // Newer 00501d00 unlinks only this external attachment. Mounted children
  // retain their parent, relative frames and committed world poses.
  if (!part.installedParent)
    return true;
  auto* car = part.indexedCar;
  auto* target = part.installedParent;
  auto* bodyParent = part.root && part.root->attachment
                         ? part.root->attachment->parent
                         : nullptr;
  auto& siblings = part.installedParent->installedChildren;
  siblings.erase(std::remove(siblings.begin(), siblings.end(), &part),
                 siblings.end());
  part.installedParent = nullptr;
  if (part.root) {
    part.root->attachment.reset();
    part.root->flags &= ~physics::kBodyChild;
  }
  setIndexedCar(part, nullptr);
  if (car) {
    std::string detail;
    if (!indexCarBodies(*car, detail)) {
      // Destruction must not retain observations pointing at a freed part.
      // Production callers also receive the failure instead of continuing.
      for (auto& observation : car->bodyObservations) {
        setLinkOwner(observation.second->resource, nullptr);
        observation.second->body = nullptr;
      }
      car->bodyChildren.clear();
      car->bodyObservations.clear();
      car->teardownOrder.clear();
      return failed("part detach index: " + detail);
    }
  }
  if (bodyParent) {
    std::string detail;
    if (!finalizeInstalledParent(*target, *bodyParent, detail))
      return failed("part detach finalization: " + detail);
  } else {
    return failed("part detach parent attachment unavailable");
  }
  return true;
}

bool partPhysicsTree(PartPhysics& part, std::vector<physics::Body*>& tree,
                     std::string& error) {
  tree.clear();
  error.clear();
  std::vector<physics::Body*> bodies;
  std::vector<PartPhysics*> parts;
  std::function<bool(PartPhysics&)> collect = [&](PartPhysics& current) {
    if (!current.root || std::find(parts.begin(), parts.end(), &current) != parts.end())
      return false;
    parts.push_back(&current);
    bodies.push_back(current.root.get());
    for (auto& child : current.children)
      bodies.push_back(child.body.get());
    for (auto* child : current.installedChildren)
      if (!child || child->installedParent != &current || !collect(*child))
        return false;
    return true;
  };
  if (!collect(part) || part.root->attachment) {
    error = "part tree: detached owner unavailable";
    return false;
  }
  for (size_t i = 0; i < bodies.size(); ++i) {
    auto* body = bodies[i];
    const auto before = bodies.begin() + static_cast<std::ptrdiff_t>(i);
    if (!body || !body->proto || body->current > 1 ||
        std::find(bodies.begin(), before, body) != before) {
      error = "part tree: invalid or repeated body";
      return false;
    }
  }
  std::function<void(physics::Body*)> visit = [&](physics::Body* body) {
    if (std::find(tree.begin(), tree.end(), body) != tree.end())
      return;
    tree.push_back(body);
    for (auto it = bodies.rbegin(); it != bodies.rend(); ++it)
      if ((*it)->attachment && (*it)->attachment->parent == body)
        visit(*it);
  };
  visit(part.root.get());
  if (tree.size() != bodies.size()) {
    tree.clear();
    error = "part tree: child parent outside owner or cycle";
    return false;
  }
  return true;
}

bool placePartPhysics(PartPhysics& part, const physics::Vec3& position,
                      const physics::Mat34& rotation,
                      std::vector<physics::Body*>& affected, std::string& error) {
  affected.clear();
  error.clear();
  const auto finite = [](const auto& values) {
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
  };
  if (!finite(position) || !finite(rotation)) {
    error = "part placement: nonfinite requested pose";
    return false;
  }
  std::vector<physics::Body*> tree;
  if (part.indexedCar) {
    tree.assign(part.indexedCar->teardownOrder.rbegin(),
                part.indexedCar->teardownOrder.rend());
    if (tree.empty() || tree.front() != part.indexedCar->root.get()) {
      error = "part placement: indexed car tree unavailable";
      return false;
    }
  } else {
    auto* owner = &part;
    std::vector<PartPhysics*> ancestors;
    while (owner->installedParent) {
      if (std::find(ancestors.begin(), ancestors.end(), owner) != ancestors.end()) {
        error = "part placement: owner cycle";
        return false;
      }
      ancestors.push_back(owner);
      owner = owner->installedParent;
    }
    if (!partPhysicsTree(*owner, tree, error))
      return false;
  }
  auto* selected = part.rootBodyIndex < 0 ? part.root.get() : part.rootGeometryBody();
  if (!selected || std::find(tree.begin(), tree.end(), selected) == tree.end()) {
    error = "part placement: selected body outside owner";
    return false;
  }
  for (size_t i = 0; i < tree.size(); ++i) {
    const auto end = tree.begin() + static_cast<std::ptrdiff_t>(i);
    auto* body = tree[i];
    if (!body || !body->proto || body->current > 1 ||
        std::find(tree.begin(), end, body) != end ||
        !finite(body->state[body->current].pos) || !finite(body->state[body->current].rot) ||
        !finite(body->state[body->current].quat) ||
        (i == 0 && body->attachment) ||
        (i != 0 && (!body->attachment ||
                    std::find(tree.begin(), end, body->attachment->parent) == end ||
                    !finite(body->attachment->relative.position) ||
                    !finite(body->attachment->relative.rotation()) ||
                    !finite(body->attachment->relative.orientation())))) {
      error = "part placement: invalid body or attachment tree";
      return false;
    }
  }

  // 004e9730 deliberately multiplies the requested matrix by each forward
  // attachment matrix in selected-to-root order. This can double a selected
  // body's local angle after child publication; preserve that Stock quirk.
  auto rootRotation = rotation;
  physics::Vec3 relativePosition{};
  for (auto* body = selected; body->attachment; body = body->attachment->parent) {
    const auto& relative = body->attachment->relative;
    relativePosition = placementVector(relative.rotation(), relativePosition);
    for (size_t i = 0; i < 3; ++i)
      relativePosition[i] += relative.position[i];
    rootRotation = placementProduct(rootRotation, relative.rotation());
  }
  const auto offset = placementVector(rootRotation, relativePosition);
  const physics::Vec3 rootPosition{position[0] - offset[0], position[1] - offset[1], position[2] - offset[2]};
  if (!finite(rootPosition) || !finite(rootRotation)) {
    error = "part placement: transformed pose is nonfinite";
    return false;
  }
  auto& root = *tree.front();
  auto& state = root.state[root.current];
  float rotationDifference = 0;
  for (const size_t index : {1u, 0u, 2u, 4u, 5u, 6u, 8u, 9u, 10u}) {
    const float delta = rootRotation[index] - state.rot[index];
    rotationDifference += delta * delta;
  }
  const physics::Vec3 delta{state.pos[0] - rootPosition[0], state.pos[1] - rootPosition[1], state.pos[2] - rootPosition[2]};
  const bool rotate = rotationDifference > 9.999999e-9f;
  const bool translate = delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2] > 1.0000001e-6f;
  if (rotate || translate) {
    if (rotate) {
      state.quat = physics::quatFromMatrix(rootRotation);
      state.rot = physics::matrixFromQuat(state.quat);
    }
    if (translate)
      state.pos = rootPosition;
    physics::updateWorldInertia(*root.proto, state);
    auto copyPose = [](physics::State& destination, const physics::State& source) {
      destination.pos = source.pos;
      destination.quat = source.quat;
      destination.rot = source.rot;
    };
    copyPose(root.state[root.current ^ 1], state);
    physics::updateWorldInertia(*root.proto, root.state[root.current ^ 1]);
    // 005030b0 republishes child poses even for sleeping children; it does
    // not replace their unrelated velocity/force fields or current index.
    for (size_t i = 1; i < tree.size(); ++i) {
      auto& child = *tree[i];
      auto& current = child.state[child.current];
      const auto& parent = child.attachment->parent->state[child.attachment->parent->current];
      const auto& relative = child.attachment->relative;
      const auto point = placementVector(parent.rot, relative.position);
      for (size_t axis = 0; axis < 3; ++axis)
        current.pos[axis] = parent.pos[axis] + point[axis];
      current.quat = placementQuaternion(parent.quat, relative.orientation());
      current.rot = physics::matrixFromQuat(current.quat);
      current.time = parent.time;
      copyPose(child.state[child.current ^ 1], current);
    }
  }
  // 004c6370's six-float request leaves both optional velocity vectors zero.
  state.vel = {};
  state.omega = {};
  state.angMom = {};
  if (!(root.flags & physics::kBodyAsleep))
    for (auto* body : tree)
      physics::sleepBodyState(*body);
  affected = std::move(tree);
  return true;
}

PartPhysics::~PartPhysics() {
  while (!installedChildren.empty())
    (void)detachPartPhysics(*installedChildren.back());
  (void)detachPartPhysics(*this);
}

bool wheelReferencePose(const PartPhysics& target, int32_t slotType,
                        physics::State& out) {
  const uint32_t type = static_cast<uint32_t>(slotType) & 0xffffu;
  if (type <= 100u || type >= 0x191u)
    return false;
  const char* decline = nullptr;
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
  const auto& body = *wheel.hubBody;
  runlog::event(runlog::EventKind::Note,
                "wheelref pose: slot=%u i=%zu anchor=%.3f,%.3f,%.3f bone=%.3f,%.3f,%.3f center=%.3f,%.3f,%.3f hubY=%.3f rootY=%.3f",
                type, index, wheel.anchor[0], wheel.anchor[1], wheel.anchor[2],
                wheel.config.bonePosition[0], wheel.config.bonePosition[1], wheel.config.bonePosition[2],
                target.indexedCar->center[0], target.indexedCar->center[1], target.indexedCar->center[2],
                body.state[body.current].pos[1], target.indexedCar->root->state[target.indexedCar->root->current].pos[1]);
  // The neutral reference record must not acquire the hub's live travel or
  // spin: installation alignment is separate from the moving slot parent.
  const auto* main = target.rootGeometryBody();
  if (!main || main->current > 1) return false;
  out = main->state[main->current];
  const auto& position = target.indexedCar->wheels[index]->config.position;
  out.pos = physics::transformPoint(out.pos, out.rot, {position[0], position[1], position[2]});
  return true;
}

bool slotReferencePose(const PartPhysics& target, int32_t slotType,
                       physics::State& out) {
  if (wheelReferencePose(target, slotType, out))
    return true;
  const auto* body = target.rootGeometryBody();
  if (!body)
    body = target.root.get();
  if (!body || !body->proto)
    return false;
  out = body->state[body->current];
  return true;
}

physics::Vec3 slotReferencePosition(const PartPhysics& target, int32_t slotType,
                                    const PartSlotState& slot) {
  physics::State pose{};
  if (!slotReferencePose(target, slotType, pose))
    return {0, 0, 0};
  return physics::transformPoint(pose.pos, pose.rot, slot.position);
}

bool attachPartPhysics(PartPhysics& part, const PartSlotState& partSlot,
                       PartPhysics& target, const PartSlotState& targetSlot,
                       int32_t targetSlotType, std::string* detail) {
  if (detail)
    detail->clear();
  const auto failed = [&](const std::string& reason) {
    if (detail)
      *detail = reason;
    runlog::event(runlog::EventKind::Note,
                  "part physics attach refused: %s parent_shape=%u parent_flags=0x%x",
                  reason.c_str(), static_cast<unsigned>(target.rootPrototype.shape.type),
                  target.rootPrototype.flags);
    return false;
  };
  if (!part.root || !target.root || part.installedParent ||
      part.root->attachment)
    return failed("missing root or already attached part");
  for (auto* ancestor = &target; ancestor; ancestor = ancestor->installedParent)
    if (ancestor == &part)
      return failed("attachment cycle");
  auto* slotBody = target.rootGeometryBody();
  if (!slotBody)
    slotBody = target.root.get();
  physics::State targetPose = slotBody->state[slotBody->current];
  physics::State wheelPose{};
  if (wheelReferencePose(target, targetSlotType, wheelPose))
    targetPose = wheelPose;
  auto* bodyParent = slotBody;
  // FUN_0044a250 binds slot types 101/201/301 to the wheelbone/hub/a_arm
  // resources (+0x0c/+0x1c/+0x2c in the wheel record), respectively.
  const uint32_t type = static_cast<uint32_t>(targetSlotType) & 0xffffu;
  if (target.indexedCar && type > 100 && type <= 400) {
    const size_t index = (type - 1) % 100;
    if (index >= target.indexedCar->wheels.size() || !target.indexedCar->wheels[index])
      return failed("missing wheel slot record");
    auto& wheel = *target.indexedCar->wheels[index];
    bodyParent = type <= 200   ? wheel.boneBody.get()
                 : type <= 300 ? wheel.hubBody.get()
                               : wheel.armBody.get();
    if (!bodyParent || bodyParent->current > 1) return failed("invalid wheel slot body");
    // 00472e80 uses the returned slot resource body's current pose, not the
    // neutral record coordinates returned alongside it by getInfo(0x2f).
    targetPose = bodyParent->state[bodyParent->current];
  }
  const auto aligned = alignPartSlots(targetPose, targetSlot, partSlot);
  if ((bodyParent->flags & 0x8000u) == 0 && bodyParent->attachment)
    bodyParent = bodyParent->attachment->parent;
  movePartTree(part, aligned.position, aligned.rotation);
  if (!physics::attachBody(*part.root, *bodyParent))
    return failed("physical attachment cycle");
  if (type > 100 && type <= 400 && target.indexedCar) {
    const auto& relative = part.root->attachment->relative;
    runlog::event(runlog::EventKind::Note,
                  "wheel slot attachment: type=%u target=(%.4f,%.4f,%.4f) helper=(%.4f,%.4f,%.4f) relative=(%.4f,%.4f,%.4f)",
                  type, aligned.position[0], aligned.position[1], aligned.position[2],
                  bodyParent->state[bodyParent->current].pos[0], bodyParent->state[bodyParent->current].pos[1], bodyParent->state[bodyParent->current].pos[2],
                  relative.position[0], relative.position[1], relative.position[2]);
  }
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
      return failed(error);
    }
  }
  std::string error;
  if (!finalizeInstalledParent(target, *bodyParent, error)) {
    detachPartPhysics(part);
    return failed(error);
  }
  return true;
}
