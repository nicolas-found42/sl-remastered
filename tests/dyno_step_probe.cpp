#include "dyno_model.hpp"

#include <bit>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <variant>

namespace {
const char* name(DynoPhase phase) {
  switch (phase) {
  case DynoPhase::Reset:
    return "reset";
  case DynoPhase::Inputs:
    return "inputs";
  case DynoPhase::Geometry:
    return "geometry";
  case DynoPhase::Prepare:
    return "prepare";
  case DynoPhase::CycleInit:
    return "cycle_init";
  case DynoPhase::TemperaturePower:
    return "temperature_power";
  case DynoPhase::Step:
    return "step";
  case DynoPhase::CycleResult:
    return "cycle_result";
  }
  return "invalid";
}
} // namespace

int main() {
  DynoModel model, baseline;
  DynoParameters parameters;
  unsigned count = 0;
  bool overflow = false;
  const auto traced = model.calculate(parameters, 16, [&](const DynoObservation& event) {
    const bool initial = event.phase == DynoPhase::Reset || event.phase == DynoPhase::Inputs || event.phase == DynoPhase::Geometry;
    if (!initial && (event.bank != 0 || event.state[34] != 437 || event.cycle != 0))
      return;
    if (event.phase == DynoPhase::TemperaturePower && event.step != 164)
      return;
    if (++count > 241) {
      overflow = true;
      return;
    }
    std::printf("{\"type\":\"dyno_trace\",\"phase\":\"%s\",\"bank\":%u,\"cycle\":%u,\"step\":%u,\"cycle_count\":%u,\"words\":[",
                name(event.phase), event.bank, event.cycle, event.step, event.cycleCompleted ? 1u : 0u);
    for (size_t i = 0; i < event.state.size(); ++i)
      std::printf("%s%u", i ? "," : "", std::bit_cast<uint32_t>(event.state[i]));
    if (event.scalar)
      std::printf("],\"scalar\":[%u,%u,%u],\"double_intermediate\":%u}\n", std::bit_cast<uint32_t>(event.scalar->input), std::bit_cast<uint32_t>(event.scalar->exponent), std::bit_cast<uint32_t>(event.scalar->result), std::bit_cast<uint32_t>(static_cast<float>(std::pow(static_cast<double>(event.scalar->input), static_cast<double>(event.scalar->exponent)))));
    else
      std::puts("]}");
  });
  const auto plain = baseline.calculate(parameters, 16);
  const auto* result = std::get_if<DynoResult>(&traced);
  const auto* expected = std::get_if<DynoResult>(&plain);
  const auto sameWords = [](const auto& a, const auto& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
      if (std::bit_cast<uint32_t>(a[i]) != std::bit_cast<uint32_t>(b[i])) return false;
    return true;
  };
  if (!result || !expected || overflow || count < 200 || !sameWords(result->primary.torque, expected->primary.torque) || !sameWords(result->primary.power, expected->primary.power) || !sameWords(result->nitro.torque, expected->nitro.torque) || !sameWords(result->nitro.power, expected->nitro.power) || !sameWords(std::array{result->displacement, result->compression, result->maximumTorque, result->maximumTorqueRpm, result->maximumHorsepower, result->maximumHorsepowerRpm}, std::array{expected->displacement, expected->compression, expected->maximumTorque, expected->maximumTorqueRpm, expected->maximumHorsepower, expected->maximumHorsepowerRpm}))
    return 1;
  std::puts("{\"type\":\"scope\",\"observer_output_unchanged\":true,\"word93_is_reserved\":true}");
}
