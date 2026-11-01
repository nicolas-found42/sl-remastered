#include <cstdio>
#include <cstring>
#include "car_runtime.hpp"

#include "car_click_geometry.hpp"
#include "car_engine.hpp"
#include "game_creation.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"
#include "world_model.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

void CarRuntimeInstance::initializeWheelDrivetrain() {
  if (!carPhysics)
    return;
  const size_t count = std::min(carPhysics->wheels.size(), wheelExtras.size());
  for (size_t i = 0; i < count; ++i) {
    const auto &wheel = carPhysics->wheels[i];
    if (!wheel)
      continue;
    auto &extra = wheelExtras[i];
    extra.steer = wheel->config.wheelParameters[0];
    extra.drive = wheel->config.wheelParameters[1];
    extra.brake = wheel->config.wheelParameters[2];
    extra.handBrake = wheel->config.wheelParameters[3];
    extra.brakeThermal.baseBrake = extra.brake;
    extra.brakeThermal.baseHandbrake = extra.handBrake;
    extra.radius = wheel->radius;
  }
}

bool CarRuntimeInstance::advanceControls(const CarControlSnapshot &input,
                                         const CarConfig &config,
                                         const CarControlStepContext &context,
                                         std::string &error) {
  const float values[] = {context.time,
                          context.step,
                          context.drivenWheelSpeed,
                          context.displayedEngineSpeed,
                          steeringState,
                          throttleState,
                          brakeState,
                          handbrakeState,
                          clutchState,
                          shiftState.clutchUntil,
                          shiftState.upUntil,
                          shiftState.downUntil};
  for (float value : values) {
    if (!std::isfinite(value)) {
      error = "car controls: non-finite state";
      return false;
    }
  }
  for (float value : input.continuous) {
    if (!std::isfinite(value)) {
      error = "car controls: non-finite axis";
      return false;
    }
  }
  if (context.step < 0 || config.gearCount < 0 || config.gearCount > 6 ||
      currentGear < 0 || currentGear > 7 ||
      (currentGear != 7 && currentGear > config.gearCount)) {
    error = "car controls: invalid step or gear";
    return false;
  }
  auto shift = shiftState;
  int gear = currentGear;
  float steering = input.continuous[0], throttle = input.continuous[1];
  float brake = input.continuous[2], handbrake = input.continuous[3];
  float clutch = static_cast<float>(input.digital[0]) / 127.0f;
  const float change =
      forcedBrake ? 0.0f : static_cast<float>(input.digital[2]) / 127.0f;
  if (forcedBrake) {
    throttle = 0;
    brake = handbrake = 1;
    gear = 0;
  }
  const auto shifted = [&](bool up) {
    shift.upUntil = context.time + (up ? 0.5f : 0.7f);
    shift.downUntil = context.time + (up ? 1.0f : 0.3f);
  };
  if (change >= -0.1f && change <= 0.1f) {
    shift.buttonHeld = false;
  } else if (!shift.buttonHeld) {
    shift.buttonHeld = true;
    if (config.gearCount) {
      if (change > 0.1f) {
        if (gear == 7) {
          gear = 0;
          shift.clutchUntil = context.time + 0.2f;
          shifted(true);
        } else if (gear < config.gearCount) {
          ++gear;
          shift.clutchUntil = context.time + 0.3f;
          shifted(true);
        }
      } else {
        if (gear == 0) {
          gear = 7;
          shift.clutchUntil = context.time + 0.3f;
          shifted(false);
        } else if (gear != 7) {
          --gear;
          shift.clutchUntil = context.time + 0.2f;
          shifted(false);
        }
      }
    }
  }
  if (shift.clutchUntil > 0) {
    if (shift.clutchUntil <= context.time)
      shift.clutchUntil = -1;
    else {
      clutch = 1;
      throttle = 0;
    }
  }
  if (shift.upUntil > 0 || shift.downUntil > 0) {
    if (shift.upUntil < context.time)
      shift.upUntil = -1;
    if (shift.downUntil < context.time)
      shift.downUntil = -1;
  }
  if (context.automatic && context.positiveMass && context.engineRunning &&
      !context.automaticInhibited && !forcedBrake && shift.clutchUntil <= 0) {
    if (std::fabs(context.drivenWheelSpeed) > 1.0f) {
      if (!carPhysics || carPhysics->wheels.size() > wheelExtras.size()) {
        error = "car controls: driven-wheel state unavailable";
        return false;
      }
      float slip = 0;
      unsigned contacts = 0;
      for (size_t i = 0; i < carPhysics->wheels.size(); ++i) {
        const auto &wheel = carPhysics->wheels[i];
        if (wheel && wheel->suspensionBody &&
            wheel->config.wheelParameters[1] != 0 &&
            wheel->suspension.force > 0) {
          slip += wheel->longitudinalSlip;
          ++contacts;
        }
      }
      if (contacts)
        slip /= static_cast<float>(contacts);
      if (!std::isfinite(slip) || !std::isfinite(maximumRpm) ||
          !std::isfinite(rpmLimit) || !std::isfinite(currentGearRatio)) {
        error = "car controls: invalid automatic transmission state";
        return false;
      }
      const float demand = throttle - brake;
      const float idle = config.rpmIdle * 0.10471975803375244f * 3.0f;
      const float ratio = currentGearRatio;
      const float speed =
          ratio * config.rearEndRatio * context.drivenWheelSpeed;
      const float rpm = speed * 9.549296379089355f;
      const auto torque = [&](float value) {
        return value >= rpmLimit
                   ? 0.0f
                   : sampleCarTorqueCurve(config.torqueCurve, value);
      };
      const float currentTorque = torque(rpm);
      const float threshold = std::min(rpmLimit, maximumRpm) * 0.95f;
      const auto downshift = [&] {
        if (shift.downUntil > 0 || gear <= 1 || gear == 7 || ratio == 0)
          return;
        const float lowerRatio = config.gearRatios[gear - 2];
        const float lowerRpm = rpm / ratio * lowerRatio;
        if (lowerRpm < threshold &&
            currentTorque * ratio * 1.15f < torque(lowerRpm) * lowerRatio) {
          --gear;
          shift.clutchUntil = context.time + 0.2f;
          shifted(false);
        }
      };
      if (demand < 0 && gear != 0 && gear != 7) {
        shift.reversing = true;
        downshift();
      } else if (demand > 0.1f && gear == 0) {
        if (config.gearCount && shift.upUntil <= 0 &&
            context.displayedEngineSpeed > idle) {
          gear = 1;
          shifted(true);
        }
      } else if (demand > 0.1f && gear == 7) {
        if (shift.downUntil <= 0) {
          gear = 0;
          shifted(true);
        } else {
          std::swap(throttle, brake);
        }
      } else if (demand < -0.1f) {
        if (config.gearCount) {
          std::swap(throttle, brake);
          if (gear != 7 && shift.upUntil <= 0 &&
              context.displayedEngineSpeed > idle) {
            gear = 7;
            shifted(true);
          }
        }
      } else if (gear >= 1 && gear != 7 &&
                 (demand > 0.1f ? std::fabs(slip) < 0.2f
                                : gear > 1 && shift.downUntil <= 0)) {
        if (rpm < 0) {
          gear = 0;
          shift.clutchUntil = context.time + 0.2f;
          shifted(false);
        } else if (ratio != 0) {
          if (shift.upUntil <= 0 && gear < config.gearCount &&
              (std::fabs(ratio) <= 0.01f ||
               std::fabs(context.displayedEngineSpeed - speed) <=
                   context.displayedEngineSpeed * 0.1f)) {
            const float higherRatio = config.gearRatios[gear];
            const float higherRpm = rpm / ratio * higherRatio;
            if (rpm > threshold ||
                currentTorque * ratio < torque(higherRpm) * higherRatio) {
              ++gear;
              shift.clutchUntil = context.time + 0.3f;
              shifted(true);
            }
          }
          downshift();
        }
      }
    } else {
      const float demand = throttle - brake;
      const float idleSpeed = config.rpmIdle * 0.10471975803375244f;
      if (!shift.reversing) {
        if (demand > 0.1f) {
          if (config.gearCount && gear == 0 &&
              context.displayedEngineSpeed > idleSpeed * 3.0f) {
            gear = 1;
            shifted(true);
          }
        } else if (demand < -0.1f) {
          if (config.gearCount) {
            std::swap(throttle, brake);
            if (gear == 0 && context.displayedEngineSpeed > idleSpeed * 3.0f) {
              gear = 7;
              shifted(true);
            }
          }
        } else if (gear != 0 &&
                   context.displayedEngineSpeed < idleSpeed * 1.2f) {
          gear = 0;
          shifted(false);
        }
      } else if (demand > -0.5f) {
        shift.reversing = false;
        gear = 0;
        shifted(false);
      }
    }
  }
  // 0044d330 runs after automatic shifting and immediately applies its clutch.
  if (shift.clutchUntil > context.time && shift.clutchUntil > 0) {
    clutch = 1;
    throttle = 0;
  }
  const float pedalBlend = std::min(context.step * 10.0f, 1.0f);
  const float clutchBlend = std::min(context.step * 33.333335876464844f, 1.0f);
  const float nextThrottle =
      throttleState + (throttle - throttleState) * pedalBlend;
  const float nextBrake = brakeState + (brake - brakeState) * pedalBlend;
  const float nextClutch = clutchState + (clutch - clutchState) * clutchBlend;
  if (!std::isfinite(nextThrottle) || !std::isfinite(nextBrake) ||
      !std::isfinite(nextClutch) || !std::isfinite(shift.clutchUntil) ||
      !std::isfinite(shift.upUntil) || !std::isfinite(shift.downUntil)) {
    error = "car controls: non-finite result";
    return false;
  }
  steeringState = steering;
  throttleState = nextThrottle;
  brakeState = nextBrake;
  handbrakeState = handbrake;
  clutchState = nextClutch;
  shiftState = shift;
  currentGear = gear;
  // FUN_0044df70 / FUN_0044e120: intervention flags describe actual pedal
  // attenuation, never merely an enabled assistance setting.
  absActive = asrActive = false;
  const auto assist = [&](float threshold, bool braking, float& previous) {
    float& pedal = braking ? brakeState : throttleState;
    if (!(threshold > 0 && pedal >= 0.01f))
      return false;
    float slip = 0;
    unsigned count = 0;
    if (carPhysics)
      for (size_t i = 0; i < carPhysics->wheels.size() && i < wheelExtras.size(); ++i) {
        const auto& wheel = carPhysics->wheels[i];
        const float weight = braking ? wheelExtras[i].brake : wheelExtras[i].drive;
        if (wheel && wheel->suspensionBody && weight != 0 && wheel->suspension.force > 0) {
          slip += std::fabs(wheel->longitudinalSlip);
          ++count;
        }
      }
    if (!count) {
      previous = 0;
      return false;
    }
    const float residual = slip / float(count) - threshold * 0.2f;
    const float predictive = residual + (residual - previous) * (braking ? 4.0f : 2.0f);
    previous = residual;
    if (!(predictive > 0))
      return false;
    const float factor = std::max(0.0f, 1.0f - predictive /
        (threshold * (braking ? 0.40000003576278687f : 0.30000001192092896f)));
    pedal *= factor;
    if (!braking)
      clutchState = 1.0f - (1.0f - clutchState) * factor;
    return factor < 0.9f;
  };
  absActive = assist(abs, true, previousAbsSlip);
  asrActive = assist(asr, false, previousAsrSlip);
  error.clear();
  return true;
}

