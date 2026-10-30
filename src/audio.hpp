#pragma once

#include <cstddef>
#include <memory>
#include <string>

struct SDL_AudioStream;

// The owner serializes calls to this object. Samples are interleaved stereo float PCM at 48 kHz.
class AudioOutput {
public:
	static constexpr size_t maxQueuedSamples = 48000 * 2 * 2;
	static std::unique_ptr<AudioOutput> create(std::string& error);
	~AudioOutput();
	AudioOutput(const AudioOutput&) = delete;
	AudioOutput& operator=(const AudioOutput&) = delete;

	bool queue(const float* samples, size_t count, std::string& error);
	bool resume(std::string& error);
	bool pause(std::string& error);
	bool clear(std::string& error);
	bool queuedSamples(size_t& count, std::string& error) const;

private:
	explicit AudioOutput(SDL_AudioStream* stream) : stream_(stream) {}
	SDL_AudioStream* stream_;
};
