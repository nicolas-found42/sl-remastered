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
  car->center = prototype.config.center;
  if (!error.empty() || !finalizeCarPhysics(*car, error) || !indexCarBodies(*car, error)) return {};
  return car;
}

bool carRideHeightPlacement(const CarPhysics& car,
                            const std::array<float, 3>& position,
                            const std::array<float, 3>& orientation,
                            std::array<float, 3>& placedPosition,
                            std::array<float, 3>& placedOrientation) {
  const auto rotation = [](const std::array<float, 3>& angles,
                           std::array<float, 9>& r) {
    PartSlotDirective directive{};
    for (size_t i = 0; i < 3; ++i) directive.orientation[i] = angles[i];
    PartSlotState slot;
    if (!makePartSlotState(directive, slot)) return false;
    r = slot.rotation;
    return true;
  };
  placedPosition = position;
  placedOrientation = orientation;
  std::array<float, 9> m{};
  if (!rotation(orientation, m)) return false;
  if (car.wheels.size() < 4) {
    // 0044c460 < 4 wheels: position += R * center.
    for (size_t i = 0; i < 3; ++i)
      placedPosition[i] = m[i * 3 + 2] * car.center[2] +
                          m[i * 3] * car.center[0] + m[i * 3 + 1] * car.center[1] +
                          position[i];
    return true;
  }
  // Static wheel-centre points: anchor with the suspension at the length
  // reset applies, (initialLength) - (|g| / invMass / wheels) / stiffness.
  // [INFERENCE] wheel record +0x50 + +0x60 is initialLength and +0x78..+0x80
  // is `anchor`; the 0.25 mean factor (_DAT_005f0c80) was not re-read.
  const float gravityLength = std::fabs(physics::k::gravityY);
  const float weight = gravityLength * car.mass;
  const float perWheel = weight / static_cast<float>(car.wheels.size());
  float w[4][3];
  for (size_t i = 0; i < 4; ++i) {
    const auto& wheel = *car.wheels[i];
    w[i][0] = wheel.anchor[0];
    w[i][2] = wheel.anchor[2];
    w[i][1] = wheel.anchor[1] -
              (wheel.initialLength - perWheel / wheel.springForce);
  }
  const float mean[3] = {(w[1][0] + w[0][0] + w[2][0] + w[3][0]) * 0.25f,
                         (w[2][1] + w[1][1] + w[0][1] + w[3][1]) * 0.25f,
                         (w[2][2] + w[1][2] + w[0][2] + w[3][2]) * 0.25f};
  const float rollHeight = (w[3][1] + (w[1][1] - w[0][1])) - w[2][1];
  const float pitchDepth = (w[3][2] + (w[2][2] - w[0][2])) - w[1][2];
  const long double pitch = atan2l(
      (((long double)w[2][1] - w[0][1]) + w[3][1] - w[1][1]) * 0.5L,
      (long double)pitchDepth * 0.5L);
  placedOrientation[1] = (float)(pitch + (long double)orientation[1]);
  const long double roll = atan2l(
      (long double)rollHeight * 0.5L,
      (((long double)w[1][0] - w[0][0]) + w[3][0] - w[2][0]) * 0.5L);
  placedOrientation[2] = (float)((long double)orientation[2] - roll);
  if (!rotation(placedOrientation, m)) return false;
  float offset[3];
  for (size_t i = 0; i < 3; ++i)
    offset[i] = mean[2] * m[i * 3 + 2] + mean[1] * m[i * 3 + 1] +
                mean[0] * m[i * 3];
  float lift = -(m[3] * car.center[0] + m[5] * car.center[2] +
                 m[4] * car.center[1]);
  if (offset[1] <= lift) lift = offset[1];
  placedPosition[0] = position[0] - offset[0];
  placedPosition[1] = position[1] - lift;
  placedPosition[2] = position[2] - offset[2];
  return true;
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
