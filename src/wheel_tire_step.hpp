#pragma once
#include "game_spatial_payload.hpp"
#include "wheel.hpp"
#include "wheel_tire_force.hpp"

// The resolver must supply coefficients for this exact retained contact
// surface.
struct TireResolvedSurface {
  std::shared_ptr<const physics::SuspensionContactSurface> identity;
  float friction, rolling;
};
struct TireStepState {
  float longitudinalSlip = 0, slipAngle = 0, angularSpeed = 0,
        accumulatedTorque = 0;
  float slipMagnitude = 0, aligningForce = 0;
};
struct TireStepInput {
  TireSlipForceInput tire;
  TireResolvedSurface surface;
  float maxLoad, loadSmoothing, wheelRolling;
  TireGeometryCorrections geometryCorrections =
      TireGeometryCorrections::RequiredOrUnknown;
  TireForceGeometry geometry{};
  // Use the witness's retained (pose independent) contact: the suspension's
  // contact persists across frames in stock.
  bool retainedWitness = false;
};
// 00454500 loaded-contact branch. Velocities, wheel parameters, timestep and
// global gain in tire are supplied by the caller, not inferred from a surface.
// Rejects unresolved or mismatched contact and changes no state on failure.
bool stepTireContact(const WheelGroundContact &witness,
                     physics::SuspensionUpdate &contact, TireStepState &state,
                     const TireStepInput &input);

struct CarWheelRuntime;
// Live 00454500 loaded-ground branch. Wheel coefficients, the mutable stock
// slip globals, angular gain and rolling value must come from the live caller.
// Other contact types and the no-contact branches are not handled here.
bool stepLiveGroundTire(const WheelGroundContact &witness,
                        CarWheelRuntime &wheel, const Wheel &coefficients,
                        const WheelSlipParameters &slipParameters,
                        float angularGain, float wheelRolling, float timeStep,
                        bool retainedWitness = false);
