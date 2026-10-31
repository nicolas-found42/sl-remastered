#include "car_physics.hpp"
#include "physics_compound_finalize.hpp"
#include "physics_compound_step.hpp"
#include "car_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

CarPhysics::~CarPhysics() {
  if (part) while (!part->installedChildren.empty()) detachPartPhysics(*part->installedChildren.back());
  for (auto* body : teardownOrder) {
    const auto observation = bodyObservations.find(body);
    if (observation != bodyObservations.end()) {
      setLinkOwner(observation->second->resource, nullptr);
      observation->second->body = nullptr;
    }
    for (auto& wheel : wheels) {
      if (wheel->suspension.wheelBody == body) wheel->suspension.wheelBody = nullptr;
      if (wheel->suspension.contactBody == body) wheel->suspension.contactBody = nullptr;
      if (wheel->suspensionBody.get() == body) wheel->suspensionBody.reset();
      if (wheel->hubBody.get() == body) wheel->hubBody.reset();
      if (wheel->armBody.get() == body) wheel->armBody.reset();
      if (wheel->boneBody.get() == body) wheel->boneBody.reset();
    }
    if (part) {
      for (auto& child : part->children)
        if (child.body.get() == body) { child.body.reset(); child.prototype.reset(); }
      if (part->root.get() == body) part->root.reset();
    }
    if (root.get() == body) root.reset();
  }
}

std::unique_ptr<CarPhysics> buildCarPhysics(std::unique_ptr<PartPhysics> part,
    float configuredMass, std::string& error) {
  error.clear();
  if (!part || !part->root || part->root->attachment || part->root->proto != &part->rootPrototype) {
    error = "expected a detached part root";
    return {};
  }
  auto proto = physics::createBodyProto(1, 5, 1, 1, 1, nullptr, error);
  if (!proto) return {};
  auto car = std::make_unique<CarPhysics>();
  car->rootPrototype = std::move(*proto);
  const auto& pose = part->root->state[part->root->current];
  car->root = std::make_unique<physics::Body>(physics::createBody(car->rootPrototype, pose.pos, &pose.rot, pose.time));
  car->part = std::move(part);
  car->part->indexedCar = car.get();
  if (!physics::attachBody(*car->part->root, *car->root)) {
    error = "invalid car root attachment";
    return {};
  }
  car->mass = configuredMass <= 0 ? 1.0f / car->part->rootPrototype.invMass : configuredMass;
  if (!indexCarBodies(*car, error)) return {};
  return car;
}

bool indexCarBodies(CarPhysics& car, std::string& error) {
  if (!car.part) { error = "car part compound is unavailable"; return false; }
  std::vector<physics::Body*> bodies{car.root.get(), car.part->root.get()};
  for (auto& child : car.part->children) bodies.push_back(child.body.get());
  std::vector<PartPhysics*> installed;
  std::function<void(PartPhysics&)> collectInstalled = [&](PartPhysics& parent) {
    for (auto* child : parent.installedChildren) {
      installed.push_back(child);
      collectInstalled(*child);
    }
  };
  collectInstalled(*car.part);
  std::sort(installed.begin(), installed.end(), [](const auto* a, const auto* b) {
    return a->attachmentSequence < b->attachmentSequence;
  });
  for (auto& wheel : car.wheels) {
    if (!wheel) { error = "missing wheel runtime"; return false; }
    bodies.push_back(wheel->suspensionBody.get());
    if (wheel->boneBody) bodies.push_back(wheel->boneBody.get());
    bodies.push_back(wheel->hubBody.get());
    bodies.push_back(wheel->armBody.get());
  }
  for (auto* part : installed) {
    bodies.push_back(part->root.get());
    for (auto& geometry : part->children) bodies.push_back(geometry.body.get());
  }
  decltype(car.bodyChildren) index;
  for (auto* body : bodies)
    if (!body || !body->proto || !index.emplace(body, std::vector<physics::Body*>{}).second) {
      error = "invalid or duplicate car body"; return false;
    }
  if (car.root->attachment) { error = "car root is attached"; return false; }
  for (size_t i = 1; i < bodies.size(); ++i) {
    auto* body = bodies[i];
    if (!body->attachment) { error = "car body is detached"; return false; }
    auto parent = index.find(body->attachment->parent);
    if (parent == index.end()) { error = "car body parent is outside the compound"; return false; }
    auto* cursor = body;
    size_t depth = 0;
    while (cursor != car.root.get()) {
      if (!cursor->attachment || index.find(cursor->attachment->parent) == index.end() ||
          ++depth >= bodies.size()) { error = "invalid car body parent chain"; return false; }
      cursor = cursor->attachment->parent;
    }
    parent->second.push_back(body);
  }
  decltype(car.bodyObservations) observations;
  for (auto* body : bodies) {
    const auto old = car.bodyObservations.find(body);
    auto observation = old == car.bodyObservations.end() ? std::make_shared<CarBodyObservation>() : old->second;
    observation->body = body;
    observations.emplace(body, std::move(observation));
  }
  std::vector<physics::Body*> order;
  order.reserve(bodies.size());
  std::function<void(physics::Body*)> visit = [&](physics::Body* current) {
    const auto& children = index.at(current);
    for (auto it = children.rbegin(); it != children.rend(); ++it) visit(*it);
    order.push_back(current);
  };
  visit(car.root.get());
  for (const auto& old : car.bodyObservations) {
    if (observations.find(old.first) == observations.end()) {
      setLinkOwner(old.second->resource, nullptr);
      old.second->body = nullptr;
    }
  }
  car.bodyChildren = std::move(index);
  car.bodyObservations = std::move(observations);
  car.teardownOrder = std::move(order);
  return true;
}

