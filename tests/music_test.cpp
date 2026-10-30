#include "audio.hpp"
#include "music.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <system_error>

namespace fs = std::filesystem;
using Lists = std::array<std::vector<fs::path>, MusicPlayer::setCount>;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static std::vector<fs::path> names(std::initializer_list<const char*> n) {
	std::vector<fs::path> v;
	for (const char* s : n) v.emplace_back(s);
	return v;
}

static MusicPlayer::TickSource fixedTick(uint32_t t) { return [t] { return t; }; }

struct Fixture {
	NullMusicBackend backend;
	std::unique_ptr<MusicPlayer> player;
	explicit Fixture(Lists lists, MusicPlayer::TickSource tick = nullptr) {
		player = MusicPlayer::createWithPlaylists(std::move(lists), backend, tick ? tick : fixedTick(0));
	}
	const NullMusicBackend::Event* lastPlay() const {
		for (size_t i = backend.events.size(); i-- > 0;)
			if (backend.events[i].kind == NullMusicBackend::Event::Play) return &backend.events[i];
		return nullptr;
	}
	size_t plays() const {
		size_t n = 0;
		for (const auto& e : backend.events) n += e.kind == NullMusicBackend::Event::Play;
		return n;
	}
};

static Lists sampleLists() {
	Lists l;
	l[0] = names({"g0.mp3", "g1.mp3", "g2.mp3"});
	l[1] = names({"r0.mp3", "r1.mp3"});
	l[2] = {};
	l[3] = names({"m0.mp3"});
	return l;
}

static void testAttenuation() {
	CHECK(musicAttenuationCentiDb(1.0f) == 0);
	CHECK(musicAttenuationCentiDb(0.5f) == -600);
	CHECK(musicAttenuationCentiDb(0.0f) == -10000);
	CHECK(musicAttenuationCentiDb(0.999f) == -10);
	CHECK(musicAttenuationCentiDb(1.0f / 64.0f) == -3610);
	CHECK(musicAttenuationCentiDb(-0.01f) == -10000);  // truncates toward zero into index 0
	CHECK(musicAttenuationCentiDb(-1.0f) == 0);       // index -64 is out of range, so full volume
	CHECK(musicAttenuationCentiDb(-0.5f) == 0);
	CHECK(musicAttenuationCentiDb(2.0f) == 0);
	CHECK(musicAttenuationCentiDb(std::numeric_limits<float>::quiet_NaN()) == 0);
	CHECK(musicAttenuationCentiDb(std::numeric_limits<float>::infinity()) == 0);
	CHECK(musicAttenuationToGain(0) == 1.0f);
	CHECK(musicAttenuationToGain(-10000) == 0.0f);
	CHECK(std::fabs(musicAttenuationToGain(-600) - 0.5012f) < 0.001f);
}

static void testInitialState() {
	Fixture f(sampleLists(), fixedTick(7));
	CHECK(f.player->selectedSet() == -1);
	CHECK(f.player->getMode() == -1);
	CHECK(f.player->trackIndex(0) == 7 % 3);
	CHECK(f.player->trackIndex(1) == 7 % 2);
	CHECK(f.player->trackIndex(2) == 0);
	CHECK(f.player->trackIndex(3) == 0);
	CHECK(f.player->setVolume(0) == 1.0f);
	CHECK(f.player->trackCount(2) == 0);
	CHECK(f.player->trackCount(9) == 0);
	CHECK(!f.player->isPlaying());
	CHECK(f.backend.events.empty());
}

static void testSelectAndSwitch() {
	Fixture f(sampleLists(), fixedTick(1));
	f.player->changeMusicSet(0, 0.5f);
	CHECK(f.player->selectedSet() == 0);
	CHECK(f.player->isPlaying());
	CHECK(f.lastPlay() && f.lastPlay()->file == "g1.mp3");
	CHECK(f.lastPlay()->loop == false);  // mode 3 does not loop natively
	CHECK(f.backend.events.back().kind == NullMusicBackend::Event::Attenuation && f.backend.events.back().centiDb == -600);
	CHECK(f.player->setVolume(0) == 0.5f);
	CHECK(f.player->getMode() == 3);

	const size_t before = f.backend.events.size();
	f.player->changeMusicSet(0, 1.0f);  // same set is a no-op, even with a different volume
	CHECK(f.backend.events.size() == before);
	CHECK(f.player->setVolume(0) == 0.5f);

	f.player->changeMusicSet(3, 1.0f);
	CHECK(f.player->selectedSet() == 3);
	CHECK(f.lastPlay()->file == "m0.mp3");
	CHECK(f.player->trackIndex(0) == 1);  // index survives leaving a set
	f.player->changeMusicSet(0, 1.0f);
	CHECK(f.lastPlay()->file == "g1.mp3");

	f.player->changeMusicSet(4, 1.0f);  // out of range selects no set and stops
	CHECK(f.player->selectedSet() == -1 && !f.player->isPlaying() && !f.backend.playing());
	f.player->changeMusicSet(-1, 1.0f);
	f.player->changeMusicSet(1000, 1.0f);
	CHECK(f.player->selectedSet() == -1);
}