CarDrivetrainStepContext
CarRuntimeInstance::drivetrainContext(const CarConfig &config,
                                      float step) const {
  CarDrivetrainStepContext context;
  context.step = step;
  context.rpmLimit = rpmLimit;
  context.maximumRpm = maximumRpm;
  const float wearScale =
      engineWear < 1.0f ? 1.0f - engineWear * engineWear * 0.6f : 0.05f;
  context.powerScale = config.enginePower[0] * wearScale;
  context.engineInertiaFactor = engineInertiaFactor;
  context.brakeFactor = wheelBrakeFactor;
  context.starterForce = starterForce;
  context.engineRunning = engineRunning;
  context.sleeping = drivetrainSleeping;
  context.damageEnabled = damageEnabled;
  return context;
}

bool CarRuntimeInstance::advanceDrivetrain(
    const CarConfig &config, const CarDrivetrainStepContext &context,
    std::string &error) {
  if (!carPhysics || carPhysics->wheels.size() > wheelExtras.size() ||
      context.sleeping || !std::isfinite(context.step) ||
      context.step < 0.0001f || !std::isfinite(context.maximumRpm) ||
      context.maximumRpm <= 0 || !std::isfinite(context.engineInertiaFactor) ||
      context.engineInertiaFactor <= 0 || !std::isfinite(context.brakeFactor) ||
      context.brakeFactor < 0 || config.gearCount < 0 || config.gearCount > 6 ||
      currentGear < 0 || currentGear > 7 ||
      (currentGear != 7 && currentGear > config.gearCount) ||
      !std::isfinite(clutchState) || clutchState < 0 || clutchState > 1) {
    error = "car drivetrain: unsupported or invalid live physics state";
    return false;
  }
  {
    char detail[320];
    detail[0] = 0;
    const auto bad = [&](const char *name, bool failed) {
      if (!failed)
        return;
      const size_t used = std::strlen(detail);
      std::snprintf(detail + used, sizeof detail - used, "%s%s",
                    used ? "," : "", name);
    };
    bad("engineAngularSpeed", !std::isfinite(engineAngularSpeed));
    // 00454500 samples the torque curve with the signed rpm (FUN_0043be70
    // returns 0 below the first point). A negative engine speed is only
    // ported while the curve starts at or above 0 rpm.
    bad("engineAngularSpeed<0 with torque curve below 0 rpm",
        engineAngularSpeed < 0 &&
            (config.torqueCurve.empty() || config.torqueCurve.front()[0] < 0));
    bad("starterForce", !std::isfinite(context.starterForce));
    bad("powerScale", !std::isfinite(context.powerScale));
    bad("rpmLimit", !std::isfinite(context.rpmLimit));
    bad("previousEngineRpm", !std::isfinite(previousEngineRpm));
    bad("throttleState", !std::isfinite(throttleState));
    bad("intermittentCut", context.intermittentCut);
    bad("nitrous", context.nitrous);
    bad("enginePower1", config.enginePower[1] <= 0);
    bad("enginePower4", config.enginePower[4] <= 0);
    bad("enginePower5", config.enginePower[5] <= 0);
    if (detail[0]) {
      char text[480];
      std::snprintf(text, sizeof text,
                    "car drivetrain: unsupported cut, nitrous or engine state "
                    "(%s; engineAngularSpeed=%g previousEngineRpm=%g "
                    "throttleState=%g starterForce=%g powerScale=%g rpmLimit=%g)",
                    detail, double(engineAngularSpeed), double(previousEngineRpm),
                    double(throttleState), double(context.starterForce),
                    double(context.powerScale), double(context.rpmLimit));
      error = text;
      return false;
    }
  }
  // 00454500 clears engine speed when the engine is off but still brakes.
  const float speed = context.engineRunning ? engineAngularSpeed : 0.0f;
  const float rpm = speed * 9.549296379089355f;
  const float idle = config.rpmIdle * 0.10471975803375244f;
  const float frictionLimit = speed * config.enginePower[1] * -0.5f;
  const float frictionScale = context.maximumRpm * 0.05f;
  // 00454500: speed > 0 uses the +0xf0 friction (enginePower[2], scaled by
  // the wear multiplier) bounded below by frictionLimit; speed < 0 uses
  // speed^2 * +0xf4 (enginePower[3]) bounded above by frictionLimit; exactly 0
  // has no drag.
  const float drag =
      speed > 0
          ? std::max(-(speed / frictionScale) * frictionScale * frictionScale *
                         config.enginePower[2] * engineFrictionMultiplier,
                     frictionLimit)
      : speed < 0 ? std::min(speed * speed * config.enginePower[3],
                             frictionLimit)
                  : 0.0f;
  float demand = std::fabs(throttleState);
  const float available =
      sampleCarTorqueCurve(config.torqueCurve, rpm) * context.powerScale;
  if (speed < idle && speed > 0 && available > 0.001f)
    demand = std::min(1.0f, demand + std::fabs(drag * 1.1f) / available);
  const bool starter =
      context.engineRunning && demand > 0.1f && speed <= idle * 0.95f;
  if (starter)
    demand = 0;
  PrimaryEngineOutput engineOutput{};
  const bool updateRpm = std::fabs(rpm) > 0.001f;
  if (updateRpm && speed < 0) {
    // Negative speed: the curve contributes nothing (guarded above) and the
    // rpm still becomes the previous rpm (stock stores it at car +0x1f4c).
    engineOutput.rpm = rpm;
    engineOutput.limiter = 1.0f;
  } else if (updateRpm) {
    PrimaryEngineInput engine;
    engine.engineRunning = true;
    engine.angularSpeed = speed;
    engine.previousRpm = previousEngineRpm;
    engine.rpmLimit = context.rpmLimit;
    engine.accelerator = demand;
    engine.powerScale = context.powerScale;
    const auto output = primaryEngineTorque(config, engine);
    if (!output) {
      error = "car drivetrain: invalid primary torque curve or engine state";
      return false;
    }
    engineOutput = *output;
  }
  const float impulse =
      drag + engineOutput.torque + (starter ? context.starterForce : 0);
  const float ratio =
      currentGear == 0
          ? 0.0f
          : config.gearRatios[static_cast<size_t>(currentGear - 1)];
  std::vector<DrivenWheelState> wheels;
  wheels.reserve(carPhysics->wheels.size());
  float weightSum = 0;
  for (size_t i = 0; i < carPhysics->wheels.size(); ++i) {
    const auto &wheel = carPhysics->wheels[i];
    const auto &extra = wheelExtras[i];
    if (!wheel) {
      wheels.push_back({});
      continue;
    }
    wheels.push_back({wheel->suspensionBody != nullptr, extra.drive,
                      wheel->transferCoefficient, wheel->angularSpeed,
                      wheel->radius, wheel->longitudinalVelocity});
    if (wheel->suspensionBody)
      weightSum += std::fabs(extra.drive);
  }
  float nextEngine = speed, nextDisplayed = displayedEngineSpeed;
  float nextDriven = drivenWheelSpeed, nextForce = 0;
  if (!context.engineRunning || ratio == 0) {
    nextDisplayed = speed;
    nextEngine += impulse / config.enginePower[1];
  } else {
    ClutchExchangeInput exchange;
    exchange.engineSpeed = speed;
    exchange.engineImpulse = impulse;
    exchange.engineInertia = config.enginePower[1];
    exchange.transmissionInertia = config.enginePower[4];
    exchange.engineInertiaFactor = context.engineInertiaFactor;
    exchange.gearRatio = ratio;
    exchange.finalDrive = config.rearEndRatio;
    exchange.clutch = clutchState;
    exchange.previousDisplayedSpeed = displayedEngineSpeed;
    const auto result = calculateDrivenWheelExchange(exchange, wheels);
    if (!result) {
      error = "car drivetrain: invalid clutch or wheel exchange";
      return false;
    }
    wheels = result->wheels;
    nextEngine = result->clutch.engineSpeed;
    nextDisplayed = result->clutch.displayedSpeed;
    nextDriven = result->clutch.wheelSpeed;
    nextForce = result->clutch.couplingForce;
  }
  std::array<std::optional<WheelBrakeResult>, 16> brakeResults;
  std::vector<float> inverseInertias(wheels.size());
  for (size_t i = 0; i < wheels.size(); ++i) {
    if (!wheels[i].suspensionPresent)
      continue;
    const auto &extra = wheelExtras[i];
    float inertia = config.enginePower[5];
    if (weightSum > 0.0001f && extra.drive > 0.0001f && ratio != 0)
      inertia += (extra.drive / weightSum) * (1.0f - clutchState) *
                 std::fabs(config.rearEndRatio * ratio) * config.enginePower[1];
    inverseInertias[i] = 0.1f / inertia;
    if (!std::isfinite(inverseInertias[i]) || inverseInertias[i] <= 0) {
      error = "car drivetrain: invalid wheel inertia";
      return false;
    }
    WheelBrakeInput brake;
    brake.wheel = wheels[i];
    brake.brake = brakeState;
    brake.handbrake = handbrakeState;
    brake.carBrakeStrength = config.brakePower[0];
    brake.carHandbrakeStrength = config.brakePower[1];
    brake.wheelBrake = extra.brake;
    brake.wheelHandbrake = extra.handBrake;
    brake.bearing = extra.bearing;
    brake.effectiveInverseInertia = inverseInertias[i];
    brake.brakeFactor = context.brakeFactor;
    brake.step = context.step;
    brake.damageEnabled = context.damageEnabled;
    if (context.damageEnabled) {
      brake.thermal = extra.brakeThermal;
      brake.thermal->internalDamage = internalDamageFactor;
      // Record the failure even when the prototype's sound link is absent.
      brake.thermal->failureSoundEnabled = true;
    }
    const auto result = calculateWheelBrake(brake);
    if (!result) {
      error = "car drivetrain: invalid wheel brake state";
      return false;
    }
    wheels[i] = result->wheel;
    brakeResults[i] = *result;
  }
  float nextEngineWear = engineWear, nextClutchWear = clutchWear;
  float nextCoolingWear = engineCoolingWear,
        nextTransmissionWear = transmissionWear;
  float nextTemperature = engineTemperature, nextCooling = engineCooling;
  float nextFrictionMultiplier = engineFrictionMultiplier;
  bool engineFailure = false;
  if (context.damageEnabled) {
    const float values[] = {engineWear,
                            engineWearRate,
                            clutchWear,
                            clutchWearRate,
                            engineCoolingWear,
                            engineCoolingWearRate,
                            transmissionWear,
                            transmissionWearRate,
                            engineHeatCapacity,
                            engineTemperature,
                            ambientEngineTemperature,
                            engineCooling,
                            internalDamageFactor,
                            engineFrictionMultiplier,
                            engineCoolingParameters[0],
                            engineCoolingParameters[1],
                            engineCoolingParameters[2]};
    for (float value : values)
      if (!std::isfinite(value)) {
        error = "car drivetrain: invalid damage state";
        return false;
      }
    if (engineHeatCapacity <= 0 || engineWearRate <= 0) {
      error = "car drivetrain: invalid thermal capacity or wear rate";
      return false;
    }
  }
  if (context.engineRunning) {
    if (context.damageEnabled && updateRpm) {
      const float heat = available * demand * 1000.0f;
      if (heat * context.step > 0)
        nextTemperature += heat * context.step / engineHeatCapacity;
      if (nextTemperature <= 90.0f || nextCoolingWear >= 1.0f) {
        nextCooling = engineCoolingParameters[0] / engineHeatCapacity;
      } else {
        const auto &velocity =
            carPhysics->root
                ? carPhysics->root->state[carPhysics->root->current].vel
                : linearVelocity;
        const float velocitySquared = velocity[0] * velocity[0] +
                                      velocity[1] * velocity[1] +
                                      velocity[2] * velocity[2];
        const float coolingScale =
            nextCoolingWear < 1.0f
                ? 1.0f - nextCoolingWear * nextCoolingWear * 0.6f
                : 0.1f;
        nextCooling = ((velocitySquared * engineCoolingParameters[2] +
                        engineCoolingParameters[1]) *
                           coolingScale +
                       engineCoolingParameters[0]) /
                      engineHeatCapacity;
      }
      if (nextTemperature > 100.0f) {
        const float excess = nextTemperature - 100.0f;
        const float wear =
            excess * excess * internalDamageFactor * context.step;
        if (wear > 0)
          nextCoolingWear =
              std::min(1.0f, nextCoolingWear + wear * engineCoolingWearRate);
      }
      if (nextEngineWear < 1.0f) {
        float load = std::fabs(rpm) / context.maximumRpm;
        load *= load;
        load *= load;
        load *= load;
        load *= load;
        const float temperatureDelta =
            std::fabs(nextTemperature - 90.0f) * 0.01f;
        const float wear = load * (temperatureDelta * temperatureDelta + 1.0f) *
                           ((context.step + context.step) / engineWearRate) *
                           config.enginePower[2] * engineFrictionMultiplier *
                           100.0f * internalDamageFactor;
        if (wear > 0) {
          nextEngineWear =
              std::min(1.0f, nextEngineWear + wear * engineWearRate);
          if (nextEngineWear >= 1.0f) {
            nextFrictionMultiplier *= 50.0f;
            engineFailure = true;
          }
        }
      }
    }
    const float delta = nextTemperature - ambientEngineTemperature;
    nextTemperature -= std::fabs(delta) * nextCooling * delta * context.step;
  }
  if (context.damageEnabled && context.engineRunning && ratio != 0) {
    const float clutchWork =
        std::fabs((nextDriven * ratio * config.rearEndRatio - nextEngine) *
                  (1.0f - clutchState) * 0.1f) *
        internalDamageFactor;
    if (clutchWork > 0)
      nextClutchWear =
          std::min(1.0f, nextClutchWear + clutchWork * clutchWearRate);
    float transmissionWork = std::fabs(nextForce) * 0.01f;
    transmissionWork *= transmissionWork;
    transmissionWork *= transmissionWork;
    transmissionWork *= context.step * 0.0001f * internalDamageFactor;
    if (transmissionWork > 0)
      nextTransmissionWear = std::min(
          1.0f, nextTransmissionWear + transmissionWork * transmissionWearRate);
  }
  if (!std::isfinite(nextTemperature) || !std::isfinite(nextCooling) ||
      !std::isfinite(nextEngineWear) || !std::isfinite(nextCoolingWear) ||
      !std::isfinite(nextClutchWear) || !std::isfinite(nextTransmissionWear) ||
      !std::isfinite(nextFrictionMultiplier)) {
    error = "car drivetrain: non-finite damage result";
    return false;
  }
  const float nextRpm = nextDisplayed * 9.549296379089355f;
  if (!std::isfinite(nextEngine) || !std::isfinite(nextDisplayed) ||
      !std::isfinite(nextDriven) || !std::isfinite(nextForce) ||
      !std::isfinite(nextRpm)) {
    error = "car drivetrain: non-finite result";
    return false;
  }
  const auto &soundPosition =
      carPhysics->root ? carPhysics->root->state[carPhysics->root->current].pos
                       : position;
  if (engineFailure)
    failureSounds.push_back({CarFailureSoundEvent::Kind::Engine, -1,
                             soundPosition, engineFailureSoundEnabled});
  for (size_t i = 0; i < wheels.size(); ++i) {
    auto &wheel = carPhysics->wheels[i];
    if (!wheel || !wheels[i].suspensionPresent)
      continue;
    wheel->angularSpeed = wheels[i].angularSpeed;
    wheel->longitudinalVelocity = wheels[i].longitudinal;
    wheel->effectiveInverseInertia = inverseInertias[i];
    if (brakeResults[i] && context.damageEnabled) {
      auto &extra = wheelExtras[i];
      const auto &result = *brakeResults[i];
      extra.brake = result.wheelBrake;
      extra.handBrake = result.wheelHandbrake;
      extra.bearing = result.bearing;
      extra.brakeThermal = *result.thermal;
      if (result.failureSoundRequested)
        failureSounds.push_back({CarFailureSoundEvent::Kind::Brake,
                                 static_cast<int>(i), soundPosition,
                                 brakeFailureSoundEnabled});
    }
  }
  engineAngularSpeed = nextEngine;
  displayedEngineSpeed = nextDisplayed;
  drivenWheelSpeed = nextDriven;
  couplingForce = nextForce;
  engineWear = nextEngineWear;
  clutchWear = nextClutchWear;
  engineCoolingWear = nextCoolingWear;
  transmissionWear = nextTransmissionWear;
  engineTemperature = nextTemperature;
  engineCooling = nextCooling;
  engineFrictionMultiplier = nextFrictionMultiplier;
  if (context.engineRunning)
    currentGearRatio = ratio;
  if (updateRpm)
    previousEngineRpm = engineOutput.rpm;
  engineRpm = nextRpm;
  driveTorque = engineOutput.torque;
  error.clear();
  return true;
}

