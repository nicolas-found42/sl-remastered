#include "wheel_tire_step.hpp"
#include "car_wheel_runtime.hpp"
#include "ground_config.hpp"
#include <algorithm>
#include <cmath>

bool stepTireContact(const WheelGroundContact &witness,
                     physics::SuspensionUpdate &contact, TireStepState &state,
                     const TireStepInput &input) {
  using namespace physics;
  const auto *candidate = input.retainedWitness ? witness.retainedCandidate()
                                                : witness.tireCandidate();
  if (!candidate || candidate->a != contact.wheelBody ||
      candidate->b != contact.contactBody ||
      candidate->otherOwner != contact.contactOwner ||
      candidate->otherPrototype != contact.contactPrototype ||
      candidate->surface != contact.contactSurface ||
      candidate->pointB != contact.contactPoint ||
      candidate->normal != contact.normal)
    return false;
  if (!contact.wheelBody || !contact.contactBody || !contact.contactOwner ||
      !contact.contactPrototype || !contact.contactSurface ||
      contact.contactOwner.get() != contact.contactBody ||
      contact.contactOwner->proto != contact.contactPrototype.get() ||
      input.surface.identity != contact.contactSurface ||
      contact.contactFeature != contact.contactSurface->featureIndex ||
      contact.wheelBody->current > 1 || contact.forcePointThreshold != 0 ||
      !std::isfinite(contact.force) || contact.force <= 0 ||
      !std::isfinite(state.longitudinalSlip) ||
      !std::isfinite(state.slipAngle) || !std::isfinite(state.angularSpeed) ||
      !std::isfinite(state.accumulatedTorque) ||
      !std::isfinite(input.surface.friction) ||
      !std::isfinite(input.surface.rolling) ||
      !std::isfinite(input.wheelRolling))
    return false;
  const Mat34 &r = contact.wheelBody->state[contact.wheelBody->current].rot;
  const Vec3 &n = contact.normal;
  Vec3 localNormal{n[0] * r[0] + n[1] * r[4] + n[2] * r[8],
                   n[0] * r[1] + n[1] * r[5] + n[2] * r[9],
                   n[0] * r[2] + n[1] * r[6] + n[2] * r[10]};
  const auto load = tireEffectiveLoad(
      {contact.force, input.maxLoad, input.loadSmoothing, localNormal});
  if (!load)
    return false;
  TireSlipForceInput tire = input.tire;
  tire.effectiveLoad = *load;
  tire.surfaceFriction = input.surface.friction;
  tire.angularSpeed = state.angularSpeed;
  tire.slip.previousLongitudinal = state.longitudinalSlip;
  tire.slip.previousAngle = state.slipAngle;
  const auto result = calculateTireSlipForce(tire);
  if (!result)
    return false;
  const float nextAngularSpeed = state.angularSpeed + result->angularDelta;
  const float nextTorque = state.accumulatedTorque + result->torqueDelta;
  if (!std::isfinite(nextAngularSpeed) || !std::isfinite(nextTorque))
    return false;
  if (result->magnitude > 0.0001f) {
    const auto force = wheelContactLocalForce(
        {localNormal, result->lateralForce, result->longitudinalForce, *load,
         tire.slip.roadSpeed, input.surface.rolling, input.wheelRolling});
    if (!force ||
        !routeTireForce(*contact.wheelBody, *force, contact.wheelPoint,
                        input.geometryCorrections, &input.geometry))
      return false;
  }
  state.longitudinalSlip = result->slip.longitudinal;
  state.slipAngle = result->slip.angle;
  state.slipMagnitude = result->magnitude;
  state.aligningForce = result->aligningForce;
  state.angularSpeed = nextAngularSpeed;
  state.accumulatedTorque = nextTorque;
  return true;
}