static void testStopsBeforeSwitch() {
	Fixture f(sampleLists());
	f.player->changeMusicSet(0, 1.0f);
	f.backend.events.clear();
	f.player->changeMusicSet(1, 1.0f);
	CHECK(f.backend.events.size() >= 2);
	CHECK(f.backend.events[0].kind == NullMusicBackend::Event::Stop);
	CHECK(f.backend.events[1].kind == NullMusicBackend::Event::Play);
}

static void testNextPrev() {
	Fixture f(sampleLists());
	f.player->nextTrack();  // no set: nothing happens
	f.player->prevTrack();
	CHECK(f.backend.events.empty() && f.player->trackIndex(0) == 0);

	f.player->changeMusicSet(0, 1.0f);
	CHECK(f.lastPlay()->file == "g0.mp3");
	f.player->nextTrack();
	CHECK(f.lastPlay()->file == "g1.mp3");
	f.player->nextTrack();
	f.player->nextTrack();  // wraps past the end
	CHECK(f.lastPlay()->file == "g0.mp3" && f.player->trackIndex(0) == 0);
	f.player->prevTrack();  // wraps below zero
	CHECK(f.lastPlay()->file == "g2.mp3" && f.player->trackIndex(0) == 2);
	f.player->prevTrack();
	CHECK(f.lastPlay()->file == "g1.mp3");
}

static void testEmptySet() {
	Fixture f(sampleLists());
	f.player->changeMusicSet(2, 1.0f);
	CHECK(f.player->selectedSet() == 2);
	CHECK(f.plays() == 0 && !f.player->isPlaying());
	f.player->nextTrack();  // stock increments the index even with nothing to play
	CHECK(f.player->trackIndex(2) == 1 && f.plays() == 0);
	f.player->prevTrack();
	f.player->prevTrack();
	CHECK(f.player->trackIndex(2) == -1);
	CHECK(f.player->getMode() == 3);
}

static void testModeApi() {
	Fixture f(sampleLists());
	CHECK(f.player->setMode(2) == 2);  // no set: echoes the argument, stores nothing
	CHECK(f.player->setMode(99) == 99);
	f.player->changeMusicSet(0, 1.0f);
	CHECK(f.player->getMode() == 3);
	CHECK(f.player->setMode(1) == 1 && f.player->getMode() == 1);
	CHECK(f.player->setMode(4) == 1);   // out of range leaves the mode and returns it
	CHECK(f.player->setMode(-1) == 1);
	CHECK(f.player->setMode(0) == 0);
	f.player->changeMusicSet(1, 1.0f);
	CHECK(f.player->getMode() == 3);    // mode is per set
	f.player->changeMusicSet(0, 1.0f);
	CHECK(f.player->getMode() == 0);
}

static void testTrackEndModes() {
	std::string error;
	{
		Fixture f(sampleLists());
		f.player->changeMusicSet(0, 1.0f);
		f.player->setMode(1);
		f.backend.finishTrack();
		CHECK(f.player->update(error));
		CHECK(f.lastPlay()->file == "g1.mp3");
		f.backend.finishTrack();
		f.player->update(error);
		f.backend.finishTrack();
		f.player->update(error);
		CHECK(f.lastPlay()->file == "g0.mp3");  // wrapped
	}
	{
		Fixture f(sampleLists());
		f.player->changeMusicSet(0, 1.0f);
		f.player->setMode(2);
		f.backend.finishTrack();
		f.player->update(error);
		CHECK(f.lastPlay()->file == "g2.mp3");
	}
	{
		Fixture f(sampleLists());
		f.player->changeMusicSet(0, 1.0f);
		f.player->setMode(0);
		CHECK(f.lastPlay()->loop == false);  // loop flag was chosen at start with mode 3
		f.player->nextTrack();
		CHECK(f.lastPlay()->loop == true);   // now the backend is asked to loop natively
		const size_t plays = f.plays();
		f.backend.finishTrack();
		f.player->update(error);
		CHECK(f.plays() == plays && !f.player->isPlaying());
	}
	{
		uint32_t seq[] = {0, 0, 0, 2};
		size_t i = 0;
		Fixture f(sampleLists(), [&] { return seq[i < 4 ? i++ : 3]; });  // 3 initial calls, then picks
		CHECK(f.player->trackIndex(0) == 0);
		i = 3;
		f.player->changeMusicSet(0, 1.0f);
		CHECK(f.lastPlay()->file == "g0.mp3");
		f.backend.finishTrack();
		f.player->update(error);
		CHECK(f.lastPlay()->file == "g2.mp3");  // picked 2, differing from 0
	}
	{
		Fixture f(sampleLists(), fixedTick(0));  // a clock that never changes must not hang
		f.player->changeMusicSet(0, 1.0f);
		f.backend.finishTrack();
		f.player->update(error);
		CHECK(f.lastPlay()->file == "g1.mp3");
	}
	{
		Fixture f(sampleLists());
		f.player->changeMusicSet(3, 1.0f);  // one track, random mode
		f.backend.finishTrack();
		f.player->update(error);
		CHECK(f.lastPlay()->file == "m0.mp3" && f.plays() == 2);
	}
	{
		Fixture f(sampleLists());
		f.backend.finishTrack();  // finish with no set selected is ignored
		CHECK(f.player->update(error) && f.plays() == 0);
	}
}

