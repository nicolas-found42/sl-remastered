#include "natives/lang_system.hpp"
#include "camera_frame.hpp"
#include "natives/game_type.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"
#include "timers.hpp"
#include <bit>
#include <cmath>
#include <limits>

NativeResult systemSetLdPriority(const std::vector<VmValue> &args,
                                 const VmObject *, VmArena *arena,
                                 const std::vector<const TufaClass *> *) {
  if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid System.setLdPriority call"};
  if (!arena || !arena->gameState || !arena->gameState->visibility)
    return std::string{"System.setLdPriority: visibility state unavailable"};
  arena->gameState->visibility->setPriority(std::get<int32_t>(args[0]));
  return std::monostate{};
}

namespace {
bool isArray(const VmObject *object) {
  return object && !object->runtimeClass && !object->arrayDescriptor.empty();
}
} // namespace

NativeResult systemArraycopy(const std::vector<VmValue> &args, const VmObject *,
                             VmArena *,
                             const std::vector<const TufaClass *> *) {
  if (args.size() != 5 || !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]) ||
      !std::holds_alternative<const VmObject *>(args[2]) ||
      !std::holds_alternative<int32_t>(args[3]) ||
      !std::holds_alternative<int32_t>(args[4]))
    return std::string{"invalid System.arraycopy call"};
  const VmObject *source = std::get<const VmObject *>(args[0]);
  const VmObject *destination = std::get<const VmObject *>(args[2]);
  const int64_t sourcePos = std::get<int32_t>(args[1]),
                destinationPos = std::get<int32_t>(args[3]);
  const int64_t count = std::get<int32_t>(args[4]);
  if (!isArray(source) || !isArray(destination) ||
      source->arrayDescriptor != destination->arrayDescriptor)
    return std::string{"unsupported System.arraycopy arrays"};
  // Stock checks only the upper bounds; negative values and bad indices
  // dereference address 8.
  if (sourcePos < 0 || destinationPos < 0 || count < 0)
    return std::string{"System.arraycopy negative argument"};
  if (static_cast<int64_t>(source->arrayElements.size()) < sourcePos + count ||
      static_cast<int64_t>(destination->arrayElements.size()) <
          destinationPos + count)
    return std::string{"System.arraycopy: copy out of bounds"};
  if (source->arrayElements.empty() || destination->arrayElements.empty())
    return std::string{"System.arraycopy: empty array"};
  const std::vector<VmValue> moved(source->arrayElements.begin() + sourcePos,
                                   source->arrayElements.begin() + sourcePos +
                                       count);
  for (int64_t i = 0; i < count; ++i)
    destination->arrayElements[static_cast<size_t>(destinationPos + i)] =
        moved[static_cast<size_t>(i)];
  return std::monostate{};
}

NativeResult systemExitNoArgs(const std::vector<VmValue> &args,
                              const VmObject *, VmArena *arena,
                              const std::vector<const TufaClass *> *) {
  if (!args.empty() || !arena)
    return std::string{"invalid System.exit() context or arguments"};
  // FUN_0047be50 writes DAT_0063c520=1; it does not unwind the script call.
  arena->exitRequested = true;
  return std::monostate{};
}

NativeResult systemExitString(const std::vector<VmValue> &args,
                              const VmObject *, VmArena *,
                              const std::vector<const TufaClass *> *) {
  if (args.size() != 1 || !std::holds_alternative<const VmObject *>(args[0]))
    return std::string{"invalid System.exit call"};
  return std::monostate{};
}

NativeResult systemLog(const std::vector<VmValue> &args, const VmObject *,
                       VmArena *, const std::vector<const TufaClass *> *) {
  if (args.size() != 1 || !std::holds_alternative<const VmObject *>(args[0]))
    return std::string{"invalid System.log call"};
  return std::monostate{};
}

NativeResult systemOpenLib(const std::vector<VmValue> &args, const VmObject *,
                           VmArena *arena,
                           const std::vector<const TufaClass *> *) {
  if (args.size() != 1 || !std::holds_alternative<const VmObject *>(args[0]))
    return std::string{"invalid System.openLib call"};
  if (!arena || !arena->gameState || !arena->gameState->packs ||
      !arena->gameState->resources)
    return std::string{"System.openLib: resource world unavailable"};
  const VmObject *object = std::get<const VmObject *>(args[0]);
  const std::string *name = nullptr;
  for (const auto &entry : arena->strings)
    if (entry.second == object) {
      if (name)
        return std::string{"System.openLib: ambiguous string"};
      name = &entry.first;
    }
  if (!object || !name)
    return std::string{"System.openLib: invalid string"};
  uint16_t slot;
  std::string error;
  if (!arena->gameState->packs->registerPackName(*name, slot, error))
    return std::string{"System.openLib: "} + error;
  auto &resources = *arena->gameState->resources;
  if (resources.mountState(slot) == MountState::Mounted)
    return static_cast<int32_t>(slot);
  const auto result = resources.lookup((static_cast<uint32_t>(slot) << 16) | 1);
  if (result.status == LookupStatus::Found)
    return static_cast<int32_t>(slot);
  if (result.status == LookupStatus::NotFound)
    return int32_t{-1};
  return std::string{"System.openLib: "} + result.detail;
}

