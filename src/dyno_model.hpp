#pragma once

#include <array>
#include <functional>
#include <span>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// DynoData script dimensions are metres and cubic metres. FUN_0046aa20 copies
// these fields into the native combustion model; T_loss is not copied.
struct DynoParameters {
  float cylinders = 4, bore = 0.082f, stroke = 0.090f, minimumVolume = 0.00005f;
  float intakeMinimum = 0.005f, intakeMaximum = 0.8f;
  float exhaustMinimum = 0.005f, exhaustMaximum = 0.8f;
  float intakeOpen = 0.001f, intakeClose = 0.25f;
  float exhaustOpen = 0.75f, exhaustClose = 0.99f;
  float sparkMinimum = 0.48f, sparkIncrement = -0.05f;
  float sparkRpm0 = 2000, sparkRpm1 = 5000, burnTime = 0.003f;
  float rpmLimit = 7000, maximumRpm = 7000;
  float turboMultiplier = 16, turboOptimum = 85000, turboRange = 20000;
  float turboPressure = 0, wastePressure = 10, intercooling = 1;
  float mixtureHeat = 1000000, mixtureRatio = 14;
  float maximumFuelConsumption = 0, maximumAirConsumption = 0;
  float nitroHeat = 1, nitroCooling = 1;
};

struct DynoCurve {
  // Native tables retain negative torque. The legacy script-array path clamps
  // its copy, not the stored curve. Power samples are watts (FUN_004b77f0).
  std::vector<float> torque, power;
};

struct DynoResult {
  int stepRpm = 0;
  float requestedStepRpm = 0, maximumRpm = 0;
  float displacement = 0, compression = 0;
  float maximumTorque = 0, maximumHorsepower = 0;
  float maximumTorqueRpm = 0, maximumHorsepowerRpm = 0;
  DynoCurve primary, nitro;
};

enum class DynoPhase { Reset,
                       Inputs,
                       Geometry,
                       Prepare,
                       CycleInit,
                       TemperaturePower,
                       Step,
                       CycleResult };

// The span is valid only during the callback. Word93 is reserved in the portable
// float state; native integer cycle completion is represented explicitly below.
struct DynoScalar {
  float input, exponent, result;
};

struct DynoObservation {
  DynoPhase phase;
  std::span<const float, 116> state;
  unsigned bank, cycle, step;
  bool cycleCompleted;
  std::optional<DynoScalar> scalar;
};
using DynoObserver = std::function<void(const DynoObservation&)>;

// Owns the persistent native model attached to one DynoData. Recalculation
// preserves fields which FUN_004b6670 does not reset, including the preceding
// consumption observations used by the first RPM preparation.
class DynoModel {
public:
  std::variant<DynoResult, std::string> calculate(const DynoParameters& parameters,
                                                  float tableSize, const DynoObserver& observer = {});
  const DynoResult* result() const { return result_ ? &*result_ : nullptr; }

private:
  std::array<float, 116> state_{};
  std::optional<DynoResult> result_;
};