static void testVolume() {
	Fixture f(sampleLists(), fixedTick(0));
	f.player->setMusicVolume(0.3f);  // no set: ignored
	CHECK(f.backend.events.empty() && f.player->setVolume(0) == 1.0f);

	f.player->changeMusicSet(0, 1.0f);
	f.player->setMusicVolume(0.25f);
	CHECK(f.backend.events.back().kind == NullMusicBackend::Event::Attenuation);
	CHECK(f.backend.events.back().centiDb == musicAttenuationCentiDb(0.25f));
	CHECK(f.player->setVolume(0) == 0.25f);
	f.player->setMusicVolume(1.75f);  // stock stores it unclamped
	CHECK(f.player->setVolume(0) == 1.75f && f.backend.events.back().centiDb == 0);

	const size_t plays = f.plays();
	f.player->setMusicVolume(0.005f);  // below 0.01 stores 0 and stops
	CHECK(f.player->setVolume(0) == 0.0f && !f.player->isPlaying() && !f.backend.playing());
	f.player->setMusicVolume(0.0f);
	CHECK(f.plays() == plays);
	f.player->setMusicVolume(0.5f);  // coming back restarts the same track
	CHECK(f.plays() == plays + 1 && f.player->isPlaying() && f.lastPlay()->file == "g0.mp3");
}

static void testMutedSet() {
	Fixture f(sampleLists());
	f.player->changeMusicSet(0, 0.0f);  // volume channel at zero: set selected, nothing plays
	CHECK(f.player->selectedSet() == 0 && !f.player->isPlaying());
	f.player->nextTrack();
	CHECK(f.player->trackIndex(0) == 1 && f.plays() <= 1);
	f.player->setMusicVolume(1.0f);
	CHECK(f.player->isPlaying() && f.lastPlay()->file == "g1.mp3");
	f.player->changeMusicSet(1, 0.0f);
	f.player->changeMusicSet(0, 1.0f);  // the old per-set volume was 1.0 so it plays again
	CHECK(f.player->isPlaying() && f.player->setVolume(1) == 0.0f);
}

static void testBadFiles() {
	Lists l;
	l[0] = names({"readme.txt", "ok.MP3", "clip.WAV", "noext"});
	Fixture f(l, fixedTick(0));
	f.player->changeMusicSet(0, 1.0f);
	CHECK(!f.player->isPlaying() && f.plays() == 0);
	CHECK(f.player->lastPlayError().find("readme.txt") != std::string::npos);
	f.player->nextTrack();
	CHECK(f.player->isPlaying() && f.lastPlay()->file == "ok.MP3" && f.player->lastPlayError().empty());
	f.player->nextTrack();
	CHECK(f.lastPlay()->file == "clip.WAV");
	f.player->nextTrack();
	CHECK(!f.player->isPlaying() && f.player->currentTrack() == nullptr);

	f.backend.failPlay = true;
	f.player->nextTrack();
	CHECK(f.player->trackIndex(0) == 0);
	f.player->nextTrack();
	CHECK(!f.player->isPlaying() && f.player->lastPlayError() == "null backend: play refused");
}

