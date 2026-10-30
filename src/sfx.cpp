#include "sfx.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

namespace {
// DirectSound attenuation table at identified-stock 0061b700, in hundredths of a dB.
constexpr int attenuation[] = {-10000, -3610, -3010, -2660, -2410, -2210, -2060, -1920, -1810, -1700, -1610, -1530, -1450, -1380, -1320, -1260, -1200, -1150, -1100, -1050, -1010, -970, -930, -890, -850, -820, -780, -750, -720, -690, -660, -630, -600, -580, -550, -520, -500, -480, -450, -430, -410, -390, -370, -350, -330, -310, -290, -270, -250, -230, -210, -200, -180, -160, -150, -130, -120, -100, -90, -70, -60, -40, -30, -10, 0};
}

struct SoundEffects::Impl {
	struct Voice {
		uint32_t resource; int32_t id; SDL_AudioStream* stream; int openFreq;
		std::chrono::steady_clock::time_point done;
	};
	std::vector<Voice> voices;
	~Impl() { for (const auto& voice : voices) SDL_DestroyAudioStream(voice.stream); }
	void prune() {
		const auto now = std::chrono::steady_clock::now();
		voices.erase(std::remove_if(voices.begin(), voices.end(), [&](const Voice& v) {
			if (now < v.done || SDL_GetAudioStreamQueued(v.stream) != 0) return false;
			SDL_DestroyAudioStream(v.stream); return true;
		}), voices.end());
	}
};
SoundEffects::SoundEffects() : impl_(std::make_unique<Impl>()) {}
SoundEffects::~SoundEffects() { impl_.reset(); SDL_QuitSubSystem(SDL_INIT_AUDIO); }
std::unique_ptr<SoundEffects> SoundEffects::create(std::string& error) {
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { error = SDL_GetError(); return {}; }
	error.clear(); return std::unique_ptr<SoundEffects>(new SoundEffects());
}
bool SoundEffects::play(uint32_t resourceId, int32_t id, const SoundSample& sample,
                        float volume, float pitch, std::string& error, Outcome* outcome) {
	auto report = [&](Outcome value) { if (outcome) *outcome = value; error.clear(); return true; };
	if (!resourceId || sample.pcm.empty() || sample.pcm.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
	    sample.spec.freq <= 0 || sample.spec.channels <= 0 || !std::isfinite(volume) ||
	    !std::isfinite(pitch) || volume <= 0.01f || volume > 1.0f || pitch <= 0) {
		error = "sfx: unsupported sample, volume or pitch"; return false;
	}
	const int level = static_cast<int>(volume * 64.0f);
	if (!level) { error = "sfx: volume rounds to silence"; return false; }
	const float gain = std::pow(10.0f, static_cast<float>(attenuation[level]) / 2000.0f);
	const double requested = static_cast<double>(sample.spec.freq) * pitch;
	const int frequency = static_cast<int>(std::clamp(requested, 100.0, 200000.0));
	impl_->prune();
	if (id) {
		for (auto& voice : impl_->voices) if (voice.resource == resourceId && voice.id == id) {
			if (!SDL_SetAudioStreamGain(voice.stream, gain) ||
			    !SDL_SetAudioStreamFrequencyRatio(voice.stream, static_cast<float>(frequency) / static_cast<float>(voice.openFreq))) {
				error = SDL_GetError(); return report(Outcome::Dropped);
			}
			return report(Outcome::Updated);
		}
	}
	if (impl_->voices.size() >= 64) return report(Outcome::Dropped);
	const auto perResource = std::count_if(impl_->voices.begin(), impl_->voices.end(),
		[&](const Impl::Voice& v) { return v.resource == resourceId; });
	if (sample.instances <= 0 || perResource >= std::min(sample.instances, 8)) return report(Outcome::Dropped);
	SDL_AudioSpec spec = sample.spec;
	spec.freq = frequency;
	// Stock keeps going without a usable device (0055b9a0 returns -1 when DAT_00784cac is 0), so a host
	// audio failure drops the voice and reports the reason through `error`.
	SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
	if (!stream) { error = SDL_GetError(); if (outcome) *outcome = Outcome::Dropped; return true; }
	const int bytesPerFrame = SDL_AUDIO_BYTESIZE(spec.format) * spec.channels;
	if (bytesPerFrame <= 0) { SDL_DestroyAudioStream(stream); error = "sfx: invalid sample format"; return false; }
	if (!SDL_SetAudioStreamGain(stream, gain) ||
	    !SDL_PutAudioStreamData(stream, sample.pcm.data(), static_cast<int>(sample.pcm.size())) ||
	    !SDL_FlushAudioStream(stream) || !SDL_ResumeAudioStreamDevice(stream)) {
		error = SDL_GetError(); SDL_DestroyAudioStream(stream); if (outcome) *outcome = Outcome::Dropped; return true;
	}
	const double seconds = static_cast<double>(sample.pcm.size()) / bytesPerFrame / spec.freq;
	const auto end = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds + 0.25));
	impl_->voices.push_back({resourceId, id, stream, spec.freq, end});
	return report(Outcome::Started);
}
void SoundEffects::stop(uint32_t resourceId, int32_t id) {
	for (auto it = impl_->voices.begin(); it != impl_->voices.end(); ++it) {
		if (it->resource == resourceId && it->id == id) {
			SDL_DestroyAudioStream(it->stream); impl_->voices.erase(it); return;
		}
	}
}
size_t SoundEffects::voiceCount() const { return impl_->voices.size(); }
