#include "natives/random.hpp"

#include <cstdint>
#include <cstring>
#include <cstdlib>

static uint32_t bits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, sizeof result);
  return result;
}

int main() {
  VmRandomState state;
  VmArena arena;
  arena.randomState = &state;
  if (!std::holds_alternative<std::string>(mathRandomize({}, nullptr, &arena, nullptr))) return 1;
  setenv("SLRR_FORCE_SEED", "4294967295", 1);
  if (!std::holds_alternative<std::monostate>(mathRandomize({}, nullptr, &arena, nullptr)) ||
      arena.randomState->state != 0xffffffffu) return 1;
  setenv("SLRR_FORCE_SEED", "4294967296", 1);
  if (!std::holds_alternative<std::string>(mathRandomize({}, nullptr, &arena, nullptr))) return 1;
  unsetenv("SLRR_FORCE_SEED");
  arena.performanceMilliseconds = [] { return 1234.9; };
  if (!std::holds_alternative<std::monostate>(mathRandomize({}, nullptr, &arena, nullptr)) ||
      state.state != 1234) return 1;
  arena.performanceMilliseconds = [] { return 2147483648.0; };
  if (!std::holds_alternative<std::monostate>(mathRandomize({}, nullptr, &arena, nullptr)) ||
      state.state != 0x80000000u) return 1;
  arena.performanceMilliseconds = [] { return -1.5; };
  if (!std::holds_alternative<std::monostate>(mathRandomize({}, nullptr, &arena, nullptr)) ||
      state.state != 0xffffffffu) return 1;
  setenv("SLRR_TRACE_RANDOM_SEED", "1", 1);
  arena.performanceMilliseconds = [] { return 1.5; };
  if (!std::holds_alternative<std::monostate>(mathRandomize({}, nullptr, &arena, nullptr)) ||
      state.state != 1) return 1;
  unsetenv("SLRR_TRACE_RANDOM_SEED");
  const uint32_t states[] = {0x0029e2c0, 0xc823f683, 0x18be873a, 0xe7847115, 0xcae1df84};
  const uint32_t values[] = {0x3aa40148, 0x3f104721, 0x3e45f18c, 0x3f4f099e, 0x3f15c32c};
  for (size_t i = 0; i < 5; ++i) {
    const auto result = mathRandom({}, nullptr, &arena, nullptr);
    const auto* value = std::get_if<VmValue>(&result);
    if (!value || !std::holds_alternative<float>(*value) || bits(std::get<float>(*value)) != values[i] ||
        state.state != states[i]) return 1;
  }
  state.state = 0xf01bf641;
  const auto clamp = mathRandom({}, nullptr, &arena, nullptr);
  const auto* value = std::get_if<VmValue>(&clamp);
  if (!value || !std::holds_alternative<float>(*value) || bits(std::get<float>(*value)) != 0x3f7ffe00 ||
      state.state != 0x7fff0000) return 1;
  // Stock multiplies the 15-bit draw by the float32 constant at 0x005f1388
  // (bits 0x38000100, ~1/32767); a 1/32768 scale flips used-car install-skip
  // draws in Chassis.addStockParts. A draw of 1 must give exactly 0x38000100.
  state.state = 0xd2c5f641;
  const auto unit = mathRandom({}, nullptr, &arena, nullptr);
  const auto* unitValue = std::get_if<VmValue>(&unit);
  if (!unitValue || !std::holds_alternative<float>(*unitValue) ||
      bits(std::get<float>(*unitValue)) != 0x38000100) return 1;
  if (!std::holds_alternative<std::string>(mathRandom({int32_t{0}}, nullptr, &arena, nullptr)) ||
      state.state != 0x00010000 || !std::holds_alternative<std::string>(mathRandom({}, nullptr, nullptr, nullptr))) return 1;
  return 0;
}