class FailingPumpBackend final : public MusicBackend {
public:
	const char* name() const override { return "failing test backend"; }
	bool play(const fs::path&, bool, std::string& error) override { playing = true; error.clear(); return true; }
	void stop() override { playing = false; }
	void setAttenuationCentiDb(int32_t) override {}
	bool takeTrackFinished() override { return false; }
	bool pump(std::string& error) override { error = "decode failed"; return false; }
	bool playing = false;
};

static void testPumpFailure() {
	FailingPumpBackend backend;
	auto player = MusicPlayer::createWithPlaylists(sampleLists(), backend, fixedTick(0));
	player->changeMusicSet(0, 1.0f);
	CHECK(player->isPlaying() && backend.playing);
	std::string error;
	CHECK(!player->update(error));
	CHECK(error == "decode failed");
	CHECK(player->lastPlayError() == "decode failed");
	CHECK(!player->isPlaying() && !backend.playing && player->currentTrack() == nullptr);
}

static fs::path makeTemp(const char* tag) {
	std::error_code ec;
	fs::path base = fs::temp_directory_path(ec) / (std::string("sl_music_test_") + tag + "_" + std::to_string(std::rand()));
	fs::remove_all(base, ec);
	fs::create_directories(base);
	return base;
}

static void touch(const fs::path& p) {
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << "x";
}

static void testScan() {
	std::string error;
	const fs::path root = makeTemp("scan");
	touch(root / "MUSIC" / "garage_shop" / "b.mp3");
	touch(root / "MUSIC" / "garage_shop" / "A.mp3");
	touch(root / "MUSIC" / "garage_shop" / "notes.txt");
	touch(root / "MUSIC" / "garage_shop" / "sub" / "c.mp3");
	touch(root / "MUSIC" / "garage_shop" / "z.mp3");
	touch(root / "MUSIC" / "ROAM_RIDE" / "only.wav");
	fs::create_directories(root / "MUSIC" / "Main_Menu");  // empty set
	NullMusicBackend backend;
	auto p = MusicPlayer::create(root, backend, fixedTick(0), error);
	CHECK(p != nullptr);
	if (p) {
		CHECK(p->trackCount(0) == 5);  // every file counts, the sub folder is scanned in place
		CHECK(p->trackCount(1) == 1);
		CHECK(p->trackCount(2) == 0);  // missing folder
		CHECK(p->trackCount(3) == 0);
		p->changeMusicSet(0, 1.0f);
		CHECK(p->currentTrack() && p->currentTrack()->filename() == "A.mp3");
		const char* expected[] = {"b.mp3", nullptr, "c.mp3", "z.mp3", "A.mp3"};  // notes.txt counts but cannot play
		for (const char* want : expected) {
			p->nextTrack();
			if (want) CHECK(p->currentTrack() && p->currentTrack()->filename() == want);
			else CHECK(p->currentTrack() == nullptr);
		}
		CHECK(p->currentTrack()->parent_path().filename() != "sub");
	}
	std::error_code ec;
	fs::remove_all(root, ec);

	const fs::path none = makeTemp("none");  // no Music folder at all
	auto q = MusicPlayer::create(none, backend, fixedTick(0), error);
	CHECK(q != nullptr && q->trackCount(0) == 0 && q->trackCount(3) == 0);
	fs::remove_all(none, ec);

	auto r = MusicPlayer::create(none / "missing", backend, fixedTick(0), error);  // unreadable root is not an error
	CHECK(r != nullptr && r->trackCount(1) == 0);
}

static void testRealData() {
	const fs::path root = fs::path(SL_SOURCE_DIR) / "ref" / "raw";
	std::error_code ec;
	if (!fs::exists(root / "music", ec)) { std::puts("music real-data test: skipped (no ref/raw/music)"); return; }
	std::string error;
	NullMusicBackend backend;
	auto p = MusicPlayer::create(root, backend, fixedTick(0), error);
	CHECK(p != nullptr);
	if (!p) return;
	CHECK(p->trackCount(0) == 4);  // Garage_Shop
	CHECK(p->trackCount(1) == 6);  // Roam_ride on disk, Roam_Ride in stock
	CHECK(p->trackCount(2) == 4);  // Race_chase
	CHECK(p->trackCount(3) == 1);  // Main_menu
	p->changeMusicSet(3, 1.0f);
	CHECK(p->isPlaying() && p->currentTrack() && p->currentTrack()->extension() == ".mp3");
	CHECK(p->currentTrack()->filename().string().find("Redline mastered") != std::string::npos);
}