NativeResult systemCurrentTime(const std::vector<VmValue> &args,
                               const VmObject *, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!args.empty() || !arena)
    return std::string{"System.currentTime: performance clock unavailable"};
  if (arena->storedCurrentTimeBits) {
    const auto stored = arena->storedCurrentTimeBits();
    if (const auto* error = std::get_if<std::string>(&stored))
      return "System.currentTime: stored clock provider: " + *error;
    const auto* bits = std::get_if<uint32_t>(&stored);
    if (!bits)
      return std::string{"System.currentTime: stored clock exhausted"};
    if ((*bits & 0x7f800000u) == 0x7f800000u)
      return std::string{"System.currentTime: nonfinite stored clock"};
    // A stored Native result is not a performance-clock operand. Avoid both
    // arithmetic that changes its bits and backprojection into other clocks.
    return VmValue{std::bit_cast<float>(*bits)};
  }
  if (!arena->performanceMilliseconds)
    return std::string{"System.currentTime: performance clock unavailable"};
  const double milliseconds = arena->performanceMilliseconds();
  if (!std::isfinite(milliseconds))
    return std::string{"System.currentTime: invalid clock"};
  return VmValue{
      static_cast<float>(milliseconds * static_cast<double>(0.001f))};
}

NativeResult systemTimeWarp(const std::vector<VmValue> &args, const VmObject *,
                            VmArena *arena,
                            const std::vector<const TufaClass *> *) {
  if (args.size() != 1 || !std::holds_alternative<float>(args[0]) || !arena ||
      !arena->timers)
    return std::string{"System.timeWarp: clock state unavailable"};
  const float previous = arena->timers->timeScale();
  std::string error;
  if (std::get<float>(args[0]) < 0)
    return VmValue{previous};
  if (!arena->performanceMilliseconds)
    return std::string{"System.timeWarp: performance clock unavailable"};
  if (!arena->timers->setTimeWarp(std::get<float>(args[0]),
                                  arena->performanceMilliseconds(), error))
    return error;
  return VmValue{previous};
}

NativeResult systemSyncGameTime(const std::vector<VmValue> &args,
                                const VmObject *, VmArena *arena,
                                const std::vector<const TufaClass *> *) {
  if (args.size() != 1 || !std::holds_alternative<float>(args[0]) || !arena ||
      !arena->gameState)
    return std::string{"System.syncGameTime: clock state unavailable"};
  const double value = std::get<float>(args[0]);
  if (!std::isfinite(value) || value < std::numeric_limits<int32_t>::min() ||
      value > std::numeric_limits<int32_t>::max())
    return std::string{"System.syncGameTime: unsupported clock value"};
  const int32_t seconds = static_cast<int32_t>(value) % 86400;
  arena->gameState->gameTimeOfDay = seconds;
  arena->gameState->gameNight = !(seconds >= 3600 && seconds < 72000 &&
                                  (seconds < 18000 || seconds >= 25200));
  return std::monostate{};
}

NativeResult systemIsLoadingReset(const std::vector<VmValue> &args,
                                  const VmObject *, VmArena *arena,
                                  const std::vector<const TufaClass *> *) {
  if (!args.empty() || !arena)
    return std::string{"System.isLoadingReset: invalid call"};
  if (!arena->gameState || !arena->gameState->resources)
    return std::string{"System.isLoadingReset: resource world unavailable"};
  const auto rate =
      vmActiveStaticField(*arena, "java.util.Config", "resource_loadrate");
  if (!rate || !std::holds_alternative<int32_t>(*rate) ||
      std::get<int32_t>(*rate) < 0)
    return std::string{"System.isLoadingReset: resource load rate unavailable"};
  arena->gameState->resources->resetLoadingHistory(
      static_cast<uint32_t>(std::get<int32_t>(*rate)));
  runlog::event(runlog::EventKind::Note,
                "loading System.isLoadingReset (rate=%d)",
                std::get<int32_t>(*rate));
  return std::monostate{};
}

NativeResult systemIsLoading(const std::vector<VmValue> &args, const VmObject *,
                             VmArena *arena,
                             const std::vector<const TufaClass *> *) {
  if (!args.empty() || !arena)
    return std::string{"System.isLoading: invalid call"};
  if (!arena->gameState || !arena->gameState->resources)
    return std::string{"System.isLoading: resource world unavailable"};
  // Edge-logged: the loading screen's flasher polls this every frame; what
  // matters is when it first reports a load and when it stops.
  static int last = -1;
  static uint64_t calls = 0, trues = 0;
  const int now = arena->gameState->resources->isLoading() ? 1 : 0;
  ++calls;
  trues += static_cast<uint64_t>(now);
  if (now != last) {
    runlog::event(
        runlog::EventKind::Note,
        "loading System.isLoading -> %d (call #%llu, true so far %llu)", now,
        static_cast<unsigned long long>(calls),
        static_cast<unsigned long long>(trues));
    last = now;
  }
  return VmValue{int32_t{now}};
}

NativeResult systemGetConfigOptions(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                                    const std::vector<const TufaClass*>*) {
  // FUN_0047c500 -> FUN_00427bf0 refreshes engine settings from live Config.
  if (!args.empty() || !arena || !arena->gameState || !arena->gameState->refreshConfig)
    return std::string{"System.getConfigOptions: application configuration unavailable"};
  std::string error;
  if (!arena->gameState->refreshConfig(error)) return "System.getConfigOptions: " + error;
  return std::monostate{};
}
