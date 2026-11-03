#include "dyno_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
// Word offsets in the source-bound 0x1ec-byte native model. Integer cycle and
// table metadata and owning pointers are kept separately, never cast to floats.
enum Word : size_t {
  crank = 0,
  piston = 1,
  rod = 3,
  vmin = 4,
  cylinders = 5,
  burnTime = 7,
  burnSpeed = 8,
  density = 9,
  heatCapacity = 10,
  intakePressure = 11,
  exhaustPressure = 12,
  intakeTemperature = 13,
  exhaustTemperature = 14,
  mixtureHeat = 15,
  exhaustHeat = 16,
  intakeMinimum = 17,
  exhaustMinimum = 18,
  intakeMaximum = 19,
  exhaustMaximum = 20,
  thermalTime = 21,
  rpmLimit = 22,
  bore = 23,
  stroke = 24,
  displacement = 25,
  area = 26,
  sweptVolume = 27,
  vmax = 28,
  compression = 29,
  speedLimit = 30,
  intakeDensity = 31,
  exhaustDensity = 32,
  dt = 33,
  rpm = 34,
  maximumRpm = 35,
  spark = 36,
  intakeOpen = 37,
  intakeClose = 38,
  exhaustOpen = 39,
  exhaustClose = 40,
  intakeStart = 41,
  intakeDuration = 42,
  intakeShift = 43,
  intakeRpm0 = 44,
  intakeRpm1 = 45,
  exhaustStart = 46,
  exhaustDuration = 47,
  exhaustShift = 48,
  exhaustRpm0 = 49,
  exhaustRpm1 = 50,
  sparkStart = 51,
  sparkShift = 52,
  sparkRpm0 = 53,
  sparkRpm1 = 54,
  turboMultiplier = 55,
  turboOptimum = 56,
  turboPlateau = 57,
  turboRange = 58,
  turboPressure = 59,
  wastePressure = 60,
  mixtureRatio = 61,
  fuelLimit = 62,
  airLimit = 63,
  intakeValve = 64,
  exhaustValve = 65,
  burned = 66,
  ignitionTime = 67,
  wallTemperature = 68,
  ambientTemperature = 69,
  heatLoss = 70,
  otherHeat = 71,
  elapsed = 72,
  period = 73,
  angularSpeed = 74,
  angle = 75,
  pistonPosition = 76,
  volume = 77,
  gasDensity = 78,
  mass = 79,
  pressure = 80,
  temperature = 81,
  fuelHeat = 82,
  airFraction = 83,
  fuelFraction = 84,
  effectiveHeat = 85,
  effectivePressure = 86,
  effectiveTemperature = 87,
  effectiveDensity = 88,
  effectiveBurnTime = 89,
  fuelUsed = 90,
  airUsed = 91,
  pistonSpeed = 92,
  work = 94,
  previousWork = 95,
  frictionPower = 96,
  meanPressure = 97,
  torque = 98,
  horsepower = 99,
  grossPower = 100,
  netPower = 101,
  fuelConsumption = 102,
  airConsumption = 103,
  specificConsumption = 104,
  efficiency = 105,
  peakPressure = 106,
  peakTorque = 107,
  peakHorsepower = 108,
  peakGrossPower = 109,
  peakNetPower = 110,
  peakPressureRpm = 111,
  peakTorqueRpm = 112,
  peakHorsepowerRpm = 113,
  peakGrossRpm = 114,
  peakNetRpm = 115
};
using State = std::array<float, 116>;
void observe(const DynoObserver& observer, DynoPhase phase, const State& state,
             unsigned bank = 0, unsigned cycle = 0, unsigned step = 0,
             bool completed = false, std::optional<DynoScalar> scalar = {}) {
  if (observer)
    observer({phase, state, bank, cycle, step, completed, scalar});
}

// [INFERENCE] The source-bound newer Float32 pow boundary differs from the
// platform powf at the first observed temperature divergence. Promoting the
// exact Float32 inputs and rounding the double result matches that witness;
// complete table/branch qualification is retained separately.
float referencePower(float input, float exponent) {
  return static_cast<float>(std::pow(static_cast<double>(input), static_cast<double>(exponent)));
}

