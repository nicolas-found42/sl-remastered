#include "wheel.hpp"

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <cmath>

std::optional<float> evaluateWheelCurve(const std::array<float, 11>& c,
                                        float slip, float load, WheelCurveMode mode) {
  if (!std::isfinite(slip) || !std::isfinite(load) || load < 0 ||
      (mode != WheelCurveMode::Nonnegative && mode != WheelCurveMode::Signed)) return std::nullopt;
  for (float value : c) if (!std::isfinite(value)) return std::nullopt;
  // x87 control word 0x007f rounds basic arithmetic to a 24-bit
  // significand; transcendental instructions retain extra precision.
  // Float conversion also bounds the exponent, unlike x87; extreme inputs
  // remain outside this bounded model.
  const auto r = [](double value) { return static_cast<float>(value); };
  const float amplitude = r(r(static_cast<double>(load) * c[1]) + c[2]);
  const float denominator = r(static_cast<double>(amplitude) * c[0]);
  if (denominator == 0) return 0.0f;
  const float exponent = r(-static_cast<double>(r(static_cast<double>(load) * c[5])) * 1.4426950408889634);
  if (!std::isfinite(exponent) || exponent < -120 || exponent > 120) return std::nullopt;
  const float integral = std::nearbyint(exponent);
  const float fraction = r(static_cast<double>(exponent) - integral);
  const float expFraction = r(1.0 + std::expm1(static_cast<double>(fraction) * std::log(2.0)));
  const double decay = std::scalbn(static_cast<double>(expFraction), static_cast<int>(integral));
  const float scale = r(static_cast<double>(r(static_cast<double>(r(static_cast<double>(load) * c[3]) + c[4]) * decay)) / denominator);
  const float offset = r(static_cast<double>(r(static_cast<double>(r(static_cast<double>(load) * c[9]) + c[10])) + slip));
  const float argument = r(static_cast<double>(offset) * scale);
  const float angleDifference = r(std::atan(static_cast<double>(argument)) - argument);
  const float quadratic = r(static_cast<double>(r(static_cast<double>(load) * load)) * c[6]);
  const float curvature = r(static_cast<double>(r(static_cast<double>(r(static_cast<double>(load) * c[7]) + quadratic))) + c[8]);
  const float angleInput = r(static_cast<double>(r(static_cast<double>(curvature) * angleDifference)) + argument);
  const float angle = r(std::atan(static_cast<double>(angleInput)) * c[0]);
  const double result = r(static_cast<double>(r(static_cast<double>(amplitude) * load)) * std::sin(static_cast<double>(angle)));
  if (!std::isfinite(result)) return std::nullopt;
  const float rounded = static_cast<float>(mode == WheelCurveMode::Nonnegative && result < 0 ? 0 : result);
  if (!std::isfinite(rounded)) return std::nullopt;
  return rounded;
}

