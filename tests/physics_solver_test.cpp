#include "physics_solver.hpp"
#include <cassert>
#include <cmath>
using namespace physics;
int main() {
  BodyProto p; p.invMass = 1;
  BodyProto wall; wall.flags = 4;
  Body a; a.proto = &p;
  Body b; b.proto = &wall;
  a.state[0].vel = {-2, 0, 0}; a.state[1].vel = {-2, 0, 0};
  std::vector<Contact> contacts{{&a, &b, {1, 0, 0}, 0.5f}};
  assert(solveContacts(contacts) == ContactSolveResult::Solved);
  assert(std::abs(a.state[0].vel[0] - 0.7f) < 1e-6f);
  assert(b.state[0].vel[0] == 0);
  assert(a.state[1].vel[0] == -2);
  Body c; c.proto = &p;
  a.state[0].vel = {-2, 0, 0};
  c.state[0].vel = {0, 0, 0}; c.state[1].vel = {0, 0, 0};
  contacts = {{&a, &c, {1, 0, 0}, 0}};
  assert(solveContacts(contacts) == ContactSolveResult::Solved);
  assert(std::abs(a.state[0].vel[0] + 1.1f) < 1e-6f);
  assert(std::abs(c.state[0].vel[0] + 0.9f) < 1e-6f);
  a.state[0].vel = {-2, 0, 0}; a.state[1].vel = {-2, 0, 0};
  a.state[0].invInertiaWorld = matrixIdentity();
  contacts = {{&a, &b, {1, 0, 0}, 0, {0, 1, 0}, {}}};
  assert(solveContacts(contacts) == ContactSolveResult::Solved);
  assert(std::abs(a.state[0].vel[0] + 1.1f) < 1e-6f);
  assert(std::abs(a.state[0].omega[2] + 0.9f) < 1e-6f);
  a.state[0].vel = {-0.1f, 0, 0}; a.state[1].vel = {-0.1f, 0, 0};
  a.state[0].angMom = {}; a.state[0].omega = {};
  contacts = {{&a, &b, {1, 0, 0}, 0}};
  assert(solveContacts(contacts) == ContactSolveResult::Solved);
  assert(a.state[0].vel[0] == -0.1f);
  assert(contacts[0].impulse == 0);
  a.state[0].vel = {-2, 0, 0}; a.state[1].vel = {-2, 0, 0};
  contacts = {{&a, &b, {1, 0, 0}, 0}, {&a, &b, {1, 0, 0}, 0}};
  assert(solveContacts(contacts) == ContactSolveResult::Solved);
  assert(std::abs(a.state[0].vel[0] - 1.6f) < 1e-6f);
  Body parent; parent.proto = &p;
  parent.state[1].vel = {-2, 0, 0};
  parent.state[1].omega = {0, 0, 1};
  Body child; child.proto = &p; child.flags = kBodyChild;
  child.attachment = BodyAttachment{&parent, {{0, 1, 0}, {0, 0, 0, 1}}};
  child.state[0].invInertiaWorld = matrixIdentity();
  contacts = {{&child, &b, {1, 0, 0}, 0, {0, 1, 0}, {}}};
  assert(solveContacts(contacts) == ContactSolveResult::Solved);
  assert(std::abs(contacts[0].target - 3.8f) < 1e-6f);
  assert(std::abs(child.state[0].vel[0] - 1.9f) < 1e-6f);
  assert(std::abs(child.state[0].omega[2] + 1.9f) < 1e-6f);
  assert(parent.state[0].vel[0] == 0);
  parent.flags = kBodyChild;
  parent.attachment = BodyAttachment{&child, {}};
  assert(solveContacts(contacts) == ContactSolveResult::InvalidBody);
  parent.attachment.reset();
  parent.flags = 0;
  child.attachment.reset();
  assert(solveContacts(contacts) == ContactSolveResult::InvalidBody);
}