CarPrototype::~CarPrototype() { setLinkOwner(source, nullptr); }

CarResourceBinding::~CarResourceBinding() { setLinkOwner(owner, nullptr); }

RuntimeSlotState::~RuntimeSlotState() { setLinkOwner(attachedNode, nullptr); }

CarSeat::~CarSeat() {
  setLinkOwner(player, nullptr);
  setLinkOwner(camera, nullptr);
  setLinkOwner(renderInstance, nullptr);
  setLinkOwner(osd, nullptr);
  for (OwnerLink &link : links)
    setLinkOwner(link, nullptr);
}

std::optional<std::array<float, 16>>
calculateCarSteeringAngles(const CarRuntimeInstance &car,
                           const CarConfig &chassis) {
  if (!car.carPhysics ||
      car.carPhysics->wheels.size() > car.wheelExtras.size() ||
      !std::isfinite(car.steeringState) ||
      !std::isfinite(chassis.maxSteer[0]) ||
      !std::isfinite(chassis.maxSteer[1]))
    return std::nullopt;
  auto result = car.wheelSteeringAngles;
  const float requested = chassis.maxSteer[0] * car.steeringState;
  const float base = chassis.maxSteer[1];
  if (!std::isfinite(requested))
    return std::nullopt;
  for (size_t i = 0; i < car.carPhysics->wheels.size(); ++i) {
    const auto &wheel = car.carPhysics->wheels[i];
    if (!wheel)
      continue;
    const float factor = car.wheelExtras[i].steer;
    if (!wheel->suspensionBody ||
        wheel->suspension.wheelBody != wheel->suspensionBody.get() ||
        !std::isfinite(wheel->anchor[0]) || !std::isfinite(factor))
      return std::nullopt;
    if (std::fabs(requested) <= 0.0010000000474974513f) {
      result[i] = 0;
    } else if (base == 0) {
      result[i] = requested * factor;
    } else {
      const float radius = base / std::tan(requested);
      result[i] = (radius <= 0 ? -std::atan2(base, -radius - wheel->anchor[0])
                               : std::atan2(base, radius + wheel->anchor[0])) *
                  factor;
    }
    if (!std::isfinite(result[i]))
      return std::nullopt;
  }
  return result;
}

