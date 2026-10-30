#include "sfx.hpp"
#include "rpk.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
#include <fstream>
#include <iterator>
#include <vector>

int main() {
	SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1);
	std::ifstream file("ref/raw/sound.rpk", std::ios::binary);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
	const auto pack = rpkParse(bytes.data(), bytes.size());
	CHECK(pack.ok);
	const RpkEntry* entry = nullptr;
	for (const auto& e : pack.pack.entries) if (e.typeId == 0x1d) entry = &e;
	CHECK(entry);
	std::string error;
	auto sample = loadSoundSample(bytes, *entry, "ref/raw", error);
	CHECK(sample);
	auto effects = SoundEffects::create(error);
	CHECK(effects);
	SoundEffects::Outcome outcome{};
	CHECK(effects->play(29, 10, *sample, 1.0f, 1.0f, error, &outcome) && outcome == SoundEffects::Outcome::Started);
	CHECK(effects->play(29, 11, *sample, 0.5f, 1.0f, error, &outcome) && outcome == SoundEffects::Outcome::Started);
	CHECK(effects->voiceCount() == 2);
	// Stock state 3 (FUN_0055c120): a named voice that is playing gets new volume and pitch, not a second voice.
	CHECK(effects->play(29, 10, *sample, 0.5f, 1.5f, error, &outcome) && outcome == SoundEffects::Outcome::Updated);
	CHECK(effects->voiceCount() == 2);
	// Stock drops requests beyond the per-sound instance limit (FUN_00550980 state 1 to 0) without an error.
	SoundSample one = *sample;
	one.instances = 1;
	CHECK(effects->play(30, 0, one, 1.0f, 1.0f, error, &outcome) && outcome == SoundEffects::Outcome::Started);
	CHECK(effects->play(30, 0, one, 1.0f, 1.0f, error, &outcome) && outcome == SoundEffects::Outcome::Dropped);
	CHECK(effects->play(30, 5, one, 1.0f, 1.0f, error, &outcome) && outcome == SoundEffects::Outcome::Dropped);
	CHECK(effects->voiceCount() == 3);
	one.instances = 0;
	CHECK(effects->play(31, 0, one, 1.0f, 1.0f, error, &outcome) && outcome == SoundEffects::Outcome::Dropped);
	CHECK(effects->voiceCount() == 3);
	effects->stop(30, 0);
	CHECK(effects->voiceCount() == 2);
	effects->stop(29, 10);
	CHECK(effects->voiceCount() == 1);
	effects->stop(30, 11);
	CHECK(effects->voiceCount() == 1);
	effects->stop(29, 11);
	CHECK(effects->voiceCount() == 0);
	CHECK(!effects->play(29, 12, *sample, 1.0f, 0.0f, error));
}
