#pragma once

#include "rpk.hpp"
#include <SDL3/SDL_audio.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Owns the decoded sample for as long as the resource is cached.
struct SoundSample {
	SDL_AudioSpec spec{};
	std::vector<uint8_t> pcm;
	// 00505960 stores the source-file buffer length; 0053bb90 accounts it.
	uint32_t stockBufferBytes = 0;
	float volume = 1.0f;
	float minDistance = 5.0f;
	float maxDistance = 1000.0f;
	int instances = 4;
	int flags = 0;
};

std::unique_ptr<SoundSample> loadSoundSample(const std::vector<uint8_t>& pack,
                                             const RpkEntry& entry,
                                             const std::filesystem::path& root,
                                             std::string& error);