std::optional<CarControlSnapshot> sampleCarControls(GameWorld &world,
                                                    const CarSeat &seat) {
  auto *player = world.nodeFor(seat.player.owner);
  if (!player || !player->loaded || player->nativeDevice != "player" ||
      !player->player || !player->playerInput ||
      player->player->scriptedControlOverride)
    return std::nullopt;
  // Commit smoothing/cache updates only when every stock query is supported.
  auto input = *player->playerInput;
  CarControlSnapshot result;
  constexpr std::array<int32_t, 4> continuousAxes{{1, 0x1c, 0x1d, 0x30}};
  constexpr std::array<int32_t, 7> digitalAxes{
      {0x31, 0x1f, 0x32, 0x36, 7, 0x27, 0x28}};
  for (size_t i = 0; i < continuousAxes.size(); ++i) {
    const auto value = input.sampleAxis(continuousAxes[i]);
    if (!value || !std::isfinite(*value))
      return std::nullopt;
    result.continuous[i] = *value;
  }
  for (size_t i = 0; i < digitalAxes.size(); ++i) {
    const auto value = input.sampleAxis(digitalAxes[i]);
    if (!value || !std::isfinite(*value))
      return std::nullopt;
    const float scaled = *value * 127.0f;
    if (!std::isfinite(scaled) ||
        static_cast<double>(scaled) < std::numeric_limits<int32_t>::min() ||
        static_cast<double>(scaled) > std::numeric_limits<int32_t>::max())
      return std::nullopt;
    result.digital[i] = static_cast<int32_t>(scaled);
  }
  *player->playerInput = std::move(input);
  return result;
}

