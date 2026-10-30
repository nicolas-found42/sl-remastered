#pragma once
#include "physics_body.hpp"
#include <cstdint>
namespace physics {
// Type 6 contacts resolved to awake root bodies. Unsupported
// inputs are left untouched.
struct DirectContact {
  Body* a = nullptr;
  Body* b = nullptr;
  Vec3 normal{};
  float separation = 0;  // stock contact +0x8c
  Vec3 pointA{}, pointB{}; // local offsets, +0x5c and +0x68
  float friction = 0; // +0x94
  float bounce = 0; // +0x98
  float normalImpulse = 0; // +0xa0
  float frictionForce = 0; // +0xa8
};
struct DirectContactWorld { uint32_t corrections = 0; float step = 0; }; // world +0x14, +0x0c
// -1: unsupported; otherwise the original routine's result (0 or 1).
int correctDirectContact(DirectContact& contact, DirectContactWorld& world);
}
