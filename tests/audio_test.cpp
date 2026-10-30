#include "audio.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <vector>

int main() {
	if (!SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy")) return 1;
	std::string error;
	for (int iteration = 0; iteration < 2; ++iteration) {
		auto audio = AudioOutput::create(error);
		if (!audio) { std::fprintf(stderr, "audio: %s\n", error.c_str()); return 1; }
		std::vector<float> samples(AudioOutput::maxQueuedSamples, 0.125f);
		size_t queued = 0;
		if (!audio->queue(samples.data(), samples.size(), error) ||
			!audio->queuedSamples(queued, error) || queued != samples.size()) return 1;
		if (audio->queue(samples.data(), 2, error) || audio->queue(samples.data(), 1, error) ||
			audio->queue(nullptr, 2, error) || audio->queue(samples.data(), samples.size() + 2, error)) return 1;
		if (!audio->queuedSamples(queued, error) || queued != samples.size()) return 1;
		if (!audio->resume(error)) return 1;
		const Uint64 end = SDL_GetTicks() + 2000;
		do {
			SDL_Delay(10);
			if (!audio->queuedSamples(queued, error)) return 1;
		} while (queued == samples.size() && SDL_GetTicks() < end);
		if (queued >= samples.size() || !audio->pause(error) || !audio->clear(error) ||
			!audio->queuedSamples(queued, error) || queued != 0) return 1;
	}
	std::puts("audio queue test: ok");
	return 0;
}