static bool writeWav(const fs::path& path, double seconds, double hz) {
	const uint32_t rate = 44100;
	const uint32_t n = static_cast<uint32_t>(seconds * rate);
	std::ofstream o(path, std::ios::binary);
	auto w32 = [&](uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); };
	auto w16 = [&](uint16_t v) { o.write(reinterpret_cast<const char*>(&v), 2); };
	o.write("RIFF", 4); w32(36 + n * 2); o.write("WAVEfmt ", 8); w32(16); w16(1); w16(1); w32(rate); w32(rate * 2); w16(2); w16(16);
	o.write("data", 4); w32(n * 2);
	for (uint32_t i = 0; i < n; ++i) w16(static_cast<uint16_t>(static_cast<int16_t>(8000 * std::sin(6.2831853 * hz * i / rate))));
	return static_cast<bool>(o);
}

static void testAudioBackend() {
	if (!SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy")) { CHECK(false); return; }
	std::string error;
	auto output = AudioOutput::create(error);
	CHECK(output != nullptr);
	if (!output) return;
	auto backend = createAudioToolboxMusicBackend(*output);
	const fs::path dir = makeTemp("wav");
	CHECK(writeWav(dir / "a.wav", 0.3, 440));
	CHECK(writeWav(dir / "b.wav", 0.3, 880));

	CHECK(!backend->play(dir / "missing.wav", false, error) && !error.empty());
	touch(dir / "bad.mp3");
	CHECK(!backend->play(dir / "bad.mp3", false, error));

	CHECK(backend->play(dir / "a.wav", false, error));
	size_t queued = 0;
	CHECK(backend->pump(error) && output->queuedSamples(queued, error) && queued > 0);
	backend->stop();
	CHECK(output->queuedSamples(queued, error) && queued == 0 && !backend->takeTrackFinished());

	// A whole track plays to its end and reports it once.
	CHECK(backend->play(dir / "a.wav", false, error));
	bool finished = false;
	for (Uint64 end = SDL_GetTicks() + 5000; !finished && SDL_GetTicks() < end; SDL_Delay(5)) {
		CHECK(backend->pump(error));
		finished = backend->takeTrackFinished();
	}
	CHECK(finished && !backend->takeTrackFinished());

	// Looping never finishes and keeps feeding.
	CHECK(backend->play(dir / "a.wav", true, error));
	bool spurious = false;
	for (Uint64 end = SDL_GetTicks() + 700; SDL_GetTicks() < end; SDL_Delay(5)) {
		CHECK(backend->pump(error));
		spurious = spurious || backend->takeTrackFinished();
	}
	CHECK(!spurious);
	backend->stop();

	// Silence gain produces silent samples. Full gain does not.
	backend->setAttenuationCentiDb(0);
	CHECK(backend->play(dir / "a.wav", false, error));
	CHECK(backend->pump(error));
	backend->stop();

	// The player drives two files in NextTrack mode over the real backend.
	Lists lists;
	lists[3] = {dir / "a.wav", dir / "b.wav"};
	auto player = MusicPlayer::createWithPlaylists(std::move(lists), *backend, fixedTick(0));
	player->changeMusicSet(3, 1.0f);
	player->setMode(1);
	CHECK(player->isPlaying() && player->currentTrack()->filename() == "a.wav");
	bool advanced = false;
	for (Uint64 end = SDL_GetTicks() + 5000; !advanced && SDL_GetTicks() < end; SDL_Delay(5)) {
		CHECK(player->update(error));
		advanced = player->currentTrack() && player->currentTrack()->filename() == "b.wav";
	}
	CHECK(advanced);
	player->changeMusicSet(-1, 1.0f);
	CHECK(output->queuedSamples(queued, error) && queued == 0);

	// A real MP3 from the install decodes.
	const fs::path menu = fs::path(SL_SOURCE_DIR) / "ref/raw/music/Main_menu";
	std::error_code ec;
	if (fs::exists(menu, ec)) {
		for (const auto& e : fs::directory_iterator(menu)) {
			CHECK(backend->play(e.path(), false, error));
			CHECK(backend->pump(error) && output->queuedSamples(queued, error) && queued > 0);
			backend->stop();
			break;
		}
	} else {
		std::puts("music mp3 decode test: skipped (no ref/raw/music)");
	}
	fs::remove_all(dir, ec);
}

int main() {
	testAttenuation();
	testInitialState();
	testSelectAndSwitch();
	testStopsBeforeSwitch();
	testNextPrev();
	testEmptySet();
	testModeApi();
	testTrackEndModes();
	testVolume();
	testMutedSet();
	testBadFiles();
	testPumpFailure();
	testScan();
	testRealData();
	testAudioBackend();
	if (failures) { std::fprintf(stderr, "music test: %d failures\n", failures); return 1; }
	std::puts("music test: ok");
	return 0;
}
