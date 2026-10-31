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

// Car root and part compound. GameWorld can register the root and selected
// body resources; wheel joints and the complete world step remain absent.
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
// 'reset' car command (stock branch of the 00457xxx command parser): zero the
// root's linear and angular velocity, then put the whole body tree to sleep
// (004a7a80). Remaining reset fields (wheel slip/state, steering geometry,
// controller state) are not ported; the caller logs that gap.
void resetCarToRest(CarPhysics& car);
// 00458c00 `suspend` / `wakeup`: sleep or wake the car's whole body tree.
void suspendCar(CarPhysics& car);
void wakeCar(CarPhysics& car);
bool finalizeCarPhysics(CarPhysics& car, std::string& error);
std::unique_ptr<CarPhysics> buildDetachedCarPhysics(CarPrototype& prototype,
    std::string_view arguments, float creationTime, const physics::Vec3& gravity,
    std::string& error);