CarSeat *CarRuntimeInstance::findSeat(uint32_t playerId) const {
  for (const auto &seat : seats)
    if (seat && seat->player.cachedValue == playerId)
      return seat.get();
  return nullptr;
}

CarSeat *CarRuntimeInstance::selectSeat(uint32_t playerId) const {
  if (auto *existing = findSeat(playerId))
    return existing;
  return findSeat(0);
}

CarRuntimeInstance::~CarRuntimeInstance() {
  setLinkOwner(prototypeSource, nullptr);
  setLinkOwner(carLink, nullptr);
  setLinkOwner(physicsRoot, nullptr);
  setLinkOwner(installParentLink, nullptr);
  for (auto &slot : slots)
    if (slot)
      setLinkOwner(slot->attachedNode, nullptr);
  for (auto &part : attachedParts)
    if (part)
      setLinkOwner(*part, nullptr);
}

bool loadCarPrototype(ResourceWorld &world, ScriptNode &source,
                      const ResourceIdMapping &mapping, std::string_view text,
                      std::unique_ptr<CarPrototype> &result, std::string &error,
                      bool isCar) {
  result = std::make_unique<CarPrototype>();
  result->isCar = isCar;
  setLinkOwner(result->source, &source);
  struct DefaultRequest {
    const char *pack;
    uint16_t local;
    int type;
  };
  static const DefaultRequest carDefaults[] = {
      {"decals.rpk", 0x0b, 7}, {"frontend.rpk", 0x23, 14},
      {"cars.rpk", 0x55, 14},  {"cars.rpk", 0x57, 14},
      {"cars.rpk", 0x56, 14},  {"cars.rpk", 0x58, 14},
      {"cars.rpk", 0x58, 14},  {"cars.rpk", 0x5c, 14},
      {"cars.rpk", 0x5c, 14},  {"cars.rpk", 0x58, 14},
      {"cars.rpk", 0x61, 14},  {"cars.rpk", 0x62, 14},
      {"cars.rpk", 0x63, 14},  {"cars.rpk", 0x64, 14},
      {"sound.rpk", 0x0d, 6},  {"sound.rpk", 0x0d, 6},
      {"sound.rpk", 0x0d, 6},  {"sound.rpk", 0x0d, 6},
      {"sound.rpk", 0x19, 6},  {"sound.rpk", 0x1a, 6},
      {"sound.rpk", 7, 6},     {"sound.rpk", 9, 6},
      {"sound.rpk", 10, 6},    {"sound.rpk", 0x17, 6},
      {"sound.rpk", 0x18, 6},  {"sound.rpk", 0x15, 6},
      {"sound.rpk", 8, 6},     {"sound.rpk", 6, 6},
      {"sound.rpk", 0x25, 6},  {"particles.rpk", 2, 14},
      {"particles.rpk", 2, 14}};
  static const DefaultRequest partDefaults[] = {{"decals.rpk", 0x0b, 7},
                                                {"sound.rpk", 0x0b, 6}};
  auto load = [&](const CarResourceRequest &request, uint32_t id) {
    const auto fail = [&](const std::string &why) {
      error = "car " + request.directive + " line " +
              std::to_string(request.line) + " resource " + std::to_string(id) +
              ": " + why;
      return false;
    };
    if (!id) {
      auto binding = std::make_unique<CarResourceBinding>();
      binding->request = request;
      result->resources.push_back(std::move(binding));
      return true;
    }
    auto found = world.lookup(id, static_cast<uint32_t>(request.loadType),
                              request.fallbackType != 0);
    if (!found.node && request.fallbackType)
      found = world.lookup(id, static_cast<uint32_t>(request.fallbackType));
    if (found.status != LookupStatus::Found || !found.node.owner)
      return fail("resource unavailable");
    auto loaded = std::make_unique<CarResourceBinding>();
    loaded->request = request;
    loaded->id = id;
    setLinkOwner(loaded->owner, found.node.owner);
    result->resources.push_back(std::move(loaded));
    return true;
  };
  const DefaultRequest *defaults = isCar ? carDefaults : partDefaults;
  const size_t defaultCount =
      isCar ? std::size(carDefaults) : std::size(partDefaults);
  for (size_t i = 0; i < defaultCount; ++i) {
    const auto &item = defaults[i];
    uint32_t id = 0;
    if (!world.packs().resolveLiteral(item.pack, item.local, id, error))
      return false;
    if (!load({"default", item.local, item.type, 0, true, {}, 0}, id))
      return false;
  }
  if (!text.empty() && !parseCarConfig(text, result->config, error))
    return false;
  for (auto &slot : result->config.slots) {
    for (auto &attach : slot.attachments)
      attach.resourceId = remapResourceId(attach.resourceId, mapping);
    for (auto &comp : slot.compatible)
      comp.resource = remapResourceId(comp.resource, mapping);
    for (auto &bolt : slot.bolts)
      if (bolt.resource)
        bolt.resource = remapResourceId(bolt.resource, mapping);
    if (slot.hasFlapSounds) {
      for (auto &sfx : slot.flapSounds)
        if (sfx)
          sfx = remapResourceId(sfx, mapping);
    }
  }
  for (auto &stockPart : result->config.stockParts)
    stockPart = remapResourceId(stockPart, mapping);
  size_t up = 0, down = 0, exhaust = 0;
  for (const auto &request : result->config.resources) {
    uint32_t id = request.runtimeId
                      ? request.resource
                      : remapResourceId(request.resource, mapping);
    if (!request.literalPack.empty() &&
        !world.packs().resolveLiteral(request.literalPack, request.resource, id,
                                      error))
      return false;
    if (!load(request, id))
      return false;
    CarSfxTable *table = nullptr;
    const CarEngineSound *sound = nullptr;
    if (request.directive == "sfx_engine_up") {
      table = &result->engineUp;
      sound = &result->config.engineUp[up++];
    } else if (request.directive == "sfx_engine_down") {
      table = &result->engineDown;
      sound = &result->config.engineDown[down++];
    } else if (request.directive == "sfx_exhaust") {
      table = &result->exhaust;
      sound = &result->config.exhaust[exhaust++];
    }
    if (table &&
        !table->addItem(result->resources.back()->owner.owner, sound->rpmScale,
                        sound->values[0], sound->values[1], sound->values[2],
                        sound->values[3])) {
      error = "car sound table capacity exceeded";
      return false;
    }
  }
  if (isCar) {
    for (const auto &fallback :
         {std::pair<CarSfxTable *, uint16_t>{&result->engineUp, 3},
          {&result->engineDown, 2}}) {
      if (fallback.first->size())
        continue;
      uint32_t id = 0;
      if (!world.packs().resolveLiteral("sound.rpk", fallback.second, id,
                                        error))
        return false;
      if (!load({"default sound", fallback.second, 6, 0, true, {}, 0}, id))
        return false;
      fallback.first->addItem(result->resources.back()->owner.owner,
                              fallback.second == 3 ? 5000.0f : 2500.0f, 1.0f,
                              1000000.0f, 1.0f, 1.0f);
    }
  }
  const auto collisionLoader = [&](const std::string &path, ScxModel &model,
                                   std::string &detail) {
    std::string bytes;
    if (!readNativeDataFile(world.packs(), path, INT32_MAX, bytes, detail))
      return false;
    auto mesh =
        scxParse(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
    if (!mesh.ok) {
      detail = mesh.error;
      return false;
    }
    model = std::move(mesh.model);
    return true;
  };
  const auto geometryResourceLoader = [&](uint32_t serialized, float mass,
                                          CarGeometryPrototype &out,
                                          std::string &detail) {
    const auto id = remapResourceId(serialized, mapping);
    const auto found = world.lookup(id, 9);
    PackResource resource;
    if (!found.node || !world.packs().lookup(id, resource, detail) ||
        !resource.packBytes) {
      if (detail.empty())
        detail = "physics geometry resource unavailable";
      return false;
    }
    const auto &bytes = *resource.packBytes;
    const size_t offset = resource.entry.payloadOffset,
                 size = resource.entry.payloadSize;
    if (offset > bytes.size() || size > bytes.size() - offset) {
      detail = "invalid physics geometry resource payload bounds";
      return false;
    }
    CarGeometryPrototype prototype;
    if (!loadCarGeometryResource(
            std::string_view(
                reinterpret_cast<const char *>(bytes.data() + offset), size),
            collisionLoader, prototype, detail))
      return false;
    out = cloneGeometryResource(prototype, mass);
    return true;
  };
  if (!buildCarGeometry(result->config, collisionLoader, result->geometry,
                        error, geometryResourceLoader))
    return false;
  if (isCar && !buildCarWheelGeometry(result->config, result->config.brakePower,
                                      result->wheels, error))
    return false;
  if (result->config.click) {
    CarGeometryPrototype click;
    const auto &definition = *result->config.click;
    if (definition.resource) {
      if (!geometryResourceLoader(*definition.resource, 0, click, error))
        return false;
      click.body->flags = (click.body->flags | 0x20u) & ~physics::kProtoCreated;
    } else if (!loadCarClickMesh(definition.meshPath, collisionLoader, click,
                                 error))
      return false;
    PartSlotDirective pose{};
    for (size_t i = 0; i < 3; ++i) {
      pose.position[i] = definition.position[i];
      pose.orientation[i] = definition.orientation[i];
    }
    PartSlotState transform;
    if (!makePartSlotState(pose, transform)) {
      error = "invalid click geometry transform";
      return false;
    }
    click.transform = {definition.position, definition.orientation,
                       transform.rotation};
    result->clickGeometry = std::move(click);
  }
  result->stage = CarPrototypeStage::PhysicsGeometryReady;
  error.clear();
  return true;
}

CarHudOptionalValues carHudOptionalValues(GameWorld &world,
                                         const CarRuntimeInstance &car) {
  const auto objectField = [](const VmObject *object, std::string_view name) -> const VmObject * {
    if (object)
      for (const auto &field : object->fields)
        if (field.name == name)
          if (const auto *value = std::get_if<const VmObject *>(&field.value))
            return *value;
    return nullptr;
  };
  const auto floatField = [](const VmObject *object, std::string_view name) {
    if (object)
      for (const auto &field : object->fields)
        if (field.name == name)
          if (const auto *value = std::get_if<float>(&field.value))
            return *value;
    return 0.0f;
  };
  CarHudOptionalValues values;
  // FUN_00448430 copies tank_nitro from the chassis after updatevariables.
  // Read the same live loaded script field without inventing a tank capacity.
  values.nitro = carHudNitro(car.nitrous, floatField(car.chassisObject, "tank_nitro"));
  auto *chassis = car.chassisObject ? world.findByReceiver(car.chassisObject) : nullptr;
  const auto *runtime = chassis && chassis->carInstance ? chassis->carInstance.get() : &car;
  // FUN_00447b30 obtains dynodata from the engine installed in chassis slot 0x191.
  const VmObject *dyno = nullptr;
  for (const auto &slot : runtime->slots)
    if (slot && slot->id == 0x191) {
      if (auto *engine = world.nodeFor(slot->attachedNode.owner))
        dyno = objectField(engine->scriptReceiver, "dynodata");
      break;
    }
  if (dyno) {
    CarHudTurbo turbo;
    // FUN_0046aa20: native +dc/+e0/+e4/+e8/+ec/+f0.
    turbo.rpmMultiplier = floatField(dyno, "rpm_turbo_mul");
    turbo.optimumRpm = floatField(dyno, "rpm_turbo_opt");
    turbo.range = floatField(dyno, "rpm_turbo_range");
    turbo.maximumPressure = floatField(dyno, "P_turbo_max");
    turbo.wastePressure = turbo.maximumPressure;
    for (const auto &field : dyno->fields)
      if (field.name == "P_turbo_waste")
        if (const auto *value = std::get_if<float>(&field.value))
          turbo.wastePressure = *value;
    values.turbo = carHudTurboValue(turbo, std::fabs(car.displayedEngineSpeed * 9.549296379089355f));
  }
  return values;
}


bool carHudModel(ResourceWorld& resources, GameWorld& world, CarSeat& seat,
                 size_t index, uint32_t materialId, const std::array<float, 3>& position,
                 float roll, bool visible, std::string& error) {
  constexpr std::array<size_t, 6> slots{26, 28, 34, 35, 36, 37};
  if (index >= slots.size()) {
    error = "HUD model index unavailable";
    return false;
  }
  auto& link = seat.links[slots[index]];
  auto& instance = seat.hudModels[index];
  if (!visible) {
    if (auto* node = world.nodeFor(link.owner))
      world.destroy(*node);
    instance.reset();
    error.clear();
    return true;
  }
  if (!instance || !link.owner) {
    auto* parent = world.nodeFor(seat.osd.owner);
    if (!parent) {
      error = "HUD model parent unavailable";
      return false;
    }
    const auto found = resources.lookup(materialId, 14);
    if (found.status != LookupStatus::Found) {
      error = "HUD model material unavailable";
      return false;
    }
    auto material = resources.sceneMaterial(found.node);
    if (!material) {
      auto loaded = sceneLoadMaterial(resources, materialId);
      if (!loaded.material) {
        error = "HUD model material: " + loaded.error;
        return false;
      }
      material = std::move(loaded.material);
      if (!resources.bindSceneMaterial(found.node, material)) {
        error = "HUD model material binding failed";
        return false;
      }
    }
    SceneMaterialCache cache;
    if (!sceneRefreshMaterial(resources, *material, resources.packs().root(), cache, error))
      return false;
    auto* node = world.addTypedChild(*parent, 3, "gauge", 0x200);
    if (!node) {
      error = "HUD model allocation failed";
      return false;
    }
    node->bounds = ResourceBounds{};
    node->field6c = parent->field6c;
    node->field70 = parent->field70;
    world.markLoaded(*node);
    node->loadPriority = 1.0f;
    instance = std::make_shared<SceneRenderInstance>(found.node.owner);
    instance->setCameraPlane(true);
    if (!resources.bindSceneRenderInstance(resources.resolve(node), instance)) {
      world.destroy(*node);
      instance.reset();
      error = "HUD model binding failed";
      return false;
    }
    setLinkOwner(link, node);
  }
  const float rotation[3]{0, 0, roll};
  instance->setBoneMatrix(position.data(), rotation);
  error.clear();
  return true;
}
