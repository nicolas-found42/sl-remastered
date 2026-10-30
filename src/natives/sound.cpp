#include "natives/sound.hpp"

namespace {
VmSoundState* sound(VmArena* arena) { return arena ? arena->soundState : nullptr; }
VmMusicControl* music(VmArena* arena) { return sound(arena) ? sound(arena)->music : nullptr; }
}

NativeResult soundSetVolume(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (!sound(arena) || args.size() != 2 || !std::holds_alternative<int32_t>(args[0]) ||
	    !std::holds_alternative<float>(args[1])) return std::string{"invalid Sound.setVolume call"};
	const int32_t channel = std::get<int32_t>(args[0]);
	const float value = std::get<float>(args[1]);
	if (channel >= 0 && channel < 3) sound(arena)->volumes[channel] = value;
	if (channel == 1 && music(arena)) music(arena)->setMusicVolume(value);
	return std::monostate{};
}

NativeResult soundGetVolume(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (!sound(arena) || args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Sound.getVolume call"};
	const int32_t channel = std::get<int32_t>(args[0]);
	return VmValue{channel >= 0 && channel < 3 ? sound(arena)->volumes[channel] : 0.0f};
}

NativeResult soundChangeMusicSet(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                                 const std::vector<const TufaClass*>*) {
	if (!music(arena) || args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Sound.changeMusicSet call"};
	music(arena)->changeMusicSet(std::get<int32_t>(args[0]), sound(arena)->volumes[1]);
	return std::monostate{};
}

NativeResult soundNextTrack(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (!music(arena) || !args.empty()) return std::string{"invalid Sound.nextTrack call"};
	music(arena)->nextTrack();
	return std::monostate{};
}

NativeResult soundPrevTrack(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (!music(arena) || !args.empty()) return std::string{"invalid Sound.prevTrack call"};
	music(arena)->prevTrack();
	return std::monostate{};
}

NativeResult soundSetMode(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                          const std::vector<const TufaClass*>*) {
	if (!music(arena) || args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Sound.setMode call"};
	return VmValue{music(arena)->setMode(std::get<int32_t>(args[0]))};
}

NativeResult soundGetMode(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                          const std::vector<const TufaClass*>*) {
	if (!music(arena) || !args.empty()) return std::string{"invalid Sound.getMode call"};
	return VmValue{music(arena)->getMode()};
}
