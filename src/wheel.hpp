#pragma once

#include <array>
#include <cstdint>
#include <optional>

// 0043bbd0: wheel and road velocities are in the wheel's local frame.
// These parameters are the stock globals 0060e02c..0060e048, supplied by
// the caller because runtime configuration may change them.
struct WheelSlipParameters {
  float longitudinalLimit, lateralLimit;
  float longitudinalRelaxation, lateralRelaxation;
  float transitionScale, speedThreshold;
  float longitudinalDamping, lateralDamping;
};
struct WheelSlipInput {
  float wheelSpeed, roadSpeed, lateralSpeed, step;
  float previousLongitudinal, previousAngle;
};
struct WheelSlipState { float longitudinal, angle; };
std::optional<WheelSlipState> updateWheelSlip(
    const WheelSlipInput& input, const WheelSlipParameters& parameters);
// 00454500 calls 0043bbd0 only for a loaded contact with a resolved surface.
enum class WheelSlipContact { NoSuspension, Separated, NoLoad, NoSurface, Loaded };
std::optional<WheelSlipState> updateWheelSlipForContact(
    const WheelSlipInput& input, const WheelSlipParameters& parameters,
    WheelSlipContact contact);

// 0043ba80 clamps the curve to nonnegative; 0043bb30 preserves its sign.
enum class WheelCurveMode { Nonnegative, Signed };
inline constexpr std::array<float, 11> kStockSignedWheelCurve{
    -2.3f, 0, 1.49f, 0, 15.2f, 0, 0, 0, -1, 0, 0};
std::optional<float> evaluateWheelCurve(const std::array<float, 11>& coefficients,
                                        float slip, float load, WheelCurveMode mode);

// Newer prototype table005da8b0 (older0060e068). Config wheelpars overwrites
// indices0,2,4,11..16; the remaining coefficients retain these exact defaults.
inline constexpr std::array<float, 17> kStockWheelParameters{
    1.4f, 0, 1.49f, 0, 15.2f, 0, -0.0f, 0, -1, 0, 0,
    8000, 1, .015f, .4f, 0, 0};

// Partial wheel state established by the stock setter handlers. Names for the
// friction fields describe their role in those handlers, not the full physics.
struct Wheel {
	std::array<float, 17> pacejka{};
	float frictionScale = 0.0f;
	float friction64 = 0.0f;
	float friction68 = 0.0f;
	float friction6c = 0.0f;
	float friction70 = 0.0f;

	void setPacejka(int32_t index, float value);
	void setSliction(float value);
	void setFriction(float value);
	void setStiffness(float value);
	void setMaxLoad(float value);
	void setFrictn_x(float value);
	void setLoadSmooth(float value);
};

// 00454500 tire force in the wheel's local frame after slip correction.
// The caller supplies the actual contact normal and resolved surface factor;
// this does not apply a force or resolve a surface.
struct WheelContactForceInput {
  std::array<float, 3> localNormal;
  float lateralForce, longitudinalForce;
  float effectiveLoad, localLongitudinalVelocity;
  float surfaceRolling, wheelRolling;
};
std::optional<std::array<float, 3>> wheelContactLocalForce(const WheelContactForceInput& input);

// 00454500 loaded, resolved-surface branch. Inputs are live values; this
// calculates force and wheel deltas without mutating the wheel or body.
struct TireSlipForceInput {
  std::array<float, 11> curve;
  WheelSlipInput slip;
  WheelSlipParameters slipParameters;
  float effectiveLoad, surfaceFriction, wheelFriction;
  float stiffnessNumerator, stiffnessDenominator;
  float longitudinalScale, radius, inverseWheelInertia, angularSpeed;
  float angularGain; // live 0060e020
};
struct TireSlipForceResult {
  WheelSlipState slip;
  float magnitude, lateralForce, longitudinalForce, aligningForce;
  float angularDelta, torqueDelta;
};
std::optional<TireSlipForceResult> calculateTireSlipForce(const TireSlipForceInput& input);