bool finalizeCarPhysics(CarPhysics& car, std::string& error) {
  if (!car.part || !car.part->root || !car.root) { error = "car part compound is unavailable"; return false; }
  std::vector<physics::Body*> children{car.part->root.get()};
  for (auto& wheel : car.wheels) {
    children.push_back(wheel->suspensionBody.get());
    if (wheel->boneBody) children.push_back(wheel->boneBody.get());
    children.push_back(wheel->hubBody.get());
    children.push_back(wheel->armBody.get());
  }
  return finalizeCompound(*car.root, car.rootPrototype, children, error);
}

std::unique_ptr<CarPhysics> buildDetachedCarPhysics(CarPrototype& prototype,
    std::string_view arguments, float creationTime, const physics::Vec3& gravity,
    std::string& error) {
  auto part = buildPartPhysics(prototype.config, prototype.geometry, arguments, error,
      prototype.clickGeometry ? &*prototype.clickGeometry : nullptr, creationTime);
  if (!part) return {};
  auto car = buildCarPhysics(std::move(part), prototype.config.centerMass, error);
  if (!car) return {};
  const auto* mainBody = car->part->rootGeometryBody();
  if (!mainBody || !mainBody->attachment) {
    error = "car main body attachment is unavailable";
    return {};
  }
  const auto& center = car->part->rootPrototype.shape.centerOffset;
  car->wheels = buildCarWheelRuntime(prototype.wheels, *car->root, *mainBody,
      {center[0], center[1], center[2]}, mainBody->attachment->relative.position,
      car->mass, gravity, error);
  if (!error.empty() || !finalizeCarPhysics(*car, error) || !indexCarBodies(*car, error)) return {};
  return car;
}

namespace {
void sleepCarTree(const CarPhysics& car, physics::Body* body) {
  if (body->flags & physics::kBodyAsleep) return;
  physics::sleepBodyState(*body);
  if (auto it = car.bodyChildren.find(body); it != car.bodyChildren.end())
    for (auto* child : it->second) sleepCarTree(car, child);
}
}  // namespace

namespace {
void collectCarTree(const CarPhysics& car, physics::Body* body,
                    std::vector<physics::Body*>& tree) {
  tree.push_back(body);
  if (auto it = car.bodyChildren.find(body); it != car.bodyChildren.end())
    for (auto* child : it->second) collectCarTree(car, child, tree);
}
}  // namespace

void resetCarToRest(CarPhysics& car) {
  if (!car.root) return;
  auto& state = car.root->state[car.root->current];
  state.vel = {};
  state.omega = {};
  state.angMom = {};
  state.force = {};
  state.torque = {};
  // 00458c00 `reset`: each wheel's suspension returns to its static preload,
  // length = (record +0x60 + +0x50) - (|g| / invMass / wheels) / (+0x44).
  // +0x44 is the spring stiffness handed to 00491f80 (the port's
  // springForce, in N/m), so the quotient is the static compression in metres
  // (equal to spring[1] at creation). [INFERENCE] +0x60 + +0x50 is
  // initialLength (radius + spring[0]).
  const float weightPerWheel =
      std::fabs(physics::k::gravityY) * car.mass /
      static_cast<float>(std::max<size_t>(car.wheels.size(), 1));
  for (auto& wheel : car.wheels) {
    if (!wheel) continue;
    wheel->suspension.length =
        wheel->initialLength - weightPerWheel / wheel->springForce;
    wheel->suspension.force = 0;
    wheel->angularSpeed = 0;
    wheel->longitudinalVelocity = 0;
    wheel->longitudinalSlip = 0;
    wheel->slipAngle = 0;
  }
  sleepCarTree(car, car.root.get());
}

void suspendCar(CarPhysics& car) {
  if (car.root) sleepCarTree(car, car.root.get());
}

void wakeCar(CarPhysics& car) {
  if (!car.root) return;
  std::vector<physics::Body*> tree;
  collectCarTree(car, car.root.get(), tree);
  physics::wakeDetachedCompound(tree);
}
