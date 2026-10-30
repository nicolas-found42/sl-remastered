#include "audio.hpp"

#include <SDL3/SDL.h>

std::unique_ptr<AudioOutput> AudioOutput::create(std::string& error) {
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { error = SDL_GetError(); return nullptr; }
	const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
	SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
	if (!stream) {
		error = SDL_GetError();
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return nullptr;
	}
	error.clear();
	return std::unique_ptr<AudioOutput>(new AudioOutput(stream));
}

AudioOutput::~AudioOutput() {
	SDL_DestroyAudioStream(stream_);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool AudioOutput::queue(const float* samples, size_t count, std::string& error) {
	if (count % 2 || (count && !samples) || count > maxQueuedSamples) {
		error = "invalid stereo PCM buffer";
		return false;
	}
	size_t queued = 0;
	if (!queuedSamples(queued, error)) return false;
	if (count > maxQueuedSamples - queued) { error = "audio queue is full"; return false; }
	if (count && !SDL_PutAudioStreamData(stream_, samples, static_cast<int>(count * sizeof(float)))) {
		error = SDL_GetError();
		return false;
	}
	error.clear();
	return true;
}

bool AudioOutput::resume(std::string& error) {
	if (!SDL_ResumeAudioStreamDevice(stream_)) { error = SDL_GetError(); return false; }
	error.clear();
	return true;
}

bool AudioOutput::pause(std::string& error) {
	if (!SDL_PauseAudioStreamDevice(stream_)) { error = SDL_GetError(); return false; }
	error.clear();
	return true;
}

bool AudioOutput::clear(std::string& error) {
	if (!SDL_ClearAudioStream(stream_)) { error = SDL_GetError(); return false; }
	error.clear();
	return true;
}

bool AudioOutput::queuedSamples(size_t& count, std::string& error) const {
	const int bytes = SDL_GetAudioStreamQueued(stream_);
	if (bytes < 0) { error = SDL_GetError(); return false; }
	count = static_cast<size_t>(bytes) / sizeof(float);
	error.clear();
	return true;
}
