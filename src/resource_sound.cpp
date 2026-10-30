#include "resource_sound.hpp"
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <string_view>
#include <system_error>

std::unique_ptr<SoundSample> loadSoundSample(const std::vector<uint8_t>& pack,
                                             const RpkEntry& entry,
                                             const std::filesystem::path& root,
                                             std::string& error) {
	auto fail = [&](const char* message) -> std::unique_ptr<SoundSample> { error = message; return {}; };
	if (entry.resType != 6 || !entry.payloadSize || entry.payloadOffset > pack.size() ||
	    entry.payloadSize > pack.size() - entry.payloadOffset) return fail("sound: invalid resource payload");
	auto sample = std::make_unique<SoundSample>();
	std::string payload(reinterpret_cast<const char*>(pack.data() + entry.payloadOffset), entry.payloadSize);
	if (payload.find('\0') != std::string::npos) return fail("sound: invalid payload text");
	std::istringstream lines(payload);
	lines.imbue(std::locale::classic());
	std::string line, source;
	while (std::getline(lines, line)) {
		std::istringstream fields(line); fields.imbue(std::locale::classic());
		std::string key; fields >> key;
		if (key == "sourcefile") fields >> source;
		else if (key == "volume") fields >> sample->volume;
		else if (key == "mindist") fields >> sample->minDistance;
		else if (key == "maxdist") fields >> sample->maxDistance;
		else if (key == "instances") fields >> sample->instances;
		else if (key == "flags") fields >> sample->flags;
		else continue;
		if (fields.fail()) return fail("sound: invalid directive");
	}
	if (source.empty() || !std::isfinite(sample->volume) || !std::isfinite(sample->minDistance) ||
	    !std::isfinite(sample->maxDistance)) return fail("sound: missing source or invalid settings");
	std::replace(source.begin(), source.end(), '\\', '/');
	if (source.front() == '/' || source.find(':') != std::string::npos) return fail("sound: unsafe path");
	const std::filesystem::path relative(source);
	for (const auto& part : relative) if (part == "." || part == ".." || part.empty()) return fail("sound: unsafe path");
	std::error_code ec;
	const auto base = std::filesystem::canonical(root, ec);
	if (ec) return fail("sound: invalid root");
	const auto path = std::filesystem::canonical(base / relative, ec);
	if (ec) return fail("sound: source not found");
	auto b = base.begin(), p = path.begin();
	for (; b != base.end() && p != path.end(); ++b, ++p) if (*b != *p) return fail("sound: source escapes root");
	if (b != base.end() || !std::filesystem::is_regular_file(path, ec) || ec) return fail("sound: unsafe source");
	const auto sourceBytes = std::filesystem::file_size(path, ec);
	if (ec || sourceBytes == 0 || sourceBytes > INT32_MAX) return fail("sound: unsupported source size");
	sample->stockBufferBytes = static_cast<uint32_t>(sourceBytes);
	Uint8* data = nullptr; Uint32 size = 0;
	if (!SDL_LoadWAV(path.string().c_str(), &sample->spec, &data, &size)) { error = std::string("sound: ") + SDL_GetError(); return {}; }
	sample->pcm.assign(data, data + size);
	SDL_free(data);
	if (sample->pcm.empty()) return fail("sound: empty sample");
	error.clear(); return sample;
}
