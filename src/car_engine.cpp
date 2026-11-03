#include "car_engine.hpp"
#include <cmath>

float sampleCarTorqueCurve(const std::vector<std::array<float, 2>> &curve,
                           float speed) {
  if (curve.empty())
    return 0.0f;
  if (speed < curve.front()[0])
    return 0.0f;
  for (size_t i = 1; i < curve.size(); ++i) {
    if (speed < curve[i][0]) {
      const auto &a = curve[i - 1];
      const auto &b = curve[i];
      return ((speed - a[0]) * (b[1] - a[1])) / (b[0] - a[0]) + a[1];
    }
  }
  return 0.0f;
}

float carEngineTorque(const CarConfig &config, float speed, float load,
                      const std::array<float, 2> &scales) {
  const auto primary = sampleCarTorqueCurve(config.torqueCurve, speed);
  const auto secondary =
      sampleCarTorqueCurve(config.secondaryTorqueCurve, speed);
  return (secondary * scales[1]) * load + ((1.0f - load) * primary) * scales[0];
}

std::optional<PrimaryEngineOutput>
primaryEngineTorque(const CarConfig &config, const PrimaryEngineInput &input) {
  if (!input.engineRunning || input.intermittentCut || input.nitrous ||
      !std::isfinite(input.angularSpeed) || !std::isfinite(input.previousRpm) ||
      !std::isfinite(input.rpmLimit) ||
      !std::isfinite(input.idleAngularSpeed) ||
      !std::isfinite(input.accelerator) || !std::isfinite(input.powerScale) ||
      input.angularSpeed < input.idleAngularSpeed ||
      // 004aebf0 installs one zero point when no native engine table exists.
      // 0043be70 samples that legal curve as zero at every speed.
      input.idleAngularSpeed < 0 || config.torqueCurve.empty())
    return std::nullopt;
  for (size_t i = 0; i < config.torqueCurve.size(); ++i) {
    if (!std::isfinite(config.torqueCurve[i][0]) ||
        !std::isfinite(config.torqueCurve[i][1]) ||
        (i && config.torqueCurve[i][0] <= config.torqueCurve[i - 1][0]))
      return std::nullopt;
  }
  const float rpm = input.angularSpeed * 9.549296379089355f;
  if (!std::isfinite(rpm) || std::fabs(rpm) <= 0.0010000000474974513f)
    return std::nullopt;
  const float demand = std::fabs(input.accelerator);
  // The starter branch suppresses accelerator torque and adds a separate force.
  if (demand > 0.1f && input.angularSpeed <= input.idleAngularSpeed * 0.95f)
    return std::nullopt;
  const float predicted = (rpm - input.previousRpm) + rpm;
  if (!std::isfinite(predicted))
    return std::nullopt;
  float limiter = 1;
  if (predicted <= input.rpmLimit) {
    if (input.rpmLimit < rpm)
      limiter = (input.rpmLimit - predicted) / (rpm - predicted);
  } else if (input.rpmLimit < rpm) {
    limiter = 0;
  } else {
    limiter = (input.rpmLimit - rpm) / (predicted - rpm);
  }
  const float torque = demand <= 0.01f
                           ? 0.0f
                           : (sampleCarTorqueCurve(config.torqueCurve, rpm) *
                              input.powerScale * limiter * demand) *
                                 2.0f;
  if (!std::isfinite(torque) || !std::isfinite(limiter))
    return std::nullopt;
  return PrimaryEngineOutput{torque, rpm, limiter};
}

