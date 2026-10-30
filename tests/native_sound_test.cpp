#include "natives/registry.hpp"
#include <cstring>

static uint32_t bits(float x) { uint32_t b; std::memcpy(&b, &x, sizeof b); return b; }
int main() {
	VmSoundState sound;
	VmArena arena; arena.soundState = &sound;
	const auto call = [&](const std::vector<VmValue>& args) {
		return invokeNative("java.sound.Sound", "setVolume", "(IF)V", args, nullptr, &arena);
	};
	if (!std::holds_alternative<std::monostate>(call({int32_t{0}, -0.5f})) ||
	    !std::holds_alternative<std::monostate>(call({int32_t{1}, 0.25f})) ||
	    !std::holds_alternative<std::monostate>(call({int32_t{2}, 2.0f}))) return 1;
	if (bits(sound.volumes[0]) != bits(-0.5f) || bits(sound.volumes[1]) != bits(0.25f) ||
	    bits(sound.volumes[2]) != bits(2.0f)) return 1;
	if (!std::holds_alternative<std::monostate>(call({int32_t{-1}, 7.0f})) ||
	    !std::holds_alternative<std::monostate>(call({int32_t{3}, 7.0f})) ||
	    !std::holds_alternative<std::string>(call({int32_t{0}})) ||
	    !std::holds_alternative<std::string>(call({int32_t{0}, int32_t{1}}))) return 1;
	// Stock stores the channel global first and then forwards channel 1 to the music player.
	struct Fake final : VmMusicControl {
		int sets = 0, nexts = 0, prevs = 0, mode = 3; float lastVolume = -1; int32_t lastSet = -9; float setVolumeArg = -1;
		void changeMusicSet(int32_t s, float v) override { ++sets; lastSet = s; setVolumeArg = v; }
		void nextTrack() override { ++nexts; }
		void prevTrack() override { ++prevs; }
		int32_t setMode(int32_t m) override { if (m >= 0 && m <= 3) mode = m; return mode; }
		int32_t getMode() const override { return mode; }
		void setMusicVolume(float v) override { lastVolume = v; }
	} fake;
	sound.music = &fake;
	if (!std::holds_alternative<std::monostate>(call({int32_t{1}, 0.75f})) || bits(sound.volumes[1]) != bits(0.75f) ||
	    bits(fake.lastVolume) != bits(0.75f)) return 1;
	fake.lastVolume = -1;
	if (!std::holds_alternative<std::monostate>(call({int32_t{0}, 0.5f})) || bits(sound.volumes[0]) != bits(0.5f) ||
	    bits(fake.lastVolume) != bits(-1.0f)) return 1;
	const auto native = [&](const char* name, const char* descriptor, const std::vector<VmValue>& args) {
		return invokeNative("java.sound.Sound", name, descriptor, args, nullptr, &arena);
	};
	if (!std::holds_alternative<std::monostate>(native("changeMusicSet", "(I)V", {int32_t{3}})) ||
	    fake.lastSet != 3 || bits(fake.setVolumeArg) != bits(0.75f)) return 1;
	if (!std::holds_alternative<std::monostate>(native("nextTrack", "()V", {})) || fake.nexts != 1) return 1;
	if (!std::holds_alternative<std::monostate>(native("prevTrack", "()V", {})) || fake.prevs != 1) return 1;
	const auto intResult = [](const NativeResult& r) { const auto* v = std::get_if<VmValue>(&r); return v && std::holds_alternative<int32_t>(*v) ? std::get<int32_t>(*v) : INT32_MIN; };
	if (intResult(native("setMode", "(I)I", {int32_t{1}})) != 1 || intResult(native("getMode", "()I", {})) != 1 ||
	    intResult(native("setMode", "(I)I", {int32_t{9}})) != 1) return 1;
	const auto floatResult = [](const NativeResult& r) { const auto* v = std::get_if<VmValue>(&r); return v && std::holds_alternative<float>(*v) ? std::get<float>(*v) : -777.0f; };
	if (bits(floatResult(native("getVolume", "(I)F", {int32_t{0}}))) != bits(0.5f) ||
	    bits(floatResult(native("getVolume", "(I)F", {int32_t{1}}))) != bits(0.75f) ||
	    bits(floatResult(native("getVolume", "(I)F", {int32_t{2}}))) != bits(sound.volumes[2]) ||
	    bits(floatResult(native("getVolume", "(I)F", {int32_t{3}}))) != bits(0.0f) ||
	    bits(floatResult(native("getVolume", "(I)F", {int32_t{-1}}))) != bits(0.0f)) return 1;
	if (!std::holds_alternative<std::string>(native("changeMusicSet", "(I)V", {})) ||
	    !std::holds_alternative<std::string>(native("changeMusicSet", "(I)V", {0.5f})) ||
	    !std::holds_alternative<std::string>(native("getVolume", "(I)F", {0.5f}))) return 1;
	// Without a player the music natives refuse instead of pretending.
	sound.music = nullptr;
	if (!std::holds_alternative<std::string>(native("changeMusicSet", "(I)V", {int32_t{0}})) ||
	    !std::holds_alternative<std::string>(native("getMode", "()I", {}))) return 1;
	if (!std::holds_alternative<std::monostate>(call({int32_t{1}, 0.1f})) || bits(sound.volumes[1]) != bits(0.1f)) return 1;
	return 0;
}
