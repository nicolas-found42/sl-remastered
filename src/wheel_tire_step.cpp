#include "wheel_tire_step.hpp"
#include <cmath>

bool stepTireContact(const WheelGroundContact& witness, physics::SuspensionUpdate& contact, TireStepState& state,
                     const TireStepInput& input) {
  using namespace physics;
  const auto* candidate = witness.candidate();
  if (!candidate || candidate->a != contact.wheelBody || candidate->b != contact.contactBody ||
      candidate->otherOwner != contact.contactOwner || candidate->otherPrototype != contact.contactPrototype ||
      candidate->surface != contact.contactSurface || candidate->pointB != contact.contactPoint ||
      candidate->normal != contact.normal) return false;
  if (!contact.wheelBody || !contact.contactBody || !contact.contactOwner ||
      !contact.contactPrototype || !contact.contactSurface ||
      contact.contactOwner.get() != contact.contactBody ||
      contact.contactOwner->proto != contact.contactPrototype.get() ||
      input.surface.identity != contact.contactSurface ||
      contact.contactFeature != contact.contactSurface->featureIndex ||
      contact.wheelBody->current > 1 || contact.forcePointThreshold != 0 || !std::isfinite(contact.force) || contact.force <= 0 ||
      !std::isfinite(state.longitudinalSlip) || !std::isfinite(state.slipAngle) ||
      !std::isfinite(state.angularSpeed) || !std::isfinite(state.accumulatedTorque) ||
      !std::isfinite(input.surface.friction) || !std::isfinite(input.surface.rolling) ||
      !std::isfinite(input.wheelRolling)) return false;
  const Mat34& r = contact.wheelBody->state[contact.wheelBody->current].rot;
  const Vec3& n = contact.normal;
  Vec3 localNormal{n[0]*r[0] + n[1]*r[4] + n[2]*r[8],
                   n[0]*r[1] + n[1]*r[5] + n[2]*r[9],
                   n[0]*r[2] + n[1]*r[6] + n[2]*r[10]};
  const auto load = tireEffectiveLoad({contact.force,input.maxLoad,input.loadSmoothing,localNormal});
  if (!load) return false;
  TireSlipForceInput tire = input.tire;
  tire.effectiveLoad = *load;
  tire.surfaceFriction = input.surface.friction;
  tire.angularSpeed = state.angularSpeed;
  tire.slip.previousLongitudinal = state.longitudinalSlip;
  tire.slip.previousAngle = state.slipAngle;
  const auto result = calculateTireSlipForce(tire);
  if (!result) return false;
  const float nextAngularSpeed = state.angularSpeed + result->angularDelta;
  const float nextTorque = state.accumulatedTorque + result->torqueDelta;
  if (!std::isfinite(nextAngularSpeed) || !std::isfinite(nextTorque)) return false;
  if (result->magnitude > 0.0001f) {
    const auto force = wheelContactLocalForce({localNormal,result->lateralForce,
        result->longitudinalForce,*load,tire.slip.roadSpeed,input.surface.rolling,input.wheelRolling});
    if (!force || !routeTireForce(*contact.wheelBody,*force,contact.wheelPoint,
                                  input.geometryCorrections)) return false;
  }
  state.longitudinalSlip = result->slip.longitudinal;
  state.slipAngle = result->slip.angle;
  state.slipMagnitude = result->magnitude;
  state.aligningForce = result->aligningForce;
  state.angularSpeed = nextAngularSpeed;
  state.accumulatedTorque = nextTorque;
  return true;
}