std::optional<ClutchExchangeOutput>
calculateClutchExchange(const ClutchExchangeInput &in) {
  const float values[] = {in.engineSpeed,
                          in.wheelSpeed,
                          in.engineImpulse,
                          in.engineInertia,
                          in.transmissionInertia,
                          in.engineInertiaFactor,
                          in.gearRatio,
                          in.finalDrive,
                          in.clutch,
                          in.previousDisplayedSpeed,
                          in.drivenWeightSum};
  for (float value : values)
    if (!std::isfinite(value))
      return std::nullopt;
  if (in.engineInertia <= 0 || in.transmissionInertia <= 0 ||
      in.engineInertiaFactor <= 0 || in.drivenWeightSum <= 0 || in.clutch < 0 ||
      in.clutch > 1)
    return std::nullopt;
  const float ratio = in.finalDrive * in.gearRatio;
  if (!std::isfinite(ratio) || ratio == 0)
    return std::nullopt;
  float factor = 1.0f / ((ratio * ratio) / in.transmissionInertia);
  factor =
      factor / (in.engineInertiaFactor / (1.0f / in.engineInertia) + factor);
  if (!std::isfinite(factor) || factor <= 0)
    return std::nullopt;
  const float displayed =
      ((in.engineSpeed -
        ((1.0f - factor) * in.engineImpulse) / in.engineInertia) -
       in.previousDisplayedSpeed) *
          0.25f +
      in.previousDisplayedSpeed;
  float engine = in.engineImpulse / in.engineInertia + in.engineSpeed;
  float wheel = in.wheelSpeed * ratio;
  const float exchange = (wheel - engine) * factor;
  const float grip = 1.0f - in.clutch;
  wheel = ((exchange + engine) - wheel) * grip * 0.9990000128746033f + wheel;
  engine = ((exchange + engine) - engine) * grip * 0.9990000128746033f + engine;
  const float wheelSpeed = wheel / ratio;
  const float acceleration = (wheelSpeed - in.wheelSpeed) / in.drivenWeightSum;
  const float force = ((grip * exchange) / (factor * factor)) * 100.0f;
  if (!std::isfinite(displayed) || !std::isfinite(engine) ||
      !std::isfinite(wheelSpeed) || !std::isfinite(acceleration) ||
      !std::isfinite(force))
    return std::nullopt;
  return ClutchExchangeOutput{engine, wheelSpeed, displayed, acceleration,
                              force};
}

std::optional<DrivenWheelExchange>
calculateDrivenWheelExchange(ClutchExchangeInput clutch,
                             const std::vector<DrivenWheelState> &wheels) {
  if (wheels.size() > 16)
    return std::nullopt;
  float allWeights = 0, eligibleWeights = 0, aggregate = 0;
  constexpr float threshold = 0.0010000000474974513f;
  for (const auto &wheel : wheels) {
    if (!wheel.suspensionPresent)
      continue;
    if (!std::isfinite(wheel.driveWeight) ||
        !std::isfinite(wheel.transferCoefficient) ||
        !std::isfinite(wheel.angularSpeed) || !std::isfinite(wheel.radius) ||
        !std::isfinite(wheel.longitudinal))
      return std::nullopt;
    allWeights += std::fabs(wheel.driveWeight);
    if (wheel.driveWeight > threshold &&
        std::fabs(wheel.transferCoefficient) > threshold) {
      aggregate =
          eligibleWeights * aggregate +
          -(wheel.angularSpeed * wheel.transferCoefficient) * wheel.driveWeight;
      eligibleWeights += std::fabs(wheel.driveWeight);
      aggregate /= eligibleWeights;
    }
  }
  if (!std::isfinite(allWeights) || !std::isfinite(aggregate) ||
      allWeights <= 0)
    return std::nullopt;
  clutch.wheelSpeed = aggregate;
  clutch.drivenWeightSum = allWeights;
  const auto exchange = calculateClutchExchange(clutch);
  if (!exchange)
    return std::nullopt;
  DrivenWheelExchange result{*exchange, wheels};
  for (auto &wheel : result.wheels) {
    if (!wheel.suspensionPresent || wheel.driveWeight <= threshold ||
        std::fabs(wheel.transferCoefficient) <= threshold)
      continue;
    const float delta =
        -(exchange->wheelDeltaPerDriveWeight * wheel.driveWeight) /
        wheel.transferCoefficient;
    wheel.angularSpeed += delta;
    wheel.longitudinal += delta * wheel.radius;
    if (!std::isfinite(wheel.angularSpeed) ||
        !std::isfinite(wheel.longitudinal))
      return std::nullopt;
  }
  return result;
}