std::optional<WheelSlipState> updateWheelSlip(
    const WheelSlipInput& in, const WheelSlipParameters& p) {
  const float values[] = {in.wheelSpeed, in.roadSpeed, in.lateralSpeed, in.step,
      in.previousLongitudinal, in.previousAngle, p.longitudinalLimit, p.lateralLimit,
      p.longitudinalRelaxation, p.lateralRelaxation, p.transitionScale, p.speedThreshold,
      p.longitudinalDamping, p.lateralDamping};
  for (float value : values) if (!std::isfinite(value)) return std::nullopt;
  if (in.step < 0 || p.longitudinalLimit < 0 || p.lateralLimit < 0 ||
      p.longitudinalRelaxation <= 0 || p.lateralRelaxation <= 0 ||
      p.transitionScale < 0 || p.speedThreshold < 0 ||
      p.longitudinalDamping < 0 || p.lateralDamping < 0) return std::nullopt;
  const float speed = std::fabs(in.roadSpeed);
  const float transition = std::clamp((speed - p.speedThreshold) * p.transitionScale, 0.0f, 1.0f);
  float targetLongitudinal = 0, targetAngle = 0;
  if (speed > p.speedThreshold) {
    targetLongitudinal = (in.wheelSpeed - in.roadSpeed) / speed;
    targetAngle = std::atan(in.lateralSpeed / speed);
  }
  if (transition >= 1) {
    if (!std::isfinite(targetLongitudinal) || !std::isfinite(targetAngle)) return std::nullopt;
    return WheelSlipState{targetLongitudinal, targetAngle};
  }
  const float longDenominator = (in.step * 500.0f * p.longitudinalDamping + 1) * p.longitudinalRelaxation;
  const float angleDenominator = (in.step * 500.0f * p.lateralDamping + 1) * p.lateralRelaxation;
  if (!std::isfinite(longDenominator) || !std::isfinite(angleDenominator) ||
      longDenominator <= 0 || angleDenominator <= 0) return std::nullopt;
  const float relaxedLong = std::clamp((p.longitudinalRelaxation * in.previousLongitudinal +
      (in.wheelSpeed - in.roadSpeed) * in.step) / longDenominator,
      -p.longitudinalLimit, p.longitudinalLimit);
  const float relaxedAngle = std::clamp((in.lateralSpeed * in.step +
      p.lateralRelaxation * in.previousAngle) / angleDenominator, -p.lateralLimit, p.lateralLimit);
  const WheelSlipState result{targetLongitudinal * transition + (1 - transition) * relaxedLong,
                              targetAngle * transition + (1 - transition) * relaxedAngle};
  if (!std::isfinite(result.longitudinal) || !std::isfinite(result.angle)) return std::nullopt;
  return result;
}

std::optional<WheelSlipState> updateWheelSlipForContact(
    const WheelSlipInput& in, const WheelSlipParameters& p, WheelSlipContact contact) {
  switch (contact) {
  case WheelSlipContact::NoSuspension:
  case WheelSlipContact::Separated:
  case WheelSlipContact::NoSurface:
    return WheelSlipState{0, 0};
  case WheelSlipContact::NoLoad:
    if (!std::isfinite(in.previousLongitudinal) || !std::isfinite(in.previousAngle)) return std::nullopt;
    return WheelSlipState{in.previousLongitudinal, in.previousAngle};
  case WheelSlipContact::Loaded:
    return updateWheelSlip(in, p);
  }
  return std::nullopt;
}

void Wheel::setPacejka(int32_t index, float value) {
	if (index >= 0 && index < 17) pacejka[static_cast<std::size_t>(index)] = value;
}

void Wheel::setSliction(float value) { pacejka[0] = value; }
void Wheel::setStiffness(float value) { pacejka[4] = value; }
void Wheel::setMaxLoad(float value) { pacejka[11] = value; }
void Wheel::setFrictn_x(float value) { pacejka[12] = value; }
void Wheel::setLoadSmooth(float value) { pacejka[14] = value; }

void Wheel::setFriction(float value) {
	// The stock constant has these exact bits, rather than those of 0.1f.
	const uint32_t minimumBits = 0x3dccccd0;
	float minimum;
	static_assert(sizeof minimum == sizeof minimumBits);
	std::memcpy(&minimum, &minimumBits, sizeof minimum);
	const float factor = frictionScale < 1.0f
		? 1.0f - frictionScale * frictionScale * 0.6f : minimum;
	pacejka[2] = value;
	friction64 = value;
	friction68 = value;
	friction6c = factor * value;
	friction70 = factor * value;
}

std::optional<std::array<float, 3>> wheelContactLocalForce(const WheelContactForceInput& in) {
  const float values[] = {in.localNormal[0], in.localNormal[1], in.localNormal[2],
      in.lateralForce, in.longitudinalForce, in.effectiveLoad,
      in.localLongitudinalVelocity, in.surfaceRolling, in.wheelRolling};
  for (float v : values) if (!std::isfinite(v)) return std::nullopt;
  if (in.effectiveLoad < 0) return std::nullopt;
  const auto& n = in.localNormal;
  std::array<float, 3> lateral{n[1], -n[0], 0};
  const float lateralLength = std::sqrt(lateral[0]*lateral[0] + lateral[1]*lateral[1]);
  if (lateralLength != 0) for (float& v : lateral) v /= lateralLength;
  std::array<float, 3> longitudinal{
      lateral[1]*n[2], -lateral[0]*n[2], lateral[0]*n[1] - lateral[1]*n[0]};
  const float longitudinalLength = std::sqrt(longitudinal[0]*longitudinal[0] +
      longitudinal[1]*longitudinal[1] + longitudinal[2]*longitudinal[2]);
  if (longitudinalLength != 0) for (float& v : longitudinal) v /= longitudinalLength;
  const float z = in.effectiveLoad * in.localLongitudinalVelocity *
      (in.surfaceRolling + in.wheelRolling) * 0.01f - in.longitudinalForce;
  std::array<float, 3> result{};
  for (std::size_t i = 0; i < 3; ++i) {
    result[i] = in.lateralForce*lateral[i] + z*longitudinal[i];
    if (!std::isfinite(result[i])) return std::nullopt;
  }
  return result;
}

