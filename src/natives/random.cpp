#include "natives/random.hpp"
#include "run_log.hpp"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

NativeResult mathRandomize(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                           const std::vector<const TufaClass*>*) {
  if (!args.empty() || !arena || !arena->randomState)
    return std::string{"invalid Math.randomize call"};
  const char* forced = std::getenv("SLRR_FORCE_SEED");
  if (forced) {
    char* end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(forced, &end, 10);
    if (!*forced || *forced == '-' || *forced == '+' || *end || errno == ERANGE || value > UINT32_MAX)
      return std::string{"Math.randomize: invalid SLRR_FORCE_SEED (expected unsigned 32-bit decimal)"};
    arena->randomState->state = static_cast<uint32_t>(value);
    runlog::event(runlog::EventKind::Note, "random seed: forced seed=%u", arena->randomState->state);
    return std::monostate{};
  }
  if (!arena->performanceMilliseconds)
    return std::string{"Math.randomize: performance clock unavailable"};
  const double ms = arena->performanceMilliseconds();
  if (!std::isfinite(ms)) return std::string{"Math.randomize: invalid performance clock"};
  const double truncated = std::trunc(ms);
  if (truncated < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
      truncated > static_cast<double>(std::numeric_limits<int32_t>::max()))
    arena->randomState->state = 0x80000000u;
  else
    arena->randomState->state = static_cast<uint32_t>(static_cast<int32_t>(truncated));
  runlog::event(runlog::EventKind::Note, "random seed: elapsed_ms=%.17g seed=%u", ms, arena->randomState->state);
  const char* trace = std::getenv("SLRR_TRACE_RANDOM_SEED");
  if (trace && std::strcmp(trace, "1") == 0)
    std::fprintf(stderr, "random seed: elapsed_ms=%.17g seed=%u\n", ms, arena->randomState->state);
  return std::monostate{};
}

NativeResult mathRandom(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                        const std::vector<const TufaClass*>*) {
  if (!args.empty() || !arena || !arena->randomState) return std::string{"invalid Math.random call"};
  auto& state = arena->randomState->state;
  state = state * uint32_t{0x343fd} + uint32_t{0x269ec3};
  uint32_t value = (state >> 16) & 0x7fff;
  if (value == 0x7fff) value = 0x7ffe;
  // Stock multiplies by the float32 constant at 0x005f1388 (docs/math-natives.md),
  // bits 0x38000100 ~= 1/32767; a 1/32768 scale shifts every draw enough to flip
  // the used-car random-skip checks in Chassis.addStockParts.
  static const float kStockRandomScale = [] {
    float f;
    const uint32_t bits = 0x38000100u;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
  }();
  const float draw = static_cast<float>(static_cast<double>(value) * static_cast<double>(kStockRandomScale));
  const char* trace = std::getenv("SLRR_TRACE_RANDOM_DRAWS");
  if (trace && std::strcmp(trace, "1") == 0)
    std::fprintf(stderr, "math.random: state=0x%08x value=%u draw=%.9g\n", state, value, static_cast<double>(draw));
  return VmValue{draw};
}