bool stepLiveGroundTire(const WheelGroundContact &witness,
                        CarWheelRuntime &wheel, const Wheel &coefficients,
                        const WheelSlipParameters &slipParameters,
                        float angularGain, float wheelRolling, float timeStep,
                        bool retainedWitness) {
  using namespace physics;
  auto &contact = wheel.suspension;
  const auto *candidate = retainedWitness ? witness.retainedCandidate()
                                          : witness.tireCandidate();
  const auto *material =
      retainedWitness ? witness.retainedMaterial() : witness.groundMaterial();
  if (!candidate || !material || !wheel.suspensionBody ||
      contact.wheelBody != wheel.suspensionBody.get() || !contact.contactBody ||
      !contact.contactBody->proto || contact.contactBody->attachment ||
      contact.contactBody->current > 1 || !contact.wheelBody->attachment ||
      !contact.wheelBody->attachment->parent ||
      contact.wheelBody->attachment->parent->attachment ||
      contact.wheelBody->attachment->parent->current > 1 ||
      !std::isfinite(contact.length) ||
      !std::isfinite(contact.settings.maxLength) ||
      !(contact.length < contact.settings.maxLength) ||
      !std::isfinite(timeStep) || timeStep < 0.0001f)
    return false;
  const auto pointVelocity = [](const Body &body, Vec3 point) {
    const Body *root = &body;
    if (body.attachment) {
      point = matrixTransformRows(body.attachment->relative.rotation(), point);
      for (size_t i = 0; i < 3; ++i)
        point[i] += body.attachment->relative.position[i];
      root = body.attachment->parent;
    }
    const auto &state = root->state[root->current];
    const auto offset = matrixTransformRows(state.rot, point);
    return Vec3{
        offset[2] * state.omega[1] - offset[1] * state.omega[2] + state.vel[0],
        offset[0] * state.omega[2] - offset[2] * state.omega[0] + state.vel[1],
        offset[1] * state.omega[0] - offset[0] * state.omega[1] + state.vel[2]};
  };
  const Vec3 groundVelocity =
      pointVelocity(*contact.contactBody, contact.contactPoint);
  const Vec3 wheelVelocity =
      pointVelocity(*contact.wheelBody, contact.wheelPoint);
  Vec3 relative{};
  for (size_t i = 0; i < 3; ++i)
    relative[i] = groundVelocity[i] - wheelVelocity[i];
  const auto &rotation =
      contact.wheelBody->state[contact.wheelBody->current].rot;
  const float lateral = relative[0] * rotation[0] + relative[1] * rotation[4] +
                        relative[2] * rotation[8];
  const float longitudinal = relative[0] * rotation[2] +
                             relative[1] * rotation[6] +
                             relative[2] * rotation[10];
  TireStepInput input{};
  std::copy_n(coefficients.pacejka.begin(), 11, input.tire.curve.begin());
  input.tire.slip = {0, longitudinal, lateral, timeStep, 0, 0};
  input.tire.slipParameters = slipParameters;
  input.tire.wheelFriction = coefficients.friction6c;
  input.tire.stiffnessNumerator = coefficients.pacejka[15];
  input.tire.stiffnessDenominator = coefficients.pacejka[16];
  input.tire.longitudinalScale = coefficients.pacejka[12];
  input.tire.radius = wheel.radius;
  input.tire.inverseWheelInertia = wheel.effectiveInverseInertia;
  input.tire.angularGain = angularGain;
  input.surface = {candidate->surface, material->friction,
                   material->rollingResistance};
  input.maxLoad = coefficients.pacejka[11];
  input.loadSmoothing = coefficients.pacejka[14];
  input.wheelRolling = wheelRolling;
  input.geometryCorrections = TireGeometryCorrections::Resolved;
  input.retainedWitness = retainedWitness;
  input.geometry = {wheel.anchor,         wheel.instantCenterA,
                    wheel.instantCenterB, wheel.radius,
                    contact.length,       contact.settings.maxLength};
  TireStepState state{wheel.longitudinalSlip, wheel.slipAngle,
                      wheel.angularSpeed,     wheel.longitudinalVelocity,
                      wheel.slipMagnitude,    wheel.aligningForce};
  if (!stepTireContact(witness, contact, state, input))
    return false;
  wheel.longitudinalSlip = state.longitudinalSlip;
  wheel.slipAngle = state.slipAngle;
  wheel.angularSpeed = state.angularSpeed;
  wheel.longitudinalVelocity = state.accumulatedTorque;
  wheel.slipMagnitude = state.slipMagnitude;
  wheel.aligningForce = state.aligningForce;
  return true;
}