std::optional<TireSlipForceResult> calculateTireSlipForce(const TireSlipForceInput& in) {
  const float values[] = {in.effectiveLoad, in.surfaceFriction, in.wheelFriction,
      in.stiffnessNumerator, in.stiffnessDenominator, in.longitudinalScale,
      in.radius, in.inverseWheelInertia, in.angularSpeed, in.angularGain};
  for (float v : values) if (!std::isfinite(v)) return std::nullopt;
  if (in.effectiveLoad < 0 || in.radius < 0 || in.inverseWheelInertia < 0) return std::nullopt;
  WheelSlipInput slipInput = in.slip;
  slipInput.wheelSpeed = -(in.angularSpeed * in.radius);
  auto slip = updateWheelSlip(slipInput, in.slipParameters);
  if (!slip) return std::nullopt;
  const double sine = std::sin(static_cast<double>(slip->angle));
  const double cosine = std::cos(static_cast<double>(slip->angle));
  const double scaledLong = cosine * slip->longitudinal * in.longitudinalScale;
  const float magnitude = static_cast<float>(std::sqrt(sine*sine + scaledLong*scaledLong));
  if (!std::isfinite(magnitude)) return std::nullopt;
  TireSlipForceResult out{*slip, magnitude, 0, 0, 0, 0, 0};
  if (magnitude <= 0.0001f) return out;
  auto curve = in.curve;
  curve[2] = in.surfaceFriction * in.wheelFriction;
  if (in.stiffnessDenominator > 0) {
    const float denominator = in.effectiveLoad + in.stiffnessDenominator;
    if (!std::isfinite(denominator) || denominator == 0) return std::nullopt;
    curve[4] *= (in.stiffnessNumerator + in.stiffnessDenominator) / denominator;
  }
  const auto force = evaluateWheelCurve(curve, magnitude, in.effectiveLoad, WheelCurveMode::Nonnegative);
  const auto aligning = evaluateWheelCurve(kStockSignedWheelCurve, slip->angle,
                                            in.effectiveLoad, WheelCurveMode::Signed);
  if (!force || !aligning) return std::nullopt;
  out.lateralForce = static_cast<float>((sine / magnitude) * *force);
  out.longitudinalForce = static_cast<float>((scaledLong / magnitude) * *force);
  out.aligningForce = *aligning * curve[2];
  float delta = in.slip.step * 50.0f * in.inverseWheelInertia * out.longitudinalForce *
                in.radius * in.angularGain;
  slipInput.previousLongitudinal = in.slip.previousLongitudinal;
  slipInput.previousAngle = in.slip.previousAngle;
  slipInput.wheelSpeed = -((delta + in.angularSpeed) * in.radius);
  const auto predicted = updateWheelSlip(slipInput, in.slipParameters);
  if (!predicted) return std::nullopt;
  if (predicted->longitudinal * slip->longitudinal < 0) {
    const float fraction = std::fabs(slip->longitudinal /
                                      (predicted->longitudinal - slip->longitudinal));
    out.longitudinalForce *= fraction;
    delta *= fraction;
  }
  out.angularDelta = delta;
  out.torqueDelta = delta * in.radius;
  const float results[] = {out.lateralForce,out.longitudinalForce,out.aligningForce,
                           out.angularDelta,out.torqueDelta};
  for (float v : results) if (!std::isfinite(v)) return std::nullopt;
  return out;
}
