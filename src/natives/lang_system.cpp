#include "natives/lang_system.hpp"
#include "natives/game_type.hpp"
#include "camera_frame.hpp"
#include "resource_world.hpp"
#include "timers.hpp"
#include <cmath>
#include <limits>


NativeResult systemSetLdPriority(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                                 const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid System.setLdPriority call"};
	if (!arena || !arena->gameState || !arena->gameState->visibility)
		return std::string{"System.setLdPriority: visibility state unavailable"};
	arena->gameState->visibility->setPriority(std::get<int32_t>(args[0]));
	return std::monostate{};
}

namespace {
bool isArray(const VmObject* object) { return object && !object->runtimeClass && !object->arrayDescriptor.empty(); }
}

NativeResult systemArraycopy(const std::vector<VmValue>& args, const VmObject*, VmArena*,
                             const std::vector<const TufaClass*>*) {
	if (args.size() != 5 || !std::holds_alternative<const VmObject*>(args[0]) || !std::holds_alternative<int32_t>(args[1]) ||
	    !std::holds_alternative<const VmObject*>(args[2]) || !std::holds_alternative<int32_t>(args[3]) ||
	    !std::holds_alternative<int32_t>(args[4])) return std::string{"invalid System.arraycopy call"};
	const VmObject* source = std::get<const VmObject*>(args[0]);
	const VmObject* destination = std::get<const VmObject*>(args[2]);
	const int64_t sourcePos = std::get<int32_t>(args[1]), destinationPos = std::get<int32_t>(args[3]);
	const int64_t count = std::get<int32_t>(args[4]);
	if (!isArray(source) || !isArray(destination) || source->arrayDescriptor != destination->arrayDescriptor)
		return std::string{"unsupported System.arraycopy arrays"};
	// Stock checks only the upper bounds; negative values and bad indices dereference address 8.
	if (sourcePos < 0 || destinationPos < 0 || count < 0) return std::string{"System.arraycopy negative argument"};
	if (static_cast<int64_t>(source->arrayElements.size()) < sourcePos + count ||
	    static_cast<int64_t>(destination->arrayElements.size()) < destinationPos + count)
		return std::string{"System.arraycopy: copy out of bounds"};
	if (source->arrayElements.empty() || destination->arrayElements.empty())
		return std::string{"System.arraycopy: empty array"};
	const std::vector<VmValue> moved(source->arrayElements.begin() + sourcePos,
	                                 source->arrayElements.begin() + sourcePos + count);
	for (int64_t i = 0; i < count; ++i) destination->arrayElements[static_cast<size_t>(destinationPos + i)] = moved[static_cast<size_t>(i)];
	return std::monostate{};
}

NativeResult systemExitString(const std::vector<VmValue>& args, const VmObject*, VmArena*,
                              const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<const VmObject*>(args[0])) return std::string{"invalid System.exit call"};
	return std::monostate{};
}

NativeResult systemLog(const std::vector<VmValue>& args, const VmObject*, VmArena*,
                       const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<const VmObject*>(args[0])) return std::string{"invalid System.log call"};
	return std::monostate{};
}

NativeResult systemOpenLib(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                           const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<const VmObject*>(args[0]))
		return std::string{"invalid System.openLib call"};
	if (!arena || !arena->gameState || !arena->gameState->packs || !arena->gameState->resources)
		return std::string{"System.openLib: resource world unavailable"};
	const VmObject* object = std::get<const VmObject*>(args[0]);
	const std::string* name = nullptr;
	for (const auto& entry : arena->strings) if (entry.second == object) {
		if (name) return std::string{"System.openLib: ambiguous string"};
		name = &entry.first;
	}
	if (!object || !name) return std::string{"System.openLib: invalid string"};
	uint16_t slot;
	std::string error;
	if (!arena->gameState->packs->registerPackName(*name, slot, error))
		return std::string{"System.openLib: "} + error;
	auto& resources = *arena->gameState->resources;
	if (resources.mountState(slot) == MountState::Mounted) return static_cast<int32_t>(slot);
	const auto result = resources.lookup((static_cast<uint32_t>(slot) << 16) | 1);
	if (result.status == LookupStatus::Found) return static_cast<int32_t>(slot);
	if (result.status == LookupStatus::NotFound) return int32_t{-1};
	return std::string{"System.openLib: "} + result.detail;
}

NativeResult systemCurrentTime(const std::vector<VmValue>& args, const VmObject*, VmArena* arena, const std::vector<const TufaClass*>*) {
  if (!args.empty() || !arena || !arena->performanceMilliseconds) return std::string{"System.currentTime: performance clock unavailable"};
  const double milliseconds = arena->performanceMilliseconds();
  if (!std::isfinite(milliseconds)) return std::string{"System.currentTime: invalid clock"};
  return VmValue{static_cast<float>(milliseconds * static_cast<double>(0.001f))};
}

NativeResult systemTimeWarp(const std::vector<VmValue>& args, const VmObject*, VmArena* arena, const std::vector<const TufaClass*>*) {
  if (args.size() != 1 || !std::holds_alternative<float>(args[0]) || !arena || !arena->timers) return std::string{"System.timeWarp: clock state unavailable"};
  const float previous = arena->timers->timeScale(); std::string error;
  if (std::get<float>(args[0]) < 0) return VmValue{previous};
  if (!arena->performanceMilliseconds) return std::string{"System.timeWarp: performance clock unavailable"};
  if (!arena->timers->setTimeWarp(std::get<float>(args[0]), arena->performanceMilliseconds(), error)) return error;
  return VmValue{previous};
}

NativeResult systemSyncGameTime(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
  if (args.size() != 1 || !std::holds_alternative<float>(args[0]) || !arena || !arena->gameState)
    return std::string{"System.syncGameTime: clock state unavailable"};
  const double value = std::get<float>(args[0]);
  if (!std::isfinite(value) || value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
    return std::string{"System.syncGameTime: unsupported clock value"};
  const int32_t seconds = static_cast<int32_t>(value) % 86400;
  arena->gameState->gameTimeOfDay = seconds;
  arena->gameState->gameNight = !(seconds >= 3600 && seconds < 72000 && (seconds < 18000 || seconds >= 25200));
  return std::monostate{};
}
