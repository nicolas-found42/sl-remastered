#include "music_control.hpp"
#include "audio.hpp"
#include "sfx.hpp"
#include "rpk.hpp"
#include <fstream>
#include <iterator>
#include <SDL3/SDL.h>
#include "natives/registry.hpp"

#include <cstdio>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int testMenuScript(const std::filesystem::path& root, const char* label) {
	const auto menuFile = tufaLoadFile((root / "sl/Scripts/game/MainMenu.class").string());
	const auto soundFile = tufaLoadFile((root / "system/Scripts/sound/Sound.class").string());
	CHECK(menuFile.ok && soundFile.ok && !soundFile.file.classes.empty());
	const TufaClass* menu = nullptr;
	for (const auto& cls : menuFile.file.classes) if (cls.name == "java.game.MainMenuDialog") menu = &cls;
	CHECK(menu);
	const std::vector<TufaNode>* show = nullptr;
	for (const auto& list : menu->methods) for (const auto& method : list)
		if (menu->constants[method.words[1]].text == "show" && menu->constants[method.words[2]].text == "()V" &&
		    method.words[3] < menu->trees.size()) show = &menu->trees[method.words[3]];
	CHECK(show);
	size_t call = show->size();
	for (size_t i = 7; i < show->size(); ++i)
		if ((*show)[i].op == 0x12 && (*show)[i].operand < menu->constants.size() &&
		    menu->constants[(*show)[i].operand].text == "changeMusicSet") { call = i; break; }
	CHECK(call < show->size());
	std::vector<TufaNode> slice(show->begin() + call - 7, show->begin() + call + 1);
	slice.push_back({0x2a, 0, false, 0});
	const auto& soundClass = soundFile.file.classes[0];
	std::vector<const TufaClass*> classes{menu, &soundClass};
	std::vector<VmStaticFields> states{{&soundClass, {{"MUSIC_SET_MENU", int32_t{3}, "I"}}}};
	NullMusicBackend backend;
	std::array<std::vector<std::filesystem::path>, MusicPlayer::setCount> lists;
	lists[3] = {"Main_Menu/a.mp3"};
	auto player = MusicPlayer::createWithPlaylists(std::move(lists), backend, [] { return uint32_t{0}; });
	MusicPlayerControl control(*player);
	VmSoundState sound;
	sound.volumes[1] = 0.5f;
	sound.music = &control;
	VmArena arena;
	arena.soundState = &sound;
	const auto result = vmExecute(*menu, slice, {}, &nativeRegistry(), &classes, &states, &arena);
	if (!result.ok) std::fprintf(stderr, "%s menu slice: %s\n", label, result.error.c_str());
	CHECK(result.ok);
	CHECK(player->selectedSet() == 3 && player->isPlaying());
	CHECK(player->currentTrack() && *player->currentTrack() == std::filesystem::path{"Main_Menu/a.mp3"});
	CHECK(player->setVolume(3) == 0.5f && backend.playing());
	CHECK(!backend.events.empty() && backend.events.back().kind == NullMusicBackend::Event::Attenuation &&
	      backend.events.back().centiDb == -600);
	// Execute the same stock script slice with the real decoded soundtrack and
	// SDL's dummy output, then inspect the queued audio bytes.
	std::string error;
	auto output = AudioOutput::create(error);
	CHECK(output);
	auto realBackend = createAudioToolboxMusicBackend(*output);
	auto realPlayer = MusicPlayer::create(root, *realBackend, [] { return uint32_t{0}; }, error);
	CHECK(realPlayer && realPlayer->trackCount(3) > 0);
	MusicPlayerControl realControl(*realPlayer);
	sound.music = &realControl;
	const auto realResult = vmExecute(*menu, slice, {}, &nativeRegistry(), &classes, &states, &arena);
	CHECK(realResult.ok && realPlayer->selectedSet() == 3 && realPlayer->isPlaying());
	CHECK(output->pause(error));
	CHECK(realPlayer->update(error));
	size_t queued = 0;
	CHECK(output->queuedSamples(queued, error) && queued > 0);
	std::ifstream sounds(root / "sound.rpk", std::ios::binary);
	CHECK(sounds);
	std::vector<uint8_t> soundPack((std::istreambuf_iterator<char>(sounds)), {});
	const auto parsed = rpkParse(soundPack.data(), soundPack.size());
	CHECK(parsed.ok);
	const RpkEntry* soundEntry = nullptr;
	for (const auto& entry : parsed.pack.entries) if (entry.typeId == 0x1d) soundEntry = &entry;
	CHECK(soundEntry);
	auto sample = loadSoundSample(soundPack, *soundEntry, root, error);
	CHECK(sample);
	auto effects = SoundEffects::create(error);
	CHECK(effects && effects->play(29, 17, *sample, 1.0f, 1.0f, error));
	CHECK(effects->voiceCount() == 1);
	effects->stop(29, 17);
	size_t stillQueued = 0;
	CHECK(output->queuedSamples(stillQueued, error) && stillQueued == queued);
	std::printf("%s menu script, soundtrack decode and independent effect: ok\n", label);
	return 0;
}