// FUN_004b68c0. The reset's volume expression has the opposite sign from
// the advancing crank expression; retaining that grouping matters at TDC.
void resetCycle(State& s) {
  s[fuelHeat] = s[effectiveHeat];
  s[pressure] = s[effectivePressure];
  for (auto field : {elapsed, angle, intakeValve, exhaustValve, burned, heatLoss,
                     otherHeat, work, previousWork, meanPressure, grossPower,
                     fuelUsed, airUsed})
    s[field] = 0;
  s[temperature] = s[effectiveTemperature];
  const float position = s[crank] + std::sqrt(s[rod] * s[rod]);
  s[pistonPosition] = position;
  s[volume] = ((position - s[rod]) - s[crank]) * s[area] + s[vmin];
  s[gasDensity] = s[effectiveDensity];
  s[mass] = s[volume] * s[effectiveDensity];
}

// Newer FUN_004f5980 computes the reciprocal once before the air fraction.
void mixture(State& s) {
  s[fuelFraction] = 1.0f / (s[mixtureRatio] + 1.0f);
  s[airFraction] = s[fuelFraction] * s[mixtureRatio];
  s[intakeDensity] = s[intakePressure] * 9.86923259915784e-6f * s[density];
  s[exhaustDensity] = s[exhaustPressure] * 9.86923259915784e-6f * s[density];
}

// FUN_004b6670 resets the model's constants, not its allocation or all state.
void reset(State& s) {
  s[crank] = 0.05317499861121178f;
  s[piston] = 0.04899999871850014f;
  s[rod] = 0.15f;
  s[vmin] = 0.0001f;
  s[cylinders] = 4;
  s[burnTime] = 0.003f;
  s[burnSpeed] = 0.00005f;
  s[2] = 1.0290000438690186f;
  s[6] = 1;
  s[density] = 1.2928999662399292f;
  s[mixtureHeat] = 1000000;
  s[exhaustHeat] = 0;
  s[heatCapacity] = 712;
  s[intakePressure] = 101325;
  s[exhaustPressure] = 111457.5f;
  s[intakeTemperature] = s[exhaustTemperature] = 293.15f;
  s[intakeMinimum] = s[exhaustMinimum] = 0.005f;
  s[intakeMaximum] = s[exhaustMaximum] = 0.8f;
  s[thermalTime] = 95;
  s[wallTemperature] = s[ambientTemperature] = 353.15f;
  s[mixtureRatio] = 14;
  s[spark] = s[sparkStart] = 0.48f;
  s[intakeOpen] = s[intakeStart] = 0.001f;
  s[intakeClose] = 0.25f;
  s[exhaustOpen] = s[exhaustStart] = 0.75f;
  s[exhaustClose] = 0.99f;
  s[sparkShift] = -0.05f;
  s[sparkRpm0] = 2000;
  s[sparkRpm1] = 5000;
  s[intakeDuration] = 0.249f;
  s[exhaustDuration] = 0.24000000953674316f;
  for (auto field : {intakeShift, intakeRpm0, intakeRpm1, exhaustShift,
                     exhaustRpm0, exhaustRpm1, elapsed, angle, turboPlateau})
    s[field] = 0;
  s[rpm] = 1000;
  s[turboMultiplier] = 20;
  s[turboOptimum] = 85000;
  s[turboRange] = 30000;
  s[turboPressure] = 0;
  s[wastePressure] = 3;
  mixture(s);
  resetCycle(s);
  for (auto field : {peakGrossPower, peakNetPower, peakPressure, peakGrossRpm,
                     peakNetRpm, peakPressureRpm, fuelUsed, airUsed})
    s[field] = 0;
  s[dt] = s[period] * 0.005f;
  s[airLimit] = s[fuelLimit] = 100;
}

