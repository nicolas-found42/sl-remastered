#include "car_engine.hpp"
#include <cassert>
#include <cmath>
#include <limits>

int main() {
  CarConfig config;
  assert(config.enginePowerCopy == 300 && config.drag[0] == 0);
  assert(sampleCarTorqueCurve(config.torqueCurve, 0) == 0);
  assert(sampleCarTorqueCurve(config.torqueCurve, 4000) == 1);
  assert(sampleCarTorqueCurve({{0,1},{1,2}}, 1) == 0);
  assert(sampleCarTorqueCurve({}, 100) == 0);
  assert(carEngineTorque(config, 4000, 0.25f, {200,300}) == 225);
  config.torqueCurve = {{1000,0.5f},{2000,1}};
  assert(carEngineTorque(config, 1500, 0, {200,300}) == 150);
  config.secondaryTorqueCurve.clear();
  assert(carEngineTorque(config, 1500, 1, {200,300}) == 0);
  config.torqueCurve = {{1000, 0.5f}, {2000, 1.0f}, {4000, 0.5f}};
  PrimaryEngineInput input;
  input.engineRunning = true;
  input.angularSpeed = 1500.0f / 9.549296379089355f;
  input.previousRpm = 1500;
  input.rpmLimit = 3500;
  input.accelerator = 0.5f;
  input.powerScale = 200;
  auto result = primaryEngineTorque(config, input);
  assert(result && std::fabs(result->torque - 150) < 0.001f && result->limiter == 1);
  input.angularSpeed = 3000.0f / 9.549296379089355f;
  input.previousRpm = 2800;
  input.rpmLimit = 3100;
  result = primaryEngineTorque(config, input);
  assert(result && std::fabs(result->limiter - 0.5f) < 0.00001f &&
         std::fabs(result->torque - 75.0f) < 0.01f);
  input.angularSpeed = 3200.0f / 9.549296379089355f;
  input.previousRpm = 3100;
  result = primaryEngineTorque(config, input);
  assert(result && result->limiter == 0 && result->torque == 0);
  input.accelerator = 0.005f;
  result = primaryEngineTorque(config, input);
  assert(result && result->torque == 0);
  input.nitrous = true;
  assert(!primaryEngineTorque(config, input));
  input.nitrous = false;
  input.angularSpeed = 1;
  input.idleAngularSpeed = 2;
  assert(!primaryEngineTorque(config, input));
  ClutchExchangeInput clutch;
  clutch.engineSpeed = 100;
  clutch.wheelSpeed = 40;
  clutch.engineImpulse = 20;
  clutch.engineInertia = 10;
  clutch.transmissionInertia = 4;
  clutch.engineInertiaFactor = 1;
  clutch.gearRatio = 2;
  clutch.finalDrive = 1;
  clutch.drivenWeightSum = 2;
  clutch.clutch = 0.5f;
  auto coupled = calculateClutchExchange(clutch);
  assert(coupled && std::fabs(coupled->engineSpeed - 101.001f) < 0.0001f &&
         std::fabs(coupled->wheelSpeed - 44.995f) < 0.0001f &&
         std::fabs(coupled->wheelDeltaPerDriveWeight - 2.4975f) < 0.0001f &&
         std::fabs(coupled->couplingForce + 12100.0f) < 0.01f);
  clutch.clutch = 1;
  auto disengaged = calculateClutchExchange(clutch);
  assert(disengaged && disengaged->engineSpeed == 102 && disengaged->wheelSpeed == 40 &&
         disengaged->couplingForce == 0 && disengaged->wheelDeltaPerDriveWeight == 0);
  clutch.gearRatio = 0;
  assert(!calculateClutchExchange(clutch));
  clutch.gearRatio = 2;
  clutch.drivenWeightSum = 0;
  assert(!calculateClutchExchange(clutch));
  clutch.clutch = 0.5f;
  std::vector<DrivenWheelState> wheels{
      {true, 1, 1, -40, 0.3f, 5},
      {true, 2, -2, 20, 0.4f, 6},
      {true, -3, 1, 7, 0.5f, 8},
      {true, 1, -0.001f, 3, 0.5f, 9},
      {false, 100, 1, 1, 1, 1},
      {true, 0.001f, 1, 4, 1, 2}};
  const auto original = wheels;
  const auto distributed = calculateDrivenWheelExchange(clutch, wheels);
  assert(distributed);
  const float perWeight = 4.995f / 7.001f;
  assert(std::fabs(distributed->clutch.wheelDeltaPerDriveWeight - perWeight) < 1e-5f);
  assert(std::fabs(distributed->wheels[0].angularSpeed - (-40 - perWeight)) < 1e-5f &&
         std::fabs(distributed->wheels[0].longitudinal - (5 - perWeight * 0.3f)) < 1e-5f &&
         std::fabs(distributed->wheels[1].angularSpeed - (20 + perWeight)) < 1e-5f &&
         std::fabs(distributed->wheels[1].longitudinal - (6 + perWeight * 0.4f)) < 1e-5f);
  assert(distributed->wheels[2].angularSpeed == 7 && distributed->wheels[3].angularSpeed == 3 &&
         distributed->wheels[4].angularSpeed == 1 && distributed->wheels[5].angularSpeed == 4 &&
         wheels[0].angularSpeed == original[0].angularSpeed);
  wheels[1].transferCoefficient = std::numeric_limits<float>::quiet_NaN();
  assert(!calculateDrivenWheelExchange(clutch, wheels));
  assert(wheels[0].angularSpeed == original[0].angularSpeed && wheels[0].longitudinal == original[0].longitudinal);
  WheelBrakeInput brake;
  brake.wheel = {true, 0, 0, 2, 0.3f, 5};
  brake.brake = -0.5f;
  brake.carBrakeStrength = 2;
  brake.wheelBrake = 1;
  brake.effectiveInverseInertia = 0.1f;
  brake.brakeFactor = 1;
  brake.step = 0.01f;
  brake.damageEnabled = false;
  auto braked = calculateWheelBrake(brake);
  assert(braked && std::fabs(braked->wheel.angularSpeed - 1.5f) < 1e-6f &&
         std::fabs(braked->wheel.longitudinal - 4.85f) < 1e-6f && brake.wheel.angularSpeed == 2);
  brake.wheel.angularSpeed = -2;
  braked = calculateWheelBrake(brake);
  assert(braked && std::fabs(braked->wheel.angularSpeed + 1.5f) < 1e-6f &&
         std::fabs(braked->wheel.longitudinal - 5.15f) < 1e-6f);
  brake.wheel.angularSpeed = 0.25f;
  braked = calculateWheelBrake(brake);
  assert(braked && braked->wheel.angularSpeed == 0 && std::fabs(braked->wheel.longitudinal - 4.925f) < 1e-6f);
  brake.wheel.angularSpeed = 2;
  brake.brake = 0;
  brake.handbrake = -0.5f;
  brake.carHandbrakeStrength = 2;
  brake.wheelHandbrake = 1;
  braked = calculateWheelBrake(brake);
  assert(braked && std::fabs(braked->wheel.angularSpeed - 1.5f) < 1e-6f);
  brake.damageEnabled = true;
  assert(!calculateWheelBrake(brake));
  brake.sleeping = true;
  assert(calculateWheelBrake(brake)->wheel.angularSpeed == 2);
  brake.sleeping = false;
  brake.wheel.suspensionPresent = false;
  assert(calculateWheelBrake(brake)->wheel.angularSpeed == 2);
  brake.wheel.suspensionPresent = true;
  brake.handbrake = 0;
  brake.brake = 0.5f;
  brake.thermal = WheelBrakeThermalState{20, 20, 0.001f, 50000, 0, 0.5f, 1, 1, 1, true};
  braked = calculateWheelBrake(brake);
  assert(braked && braked->thermal && braked->thermal->temperature > 20 &&
         braked->thermal->wear > 0 && braked->wheelBrake < 1 && !braked->failureSoundRequested);
  const float wearAfterPositive = braked->thermal->wear;
  brake.wheel.angularSpeed = -2;
  braked = calculateWheelBrake(brake);
  assert(braked && std::fabs(braked->thermal->wear - wearAfterPositive) < 1e-6f);
  brake.wheel.angularSpeed = 0.25f;
  braked = calculateWheelBrake(brake);
  assert(braked && braked->wheel.angularSpeed == 0 && braked->thermal->wear == 0);
  brake.wheel.angularSpeed = 2;
  brake.thermal->temperature = 30;
  brake.brake = 0;
  braked = calculateWheelBrake(brake);
  assert(braked && std::fabs(braked->thermal->temperature - 29.999f) < 1e-5f && braked->thermal->wear == 0);
  brake.brake = 0.5f;
  brake.thermal->temperature = 20;
  brake.thermal->wear = 0.999f;
  braked = calculateWheelBrake(brake);
  assert(braked && braked->thermal->wear == 1 && std::fabs(braked->bearing - 100) < 1e-6f &&
         std::fabs(braked->wheelBrake - 0.020000005f) < 1e-6f && braked->failureSoundRequested);
  brake.thermal->wear = 0;
  brake.thermal->internalDamage = 0;
  braked = calculateWheelBrake(brake);
  assert(braked && braked->thermal->wear == 0 && braked->thermal->temperature > 20);
  brake.thermal->heatCapacity = 0;
  assert(!calculateWheelBrake(brake));
}
