#include "resource_sound.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
#include <fstream>
#include <iterator>
#include <string>

int main() {
	std::ifstream file("ref/raw/sound.rpk", std::ios::binary);
	CHECK(file);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
	const auto pack = rpkParse(bytes.data(), bytes.size());
	CHECK(pack.ok);
	const RpkEntry* entry = nullptr;
	for (const auto& candidate : pack.pack.entries) if (candidate.typeId == 0x1d) entry = &candidate;
	CHECK(entry);
	std::string error;
	auto sound = loadSoundSample(bytes, *entry, "ref/raw", error);
	CHECK(sound && error.empty());
	CHECK(!sound->pcm.empty() && sound->spec.freq > 0 && sound->spec.channels > 0);
	CHECK(sound->stockBufferBytes > sound->pcm.size());
	CHECK(sound->volume == 1.0f && sound->instances == 10 && sound->minDistance == 5.0f && sound->maxDistance == 1000.0f);
	auto invalid = *entry; invalid.payloadOffset = static_cast<uint32_t>(bytes.size());
	CHECK(!loadSoundSample(bytes, invalid, "ref/raw", error) && !error.empty());
	const std::string escape = "sourcefile ../outside.wav\n";
	std::vector<uint8_t> unsafe(escape.begin(), escape.end());
	invalid.payloadOffset = 0;
	invalid.payloadSize = static_cast<uint32_t>(unsafe.size());
	CHECK(!loadSoundSample(unsafe, invalid, "ref/raw", error) && error.find("unsafe") != std::string::npos);
}
