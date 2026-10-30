#include "physics_suspension.hpp"

#include <cassert>
#include <cmath>

int main() {
  using namespace physics;
  auto s = configureSuspension(1000, 200, 0.9f, 0, -1, 0, 0);
  assert(s.maxLength == 0.9f && s.minLength == 0);
  assert(s.reboundDamping == 140 && s.auxiliaryLength == 0.9f && s.auxiliaryLimit == 0);
  auto explicitSettings = configureSuspension(1000, 200, 1, 2, 0.2f, 75, 1.5f);
  assert(explicitSettings.maxLength == 2 && explicitSettings.minLength == 0.2f);
  assert(explicitSettings.reboundDamping == 75 && explicitSettings.auxiliaryLimit == 1.5f);
  auto compressed = suspensionScalar({s, 0.25f, 0.6f, -0.1f, 0});
  assert(compressed.active && std::fabs(compressed.force - 440) < 0.001f);
  assert(std::fabs(compressed.dampingWork - 4) < 0.001f);
  auto rebound = suspensionScalar({s, 0.25f, 0.6f, 0.1f, 0});
  assert(rebound.active && std::fabs(rebound.force - 372) < 0.001f);
  auto separated = suspensionScalar({s, 1, 0.6f, 0, 0});
  assert(!separated.active && separated.force == 0 && separated.length == s.maxLength);
  auto negative = suspensionScalar({s, 0.25f, 0.6f, 10, 0});
  assert(!negative.active && negative.force == 0 && negative.dampingWork > 0);
  BodyProto wheelProto{}, groundProto{};
  Body wheel{}, ground{};
  wheel.proto = &wheelProto; ground.proto = &groundProto;
  SuspensionUpdate update{};
  update.settings = s; update.wheelBody = &wheel; update.contactBody = &ground;
  update.distanceSquared = 0.25f; update.length = 0.6f; update.normal = {0, 1, 0};
  assert(updateSuspension(update) == SuspensionUpdateResult::Active);
  assert(std::fabs(update.force - 400) < 0.001f);
  assert(wheel.state[0].force[1] == -ground.state[0].force[1]);
  assert(update.normal[1] == 1 && update.normalForce[1] == update.force);
  update.distanceSquared = 0.01f; update.settings.minLength = 0.2f;
  ground.state[0].vel[1] = 1;
  const float oldLength = update.length;
  assert(updateSuspension(update) == SuspensionUpdateResult::Unsupported);
  assert(update.length == oldLength && update.distanceSquared == 0.01f);
  wheelProto.invMass = groundProto.invMass = 1;
  update.timeStep = 0.1f;
  assert(updateSuspension(update) == SuspensionUpdateResult::Active);
  assert(wheel.state[0].force[1] > update.force);

  Body root{}, child{};
  root.proto = child.proto = &wheelProto;
  child.flags = kBodyChild;
  child.attachment = BodyAttachment{&root, {{1, 0, 0}, {0, 0, 0, 1}}};
  SuspensionUpdate attached{};
  attached.settings = s; attached.wheelBody = &child; attached.contactBody = &ground;
  attached.distanceSquared = 0.25f; attached.length = 0.6f; attached.normal = {0, 1, 0};
  ground.state[0].vel = {};
  assert(updateSuspension(attached) == SuspensionUpdateResult::Active);
  assert(root.state[0].force[1] == attached.force && child.state[0].force[1] == 0);
  assert(root.state[0].torque[2] == attached.force);
  child.attachment->parent = &child;
  assert(updateSuspension(attached) == SuspensionUpdateResult::Unsupported);

  auto contactProto = std::make_shared<BodyProto>();
  contactProto->flags = kProtoStatic;
  auto contact = std::make_shared<Body>();
  contact->proto = contactProto.get();
  auto surface = std::make_shared<SuspensionContactSurface>();
  surface->featureIndex = 4;
  surface->materialWord = 6;
  SuspensionUpdate selection{};
  selection.wheelBody = &wheel;
  selection.anchor = {0, 1, 0};
  selection.length = 1;
  selection.settings.auxiliaryLength = 1;
  selection.distanceSquared = 4;
  SuspensionContactCandidate candidate;
  candidate.a = &wheel;
  candidate.b = contact.get();
  candidate.pointA = {0, 0.5f, 0};
  candidate.pointB = {2, 3, 4};
  candidate.normal = {0, 1, 0};
  candidate.otherPrototype = contactProto;
  candidate.otherOwner = contact;
  candidate.surface = surface;
  assert(selectSuspensionContact(selection, candidate) == SuspensionContactResult::Selected);
  assert(selection.distanceSquared == 0.25f && selection.contactBody == contact.get() &&
         selection.contactPoint == candidate.pointB && selection.normal == candidate.normal &&
         selection.contactFeature == 4 && selection.contactSurface->materialWord == 6);
  candidate.pointB = {9, 9, 9};
  assert(selectSuspensionContact(selection, candidate) == SuspensionContactResult::Rejected);
  assert(selection.contactPoint == Vec3({2,3,4})); // equal-distance candidate cannot replace it
  selection.length = 3;
  candidate.pointA = {0, 0.75f, 0};
  assert(selectSuspensionContact(selection, candidate) == SuspensionContactResult::Rejected);
  selection.length = 1;
  candidate.a = contact.get(); candidate.b = &wheel;
  candidate.pointA = {7, 8, 9}; candidate.pointB = {0, 0.75f, 0};
  candidate.surface.reset();
  assert(selectSuspensionContact(selection, candidate) == SuspensionContactResult::Selected);
  assert(selection.contactPoint == Vec3({7,8,9}) && selection.contactFeature == 0 &&
         selection.contactSurface == surface);
  auto sameRoot = std::make_shared<Body>();
  sameRoot->proto = contactProto.get();
  sameRoot->flags = kBodyChild;
  sameRoot->attachment = BodyAttachment{&wheel, {}};
  candidate.a = sameRoot.get(); candidate.otherOwner = sameRoot;
  assert(selectSuspensionContact(selection, candidate) == SuspensionContactResult::Rejected);
  const auto saved = selection.distanceSquared;
  candidate.a = contact.get(); candidate.otherOwner = contact;
  candidate.otherPrototype.reset();
  assert(selectSuspensionContact(selection, candidate) == SuspensionContactResult::Unsupported &&
         selection.distanceSquared == saved);
  candidate.otherOwner.reset(); contact.reset(); contactProto.reset();
  assert(selection.contactOwner && selection.contactPrototype &&
         selection.contactBody == selection.contactOwner.get() && selection.contactBody->proto == selection.contactPrototype.get());
}