// FUN_004b69a0, after the wrapper has copied script values.
void geometry(State& s) {
  s[effectiveHeat] = s[mixtureHeat];
  s[bore] = s[piston] + s[piston];
  s[effectivePressure] = s[intakePressure];
  s[speedLimit] = 45;
  s[effectiveTemperature] = s[intakeTemperature];
  s[effectiveDensity] = s[intakeDensity];
  s[stroke] = s[crank] + s[crank];
  s[area] = s[piston] * s[piston] * 3.1415927410125732f;
  s[sweptVolume] = s[area] * s[stroke];
  s[vmax] = s[sweptVolume] + s[vmin];
  s[compression] = s[vmax] / s[vmin];
  s[maximumRpm] = (45.0f / s[crank]) * 9.549296379089355f;
  s[pistonSpeed] = s[angularSpeed] * s[crank];
  s[displacement] = s[sweptVolume] * s[cylinders];
}

float advanceTiming(const State& s, Word start, Word shift, Word low, Word high) {
  const float span = s[high] - s[low];
  if (span <= 0)
    return s[start];
  const float fraction = (s[rpm] - s[low]) / span;
  if (fraction < 0)
    return s[start];
  if (fraction > 1)
    return s[shift] + s[start];
  return fraction * s[shift] + s[start];
}

// FUN_004b6bf0: preparation occurs once per sampled RPM, before cycle reset.
void prepare(State& s) {
  mixture(s);
  s[spark] = advanceTiming(s, sparkStart, sparkShift, sparkRpm0, sparkRpm1);
  s[intakeOpen] = advanceTiming(s, intakeStart, intakeShift, intakeRpm0, intakeRpm1);
  s[intakeClose] = s[intakeDuration] + s[intakeOpen];
  s[exhaustOpen] = advanceTiming(s, exhaustStart, exhaustShift, exhaustRpm0, exhaustRpm1);
  s[exhaustClose] = s[exhaustDuration] + s[exhaustOpen];
  s[effectivePressure] = s[intakePressure];
  s[effectiveHeat] = s[mixtureHeat];
  s[effectiveTemperature] = s[intakeTemperature];
  s[effectiveDensity] = s[intakeDensity];
  if (s[turboMultiplier] > 0 && s[turboRange] > 0) {
    float boost = std::fabs(s[rpm] * s[turboMultiplier] - s[turboOptimum]);
    if (s[turboPlateau] <= boost) {
      boost = 1.0f - (boost - s[turboPlateau]) / (s[turboRange] - s[turboPlateau]);
      if (boost < 0)
        boost = 0;
    } else {
      boost = 1;
    }
    boost *= s[turboPressure];
    if (s[wastePressure] <= boost)
      boost = s[wastePressure];
    boost = std::sqrt(boost + 1.0f);
    s[effectivePressure] = boost * boost * s[intakePressure];
    s[effectiveTemperature] = s[intakeTemperature] * boost;
    s[effectiveDensity] = s[intakeDensity] * boost;
  }
  if (s[fuelLimit] > 0 && s[fuelLimit] < s[fuelConsumption])
    s[effectiveHeat] = s[mixtureHeat] / (s[fuelConsumption] / s[fuelLimit]);
  if (s[airLimit] > 0 && s[airLimit] < s[airConsumption])
    s[effectivePressure] /= s[airConsumption] / s[airLimit];
  float shortening = s[rpm] * s[burnSpeed];
  if (shortening >= 0.9f)
    shortening = 0.9f;
  s[effectiveBurnTime] = (1.0f - shortening) * s[burnTime];
}

// FUN_004b7020 uses a strict opening and closing interval for the valves.
bool valveOpen(const State& s, Word open, Word close) {
  const bool after = s[open] * s[period] < s[elapsed];
  if (s[close] <= s[open])
    return after || s[elapsed] < s[close] * s[period];
  return after && s[elapsed] < s[close] * s[period];
}

