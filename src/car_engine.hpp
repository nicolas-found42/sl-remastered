#pragma once
#include "car_config.hpp"
#include <optional>

// 0043be70/0043bee0. The upper endpoint is excluded, as in stock.
float sampleCarTorqueCurve(const std::vector<std::array<float,2>>& curve, float speed);
// Arithmetic portion of0043c7f0 after resolving the live car and its scales.
float carEngineTorque(const CarConfig& config, float speed, float load,
                      const std::array<float,2>& scales);

// Bounded 00454500 primary-curve branch. Speeds are engine radians/second,
// except previousRpm and rpmLimit, which are the stock RPM fields. Starter,
// low-idle compensation, intermittent cuts and nitrous are not represented.
struct PrimaryEngineInput {
  float angularSpeed = 0, previousRpm = 0, rpmLimit = 0, idleAngularSpeed = 0;
  float accelerator = 0, powerScale = 0;
  bool engineRunning = false, intermittentCut = false, nitrous = false;
};
struct PrimaryEngineOutput {
  float torque = 0, rpm = 0, limiter = 1;
};
std::optional<PrimaryEngineOutput> primaryEngineTorque(
    const CarConfig& config, const PrimaryEngineInput& input);

// 00454500's engaged-gear engine/wheel clutch exchange, after wheel speed
// aggregation and engine impulse calculation. No wheel force or world step.
struct ClutchExchangeInput {
  float engineSpeed = 0, wheelSpeed = 0, engineImpulse = 0;
  float engineInertia = 0, transmissionInertia = 0, engineInertiaFactor = 0;
  float gearRatio = 0, finalDrive = 0, clutch = 0, previousDisplayedSpeed = 0;
  float drivenWeightSum = 0; // sum of absolute +0xe0 for present wheel objects
};
struct ClutchExchangeOutput {
  float engineSpeed = 0, wheelSpeed = 0, displayedSpeed = 0;
  float wheelDeltaPerDriveWeight = 0, couplingForce = 0;
};
std::optional<ClutchExchangeOutput> calculateClutchExchange(const ClutchExchangeInput& input);

// 00454500 wheel fields for the engaged-gear exchange. Presence means the
// stock suspension object exists, not that a tire is fitted.
struct DrivenWheelState {
  bool suspensionPresent = false;
  float driveWeight = 0;          // +0xe0
  float transferCoefficient = 0; // +0xc4
  float angularSpeed = 0;         // +0xc0
  float radius = 0;               // +0x60
  float longitudinal = 0;         // +0x148
};
struct DrivenWheelExchange {
  ClutchExchangeOutput clutch;
  std::vector<DrivenWheelState> wheels;
};
// Wheel aggregation, clutch exchange and distribution use one consistent
// snapshot. The input vector is never mutated; no body force is applied.
std::optional<DrivenWheelExchange> calculateDrivenWheelExchange(
    ClutchExchangeInput clutch, const std::vector<DrivenWheelState>& wheels);

// 00454500 per-wheel brake angular and thermal update after clutch exchange.
struct WheelBrakeThermalState {
  float temperature = 0, ambientTemperature = 0, cooling = 0, heatCapacity = 0;
  float wear = 0, wearRate = 0, baseBrake = 0, baseHandbrake = 0;
  float internalDamage = 0;
  bool failureSoundEnabled = false;
};
struct WheelBrakeInput {
  DrivenWheelState wheel;
  float brake = 0, handbrake = 0, wheelExtraBrake = 0;
  float carBrakeStrength = 0, carHandbrakeStrength = 0;
  float wheelBrake = 0, wheelHandbrake = 0, bearing = 0;
  float effectiveInverseInertia = 0, brakeFactor = 0, step = 0;
  bool sleeping = false, damageEnabled = true;
  std::optional<WheelBrakeThermalState> thermal;
};
struct WheelBrakeResult {
  DrivenWheelState wheel;
  float wheelBrake = 0, wheelHandbrake = 0, bearing = 0;
  std::optional<WheelBrakeThermalState> thermal;
  bool failureSoundRequested = false, negativeWearWarning = false;
};
std::optional<WheelBrakeResult> calculateWheelBrake(const WheelBrakeInput& input);
