#include "run_log.hpp"
#include "natives/sfx.hpp"
#include "natives/game_type.hpp"
#include "resource_world.hpp"
#include "../sfx.hpp"
#include <cmath>

namespace {
ResourceNode* soundNode(const VmObject* receiver, VmArena* arena) {
	if (!arena || !arena->gameState || !arena->gameState->resources) return nullptr;
	const auto* state = nativeResourceState(receiver, arena);
	if (!state) return nullptr;
	const auto handle = static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
	const auto it = arena->gameState->bindings.find(handle);
	if (it == arena->gameState->bindings.end() || !it->second) return nullptr;
	const auto resolved = arena->gameState->resources->resolve(it->second->link.owner);
	return resolved.type == 6 ? resolved.packNode : nullptr;
}
}
NativeResult sfxPlay(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                     const std::vector<const TufaClass*>*) {
	if (!arena || !receiver || args.size() != 6 || !std::holds_alternative<const VmObject*>(args[0]) ||
	    !std::holds_alternative<float>(args[1]) || !std::holds_alternative<float>(args[2]) ||
	    !std::holds_alternative<float>(args[3]) || !std::holds_alternative<int32_t>(args[4]) ||
	    !std::holds_alternative<int32_t>(args[5])) return std::string{"SfxRef.nplay: invalid call"};
	auto* node = soundNode(receiver, arena);
	if (!node || !node->loaded() || !node->sound) return VmValue{int32_t{-1}};
	if (std::get<const VmObject*>(args[0]) || std::get<int32_t>(args[4]) != 0)
		return std::string{"SfxRef.nplay: spatial position or flags unsupported"};
	if (!arena->soundState || !arena->soundState->effects) return std::string{"SfxRef.nplay: no audio host"};
	const float volume = arena->soundState->volumes[0] * std::get<float>(args[3]) * node->sound->volume;
	if (std::isfinite(volume) && volume <= 0.01f) return VmValue{int32_t{-1}};
	std::string error;
	const auto id = std::get<int32_t>(args[5]);
	auto outcome = SoundEffects::Outcome::Started;
	if (!arena->soundState->effects->play(node->value, id, *node->sound, volume, std::get<float>(args[2]), error, &outcome))
		return "SfxRef.nplay: " + error;
	static constexpr const char* outcomeName[] = {"Started", "Updated", "Dropped"};
	static constexpr runlog::Counter outcomeCounter[] = {runlog::Counter::SoundStarted, runlog::Counter::SoundUpdated,
	                                                      runlog::Counter::SoundDropped};
	runlog::count(outcomeCounter[static_cast<int>(outcome)]);
	runlog::event(outcome == SoundEffects::Outcome::Dropped ? runlog::EventKind::Drop : runlog::EventKind::Sound,
	              "%s resource 0x%08x id %d volume %.2f", outcomeName[static_cast<int>(outcome)],
	              static_cast<unsigned>(node->value), static_cast<int>(id), static_cast<double>(volume));
	return VmValue{id};
}
NativeResult sfxStop(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                     const std::vector<const TufaClass*>*) {
	if (!arena || !receiver || args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"SfxRef.stop: invalid call"};
	auto* node = soundNode(receiver, arena);
	if (!node || !node->loaded() || !node->sound) return std::monostate{};
	if (!arena->soundState || !arena->soundState->effects) return std::string{"SfxRef.stop: no audio host"};
	arena->soundState->effects->stop(node->value, std::get<int32_t>(args[0]));
	return std::monostate{};
}