// Newer FUN_004f50e0 adds the cylinder, mixture and thermal guards absent in
// older FUN_004b7020. Returns the native integer cycle transition separately.
bool step(State& s, const DynoObserver& observer, unsigned bank, unsigned ordinal, unsigned index) {
  if (s[vmin] < 0)
    s[vmin] = 0;
  if (s[vmax] < 0)
    s[vmax] = 0;
  const float oldVolume = s[volume] < 0 ? 0.0f : s[volume];
  const float oldPressure = s[pressure];
  const float oldTemperature = s[temperature], oldBurned = s[burned];
  s[elapsed] = s[dt] + s[elapsed];
  const bool completed = s[period] <= s[elapsed];
  if (completed) {
    s[elapsed] -= s[period];
    s[previousWork] = s[work];
    s[work] = 0;
    const float swept = s[vmax] - s[vmin];
    s[meanPressure] = swept <= 1e-8f ? 0.0f : s[previousWork] / swept;
    const float pressureWork = s[meanPressure] * swept;
    s[torque] = s[cylinders] * pressureWork * 0.125f;
    s[grossPower] = s[rpm] * pressureWork * 0.01666666753590107f * s[cylinders];
    s[frictionPower] = s[rpm] * s[rpm] * s[rpm] * 2e-8f;
    s[netPower] = s[grossPower] - s[frictionPower];
    s[horsepower] = s[netPower] * 0.001341f;
  }
  s[angle] = s[angle] + s[angularSpeed] * s[dt];
  if (s[angle] > 12.566370964050293f)
    s[angle] -= 12.566370964050293f;
  const float sine = std::sin(s[angle]);
  // 004f52e3/004f52ec multiply sin(angle)*radius once, then square it.
  const float transverse = sine * s[crank];
  const float position = std::sqrt(s[rod] * s[rod] - transverse * transverse) +
                         std::cos(s[angle]) * s[crank];
  s[pistonPosition] = position;
  s[volume] = ((s[crank] + s[rod]) - position) * s[area] + s[vmin];
  if (s[volume] < 1e-7f) {
    s[volume] = 1e-7f;
    return completed;
  }
  const float inverseVolume = 1.0f / s[volume];
  // Newer 004f5381/004f539d supply the Float32 ratio in XMM0, and
  // 0059ab6c/00593704 supply 1.3/0.3 in XMM1 to the Float32 pow helper.
  s[pressure] = referencePower(inverseVolume * oldVolume, 1.3f) * oldPressure;
  const float temperatureRatio = inverseVolume * oldVolume;
  const float temperaturePower = referencePower(temperatureRatio, 0.3f);
  observe(observer, DynoPhase::TemperaturePower, s, bank, ordinal, index, false,
          DynoScalar{temperatureRatio, 0.3f, temperaturePower});
  s[temperature] = temperaturePower * oldTemperature;
  s[intakeValve] = s[valveOpen(s, intakeOpen, intakeClose) ? intakeMaximum : intakeMinimum];
  s[exhaustValve] = s[valveOpen(s, exhaustOpen, exhaustClose) ? exhaustMaximum : exhaustMinimum];
  const float ignition = s[spark] * s[period];
  float end = ignition + s[effectiveBurnTime];
  s[ignitionTime] = ignition;
  if (end > s[period])
    end -= s[period];
  const bool burning = end <= ignition ? ignition < s[elapsed] || s[elapsed] <= end
                                       : ignition < s[elapsed] && s[elapsed] <= end;
  s[burned] = burning ? (s[elapsed] - ignition) / s[effectiveBurnTime]
                      : (oldBurned <= 0 ? 0.0f : 1.0f);
  if (s[burned] > 0) {
    // The numerator deliberately uses mass before the denominator's clamp.
    const float oldMass = s[mass];
    if (s[mass] < 1e-7f)
      s[mass] = 1e-7f;
    if (s[temperature] < 1e-7f)
      s[temperature] = 1e-7f;
    const float burnEnergy = s[fuelHeat] * oldMass;
    const float nextTemperature = ((s[burned] - oldBurned) * burnEnergy) /
                                      (s[heatCapacity] * s[mass]) +
                                  s[temperature];
    s[pressure] = (nextTemperature / s[temperature]) * s[pressure];
    s[temperature] = nextTemperature;
    if (s[burned] >= 1) {
      s[burned] = 0;
      s[fuelHeat] = 0;
    }
  }
  if (s[intakeValve] > 0) {
    float fraction = s[intakeValve] * s[dt] * 1000.0f;
    if (fraction >= 1)
      fraction = 1;
    float nextPressure = fraction * (s[effectivePressure] - s[pressure]) + s[pressure];
    if (nextPressure <= 1e-6f)
      nextPressure = 1e-6f;
    const float displaced = (s[volume] * s[pressure]) / nextPressure;
    if (s[volume] <= displaced) {
      s[mass] -= (displaced - s[volume]) * inverseVolume * s[mass];
    } else {
      const float added = (nextPressure / s[effectivePressure]) * s[effectiveDensity] * (s[volume] - displaced);
      if (s[mass] < 1e-7f)
        s[mass] = 1e-7f;
      const float nextMass = s[mass] + added;
      const float inverseMass = 1.0f / nextMass;
      s[temperature] = (s[effectiveTemperature] * added + s[temperature] * s[mass]) * inverseMass;
      s[fuelHeat] = (s[effectiveHeat] * added + s[fuelHeat] * s[mass]) * inverseMass;
      s[mass] = nextMass;
      s[airUsed] = added * s[airFraction] + s[airUsed];
      s[fuelUsed] = s[fuelFraction] * added + s[fuelUsed];
    }
    s[pressure] = nextPressure;
    if (s[mass] < 0)
      s[mass] = 0;
  }
  if (s[exhaustValve] > 0) {
    float fraction = s[exhaustValve] * s[dt] * 1000.0f;
    if (fraction >= 1)
      fraction = 1;
    const float nextPressure = fraction * (s[exhaustPressure] - s[pressure]) + s[pressure];
    const float displaced = (s[volume] * s[pressure]) / nextPressure;
    if (s[volume] <= displaced) {
      s[mass] -= (displaced - s[volume]) * inverseVolume * s[mass];
    } else {
      const float added = (nextPressure / s[exhaustPressure]) * s[exhaustDensity] * (s[volume] - displaced);
      if (s[mass] < 1e-7f)
        s[mass] = 1e-7f;
      const float nextMass = s[mass] + added;
      const float inverseMass = 1.0f / nextMass;
      s[temperature] = (s[temperature] * s[mass] + added * s[exhaustTemperature]) * inverseMass;
      s[fuelHeat] = (s[exhaustHeat] * added + s[fuelHeat] * s[mass]) * inverseMass;
      s[mass] = nextMass;
    }
    s[pressure] = nextPressure;
  }
  if (s[mass] < 0)
    s[mass] = 0;
  float fraction = s[dt] / s[thermalTime];
  if (fraction >= 1)
    fraction = 1;
  const float difference = s[temperature] - s[wallTemperature];
  const float sign = difference == 0 ? 0.0f : (difference < 0 ? -1.0f : 1.0f);
  float cooling = difference * difference * sign * fraction;
  if (cooling / difference > 1)
    cooling /= cooling / difference;
  s[heatLoss] += cooling;
  const float cooled = s[temperature] - cooling;
  if (s[temperature] < 1e-7f)
    s[temperature] = 1e-7f;
  s[pressure] = (cooled / s[temperature]) * s[pressure];
  s[temperature] = cooled;
  s[work] = (s[pressure] + oldPressure) * 0.5f * (s[volume] - oldVolume) + s[work];
  return completed;
}

