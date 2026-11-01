#pragma once

#include "car_wheel_geometry.hpp"
#include "physics_compound.hpp"
#include "physics_suspension.hpp"
#include <memory>
#include <string>
#include <vector>

// Detached wheel bodies and suspension state. Scene/world registration and
// the remaining wheel constraints are not implemented.
struct CarWheelRuntime {
  // The suspension and bodies hold pointers to other members of this object.
  // Keep their addresses stable for the lifetime of the wheel.
  CarWheelRuntime() = default;
  CarWheelRuntime(const CarWheelRuntime &) = delete;
  CarWheelRuntime &operator=(const CarWheelRuntime &) = delete;
  CarWheelRuntime(CarWheelRuntime &&) = delete;
  CarWheelRuntime &operator=(CarWheelRuntime &&) = delete;

  CarWheelPrototype config;
  physics::BodyProto suspensionPrototype;
  std::unique_ptr<physics::Body> suspensionBody;
  std::unique_ptr<physics::Body> hubBody;
  std::unique_ptr<physics::Body> armBody;
  std::unique_ptr<physics::Body> boneBody;
  physics::SuspensionUpdate suspension;
  physics::Mat34 baseSteeringRotation = physics::matrixIdentity();
  physics::Vec3 anchor{};
  float rotationAngle = 0; // wheel record +0xe8, radians
  physics::Vec3 instantCenterA{};
  physics::Vec3 instantCenterB{};
  float restLength = 0;
  float springForce = 0;
  float damping = 0;
  float reboundDamping = 0;
  float minimumLength = 0;
  float maximumLength = 0;
  float initialLength = 0;
  float radius = 0;
  // Live 00454500 wheel rotational state, retained across physics steps.
  float angularSpeed = 0;            // +0xc0, radians/second
  float transferCoefficient = 0;     // +0xc4
  float longitudinalVelocity = 0;    // +0x148
  float effectiveInverseInertia = 0; // +0xec
  float longitudinalSlip = 0;        // +0x14c
  float slipAngle = 0;               // +0x150
  float slipMagnitude = 0;           // +0x154
  float aligningForce = 0;           // +0x158
};

// 0044f4e0/00492030: updates only the suspension body's local attachment
// orientation. World pose propagation and physics wake-up are separate.
bool applyWheelSteeringOrientation(CarWheelRuntime &wheel, float angle);

// partCenter and partOffset are in the car root's local coordinates. mass is
// the stock car mass, after its fallback to inverse part prototype mass.
// The caller must create and attach the car root before calling this function.
std::vector<std::unique_ptr<CarWheelRuntime>>
buildCarWheelRuntime(const std::vector<CarWheelPrototype> &prototypes,
                     physics::Body &carRoot, const physics::Body &partBody,
                     const physics::Vec3 &partCenter,
                     const physics::Vec3 &partOffset, float mass,
                     const physics::Vec3 &gravity, std::string &error);

// FUN_00454500: advances +0xe8 from angular speed, retaining the stock single
// +/-2pi wrap. Called after drivetrain/brakes and before the tire callback.
bool advanceWheelRotation(CarWheelRuntime& wheel, float seconds);
// FUN_0044fa70 default arm/hub branch. Updates helper attachment frames; the
// caller refreshes their committed descendants (FUN_004a7880) for rendering.
// Custom arm/hub linkage geometry remains explicitly unsupported.
bool updateWheelPose(CarWheelRuntime& wheel);