int main() {
	CHECK(SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1) == 0);
	const std::filesystem::path source = SL_SOURCE_DIR;
	CHECK(testMenuScript(source / "ref/raw", "modified installed") == 0);
	const auto stock = source / ".scratch/2005-extract/Program Files/Street Legal Racing - Redline";
	if (std::filesystem::exists(stock / "sl/Scripts/game/MainMenu.class"))
		CHECK(testMenuScript(stock, "2005 extracted") == 0);
	else std::puts("2005 extracted menu script: skipped (data unavailable)");
	NullMusicBackend backend;
	std::array<std::vector<std::filesystem::path>, MusicPlayer::setCount> lists;
	lists[3] = {"Main_Menu/a.mp3"};
	lists[1] = {"Roam_Ride/a.mp3", "Roam_Ride/b.mp3", "Roam_Ride/c.mp3"};
	auto player = MusicPlayer::createWithPlaylists(lists, backend, [] { return uint32_t{0}; });
	MusicPlayerControl control(*player);
	VmSoundState sound;
	sound.music = &control;
	VmArena arena;
	arena.soundState = &sound;
	const auto native = [&](const char* name, const char* descriptor, const std::vector<VmValue>& args) {
		return invokeNative("java.sound.Sound", name, descriptor, args, nullptr, &arena);
	};
	const auto ok = [](const NativeResult& r) { return !std::holds_alternative<std::string>(r); };

	CHECK(ok(native("changeMusicSet", "(I)V", {int32_t{3}})));
	CHECK(player->selectedSet() == 3);
	CHECK(backend.playing());
	CHECK(backend.events.front().kind == NullMusicBackend::Event::Play);
	CHECK(backend.events.front().file == std::filesystem::path{"Main_Menu/a.mp3"});

	const size_t before = backend.events.size();
	CHECK(ok(native("changeMusicSet", "(I)V", {int32_t{3}})));
	CHECK(backend.events.size() == before);

	CHECK(ok(native("changeMusicSet", "(I)V", {int32_t{1}})));
	CHECK(player->selectedSet() == 1);
	CHECK(*player->currentTrack() == std::filesystem::path{"Roam_Ride/a.mp3"});
	CHECK(ok(native("nextTrack", "()V", {})));
	CHECK(*player->currentTrack() == std::filesystem::path{"Roam_Ride/b.mp3"});
	CHECK(ok(native("prevTrack", "()V", {})));
	CHECK(*player->currentTrack() == std::filesystem::path{"Roam_Ride/a.mp3"});

	const auto asInt = [](const NativeResult& r) { return std::get<int32_t>(std::get<VmValue>(r)); };
	CHECK(asInt(native("getMode", "()I", {})) == 3);
	CHECK(asInt(native("setMode", "(I)I", {int32_t{0}})) == 0);
	CHECK(asInt(native("setMode", "(I)I", {int32_t{7}})) == 0);

	// Channel 1 volume below the stock threshold silences the current set.
	CHECK(ok(native("setVolume", "(IF)V", {int32_t{1}, 0.0f})));
	CHECK(!backend.playing());
	CHECK(ok(native("setVolume", "(IF)V", {int32_t{1}, 1.0f})));
	CHECK(backend.playing());

	CHECK(ok(native("changeMusicSet", "(I)V", {int32_t{42}})));
	CHECK(player->selectedSet() == -1);
	CHECK(!backend.playing());
	CHECK(asInt(native("getMode", "()I", {})) == -1);
	CHECK(asInt(native("setMode", "(I)I", {int32_t{2}})) == 2);
	return 0;
}