// Newer FUN_004f4560. With dt=period/231 a cycle needs at most 232 steps. The
// explicit bound reports invalid numeric state instead of hanging the game.
bool cycle(State& s, const DynoObserver& observer, unsigned bank, unsigned ordinal) {
  s[fuelUsed] = s[airUsed] = s[heatLoss] = s[otherHeat] = 0;
  bool completed = false;
  for (unsigned i = 0; i < 232 && !completed; ++i) {
    completed = step(s, observer, bank, ordinal, i + 1);
    observe(observer, DynoPhase::Step, s, bank, ordinal, i + 1, completed);
  }
  if (!completed)
    return false;
  const float inversePeriod = 1.0f / s[period];
  s[fuelConsumption] = s[fuelUsed] * inversePeriod * s[cylinders];
  s[airConsumption] = s[airUsed] * inversePeriod * s[cylinders];
  s[efficiency] = (s[airUsed] + s[fuelUsed]) / (s[sweptVolume] * s[density]);
  s[specificConsumption] = (s[fuelConsumption] * 7920.0f) / s[horsepower];
  s[heatLoss] = inversePeriod * s[heatLoss];
  observe(observer, DynoPhase::CycleResult, s, bank, ordinal, 0, true);
  return std::isfinite(s[torque]) && std::isfinite(s[netPower]);
}

