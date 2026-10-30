#pragma once

#include "part_physics.hpp"
#include "car_wheel_runtime.hpp"
#include "owner_links.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

struct CarPrototype;

// The resource link and body observation are invalidated on car teardown.
// Observations do not keep the physics body or car alive.
struct CarBodyObservation {
  physics::Body* body = nullptr;
  OwnerLink resource;
  ~CarBodyObservation() { setLinkOwner(resource, nullptr); }
};

// Detached car root and its existing part compound. World registration and
// joints are absent.
struct CarPhysics {
  CarPhysics() = default;
  ~CarPhysics();
  CarPhysics(const CarPhysics&) = delete;
  CarPhysics& operator=(const CarPhysics&) = delete;
  physics::BodyProto rootPrototype;
  std::unique_ptr<physics::Body> root;
  std::unique_ptr<PartPhysics> part;
  std::vector<std::unique_ptr<CarWheelRuntime>> wheels;
  // Non-owning index into the bodies held by this car, part, and wheels.
  // It describes detached construction only; it is not a world or teardown API.
  std::unordered_map<const physics::Body*, std::vector<physics::Body*>> bodyChildren;
  std::unordered_map<const physics::Body*, std::shared_ptr<CarBodyObservation>> bodyObservations;
  std::vector<physics::Body*> teardownOrder;
  float mass = 0;
};

// 0044a250: create the box root at the part root's pose and reparent the part.
// A nonpositive configured mass falls back to the part's prototype mass.
std::unique_ptr<CarPhysics> buildCarPhysics(std::unique_ptr<PartPhysics> part,
    float configuredMass, std::string& error);
bool indexCarBodies(CarPhysics& car, std::string& error);
bool finalizeCarPhysics(CarPhysics& car, std::string& error);
std::unique_ptr<CarPhysics> buildDetachedCarPhysics(CarPrototype& prototype,
    std::string_view arguments, float creationTime, const physics::Vec3& gravity,
    std::string& error);