std::optional<WheelBrakeResult> calculateWheelBrake(const WheelBrakeInput &in) {
  WheelBrakeResult result{in.wheel, in.wheelBrake, in.wheelHandbrake,
                          in.bearing, in.thermal};
  if (in.sleeping || !in.wheel.suspensionPresent)
    return result;
  constexpr float epsilon = 0.0010000000474974513f;
  const float values[] = {in.wheel.angularSpeed,
                          in.wheel.radius,
                          in.wheel.longitudinal,
                          in.brake,
                          in.handbrake,
                          in.wheelExtraBrake,
                          in.carBrakeStrength,
                          in.carHandbrakeStrength,
                          in.wheelBrake,
                          in.wheelHandbrake,
                          in.bearing,
                          in.effectiveInverseInertia,
                          in.brakeFactor,
                          in.step};
  for (float value : values)
    if (!std::isfinite(value))
      return std::nullopt;
  if (in.step < 0.00009999999747378752f || in.effectiveInverseInertia < 0 ||
      in.brakeFactor < 0)
    return std::nullopt;
  if (std::fabs(in.wheel.angularSpeed) <= epsilon)
    return result;
  if (in.damageEnabled) {
    if (!result.thermal)
      return std::nullopt;
    const auto &t = *result.thermal;
    const float thermalValues[] = {
        t.temperature,   t.ambientTemperature, t.cooling,   t.heatCapacity,
        t.wear,          t.wearRate,           t.baseBrake, t.baseHandbrake,
        t.internalDamage};
    for (float value : thermalValues)
      if (!std::isfinite(value))
        return std::nullopt;
    if (t.heatCapacity <= 0)
      return std::nullopt;
  }
  const float strength =
      in.carHandbrakeStrength * std::fabs(in.handbrake) * in.wheelHandbrake +
      (in.wheelExtraBrake + std::fabs(in.brake)) * in.carBrakeStrength *
          in.wheelBrake +
      in.bearing;
  if (!std::isfinite(strength))
    return std::nullopt;
  if (strength > 0) {
    const float amount = (in.step * 500.0f) * in.effectiveInverseInertia *
                         strength * in.brakeFactor;
    if (!std::isfinite(amount) || amount < 0)
      return std::nullopt;
    auto &wheel = result.wheel;
    if (wheel.angularSpeed <= amount) {
      if (-amount <= wheel.angularSpeed) {
        wheel.longitudinal -= wheel.radius * wheel.angularSpeed;
        wheel.angularSpeed = 0;
      } else {
        wheel.angularSpeed += amount;
        wheel.longitudinal += amount * wheel.radius;
      }
    } else {
      wheel.angularSpeed -= amount;
      wheel.longitudinal -= amount * wheel.radius;
    }
    if (in.damageEnabled) {
      auto &t = *result.thermal;
      const float work = (strength - in.bearing) * 10.0f *
                         std::fabs(wheel.angularSpeed) * (in.step * 500.0f) *
                         0.0010000000474974513f;
      if (!std::isfinite(work))
        return std::nullopt;
      if (work > 0) {
        t.temperature += work / t.heatCapacity;
        if (t.wear < 1 && work * t.internalDamage > 0) {
          t.wear += work * t.internalDamage * t.wearRate;
          result.negativeWearWarning = t.wear < 0;
          if (t.wear >= 1)
            t.wear = 1;
          const float scale = t.wear < 1
                                  ? 1.0f - t.wear * t.wear * 0.6000000238418579f
                                  : 0.10000002384185791f;
          result.wheelBrake = scale * t.baseBrake;
          result.wheelHandbrake = scale * t.baseHandbrake;
          if (t.wear >= 1) {
            result.bearing += 100;
            result.wheelBrake *= 0.20000000298023224f;
            result.wheelHandbrake *= 0.20000000298023224f;
            result.failureSoundRequested = t.failureSoundEnabled;
          }
        }
      }
    }
  }
  if (in.damageEnabled) {
    auto &t = *result.thermal;
    const float difference = t.temperature - t.ambientTemperature;
    t.temperature -= std::fabs(difference) * t.cooling * difference * in.step;
    if (!std::isfinite(t.temperature) || !std::isfinite(t.wear))
      return std::nullopt;
  }
  if (!std::isfinite(result.wheel.angularSpeed) ||
      !std::isfinite(result.wheel.longitudinal) ||
      !std::isfinite(result.wheelBrake) ||
      !std::isfinite(result.wheelHandbrake) || !std::isfinite(result.bearing))
    return std::nullopt;
  return result;
}