// Newer FUN_004f4640. The integer step truncates maximumRpm/tableSize; the table
// count is integer division, so neither is the floating script step size.
bool table(State& s, int stepRpm, DynoCurve& result, const DynoObserver& observer, unsigned bank) {
  for (size_t field = peakPressure; field <= peakNetRpm; ++field)
    s[field] = 0;
  const int count = static_cast<int>(s[maximumRpm]) / stepRpm;
  if (count < 1 || count > 65536)
    return false;
  result.torque.assign(static_cast<size_t>(count), 0);
  result.power.assign(static_cast<size_t>(count), 0);
  for (int i = 1; i < count; ++i) {
    s[angle] = s[elapsed] = 0;
    s[rpm] = static_cast<float>(i * stepRpm);
    s[period] = 120.0f / s[rpm];
    s[angularSpeed] = s[rpm] * 0.10471975803375244f;
    s[pistonSpeed] = s[angularSpeed] * s[crank];
    prepare(s);
    observe(observer, DynoPhase::Prepare, s, bank);
    s[dt] = s[period] * 0.0043290043249726295f;
    resetCycle(s);
    observe(observer, DynoPhase::CycleInit, s, bank);
    if (!cycle(s, observer, bank, 0))
      return false;
    float previous = s[meanPressure];
    for (unsigned repeat = 0; repeat < 10; ++repeat) {
      if (!cycle(s, observer, bank, repeat + 1))
        return false;
      if (std::fabs(previous / s[meanPressure] - 1.0f) < 0.05f)
        break;
      previous = previous * 0.4f + s[meanPressure] * 0.6f;
    }
    if (s[peakGrossPower] < s[grossPower]) {
      s[peakGrossPower] = s[grossPower];
      s[peakGrossRpm] = s[rpm];
    }
    if (s[peakNetPower] < s[netPower]) {
      s[peakNetPower] = s[netPower];
      s[peakHorsepower] = s[netPower] * 0.001341f;
      s[peakNetRpm] = s[peakHorsepowerRpm] = s[rpm];
    }
    if (s[peakPressure] < s[meanPressure]) {
      s[peakPressure] = s[meanPressure];
      s[peakPressureRpm] = s[peakTorqueRpm] = s[rpm];
      s[peakTorque] = s[torque];
    }
    result.torque[static_cast<size_t>(i)] = s[torque];
    result.power[static_cast<size_t>(i)] = s[netPower];
  }
  s[torque] = s[peakTorque];
  s[grossPower] = s[peakGrossPower];
  s[netPower] = s[peakNetPower];
  return true;
}
} // namespace

