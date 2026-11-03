#include "dyno_model.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>

namespace {
void words(const std::vector<float>& values) {
  std::putchar('[');
  for (size_t i = 0; i < values.size(); ++i)
    std::printf("%s%u", i ? "," : "", std::bit_cast<uint32_t>(values[i]));
  std::putchar(']');
}
bool emit(DynoModel& model, const DynoParameters& parameters, unsigned id) {
  const auto calculated = model.calculate(parameters, 16.0f);
  const auto* result = std::get_if<DynoResult>(&calculated);
  if (!result) {
    std::fprintf(stderr, "case %u: %s\n", id, std::get<std::string>(calculated).c_str());
    return false;
  }
  std::printf("{\"case\":%u,\"step\":%d,\"statistics\":", id, result->stepRpm);
  words({result->displacement, result->compression, result->maximumTorque,
         result->maximumTorqueRpm, result->maximumHorsepower, result->maximumHorsepowerRpm});
  std::printf(",\"torque\":");
  words(result->primary.torque);
  std::printf(",\"power\":");
  words(result->primary.power);
  std::printf(",\"nitro_torque\":");
  words(result->nitro.torque);
  std::printf(",\"nitro_power\":");
  words(result->nitro.power);
  std::puts("}");
  return true;
}
} // namespace

// Differential producer: numbers are IEEE-754 words, not expected values
// computed from this implementation. The accepted fixture must come from the
// source-bound original executable harness.
int main() {
  DynoModel initial;
  DynoParameters parameters;
  if (!emit(initial, parameters, 0) || !emit(initial, parameters, 1))
    return 1;
  for (unsigned id = 2; id < 7; ++id) {
    parameters = {};
    if (id == 2) {
      parameters.turboPressure = 0.6f;
      parameters.wastePressure = 0.4f;
    } else if (id == 3) {
      parameters.intercooling = 0.85f;
    } else if (id == 4) {
      parameters.nitroHeat = 1.2f;
      parameters.nitroCooling = 1.1f;
    } else if (id == 5) {
      parameters.intakeOpen = 0.95f;
      parameters.intakeClose = 0.2f;
      parameters.exhaustOpen = 0.7f;
      parameters.exhaustClose = 0.1f;
    } else if (id == 6) {
      parameters.maximumFuelConsumption = 0.001f;
      parameters.maximumAirConsumption = 0.02f;
    }
    DynoModel fresh;
    if (!emit(fresh, parameters, id))
      return 1;
    if (id == 4 && !emit(fresh, DynoParameters{}, 7))
      return 1;
  }
  parameters = {};
  parameters.cylinders = 0;
  parameters.bore = parameters.stroke = .01f;
  parameters.minimumVolume = 100000;
  parameters.intakeMinimum = parameters.intakeMaximum = 1;
  parameters.exhaustMinimum = parameters.exhaustMaximum = 1;
  parameters.intakeOpen = .001f;
  parameters.intakeClose = .002f;
  parameters.exhaustOpen = .003f;
  parameters.exhaustClose = .004f;
  parameters.sparkMinimum = .5f;
  parameters.sparkIncrement = 0;
  parameters.sparkRpm0 = 0;
  parameters.sparkRpm1 = 10000;
  parameters.burnTime = 100000;
  parameters.rpmLimit = 10000;
  parameters.maximumRpm = 0;
  parameters.turboMultiplier = parameters.turboOptimum = parameters.turboRange = 0;
  parameters.turboPressure = parameters.wastePressure = 0;
  DynoModel cleared;
  if (!emit(cleared, parameters, 8))
    return 1;
}