std::variant<DynoResult, std::string> DynoModel::calculate(const DynoParameters& p, float tableSize, const DynoObserver& observer) {
  if (!std::isfinite(tableSize) || tableSize <= 0)
    return "DynoData.calcDyno: invalid table size";
  // Preserve the old model on an unsupported numeric input or failed cycle.
  State s = state_;
  reset(s);
  observe(observer, DynoPhase::Reset, s);
  s[cylinders] = p.cylinders;
  s[piston] = p.bore * 0.5f;
  s[crank] = p.stroke * 0.5f;
  s[vmin] = p.minimumVolume;
  s[intakeMinimum] = p.intakeMinimum;
  s[intakeMaximum] = p.intakeMaximum;
  s[exhaustMinimum] = p.exhaustMinimum;
  s[exhaustMaximum] = p.exhaustMaximum;
  s[intakeOpen] = s[intakeStart] = p.intakeOpen;
  s[intakeClose] = p.intakeClose;
  s[exhaustOpen] = s[exhaustStart] = p.exhaustOpen;
  s[exhaustClose] = p.exhaustClose;
  s[intakeDuration] = s[intakeClose] - s[intakeOpen];
  s[exhaustDuration] = s[exhaustClose] - s[exhaustOpen];
  s[burnTime] = p.burnTime;
  s[rpmLimit] = p.rpmLimit;
  s[sparkStart] = p.sparkMinimum;
  s[sparkShift] = p.sparkIncrement;
  s[sparkRpm0] = p.sparkRpm0;
  s[sparkRpm1] = p.sparkRpm1;
  s[turboMultiplier] = p.turboMultiplier;
  s[turboOptimum] = p.turboOptimum;
  s[turboRange] = p.turboRange;
  s[turboPlateau] = 0;
  s[turboPressure] = p.turboPressure;
  s[wastePressure] = p.wastePressure;
  s[mixtureHeat] = p.mixtureHeat;
  s[mixtureRatio] = p.mixtureRatio;
  s[airLimit] = p.maximumAirConsumption;
  s[fuelLimit] = p.maximumFuelConsumption;
  s[intakeTemperature] = p.intercooling * 293.15f;
  if (!(s[crank] >= 0 && s[crank] <= s[rod] && s[piston] >= 0))
    return "DynoData.calcDyno: unsupported cylinder geometry";
  observe(observer, DynoPhase::Inputs, s);
  mixture(s);
  geometry(s);
  if (p.maximumRpm > 0)
    s[maximumRpm] = p.maximumRpm;
  observe(observer, DynoPhase::Geometry, s);
  const float requestedStep = std::max(s[maximumRpm] / tableSize, 1.0f);
  if (!std::isfinite(s[maximumRpm]) || s[maximumRpm] >= static_cast<float>(std::numeric_limits<int>::max()) ||
      !std::isfinite(requestedStep) || requestedStep >= static_cast<float>(std::numeric_limits<int>::max()))
    return "DynoData.calcDyno: unsupported RPM table range";
  DynoResult result;
  // Newer FUN_004f49e0 does not clear table pointers; FUN_004f4640 only
  // replaces the selected bank. Disabling nitrous leaves the old bank alive.
  if (result_)
    result.nitro = result_->nitro;
  result.stepRpm = static_cast<int>(requestedStep);
  result.requestedStepRpm = requestedStep;
  result.maximumRpm = s[maximumRpm];
  if (!table(s, result.stepRpm, result.primary, observer, 0))
    return "DynoData.calcDyno: primary combustion did not produce a finite bounded table";
  result.displacement = s[displacement];
  result.compression = s[compression];
  result.maximumTorque = s[peakTorque];
  result.maximumHorsepower = s[peakHorsepower];
  result.maximumTorqueRpm = s[peakTorqueRpm];
  result.maximumHorsepowerRpm = s[peakHorsepowerRpm];
  s[intakeTemperature] = 293.15f;
  // FUN_0046aa20 publishes primary statistics before generating bank one.
  if (p.nitroHeat - 1.0f > 0.0001f || p.nitroCooling - 1.0f > 0.0001f) {
    const float savedMaximum = s[maximumRpm];
    s[mixtureHeat] *= p.nitroHeat;
    s[intakeTemperature] *= p.nitroCooling;
    mixture(s);
    geometry(s);
    s[maximumRpm] = savedMaximum;
    // Unlike the primary branch, newer 004cc088 does not clamp this quotient
    // to one. Reject its zero-step divide rather than invent a nitrous curve.
    const int nitroStep = static_cast<int>(s[maximumRpm] / tableSize);
    if (nitroStep < 1)
      return "DynoData.calcDyno: nitrous table step would divide by zero";
    if (!table(s, nitroStep, result.nitro, observer, 1))
      return "DynoData.calcDyno: nitrous combustion did not produce a finite bounded table";
  }
  state_ = s;
  result_ = result;
  return result;
}
