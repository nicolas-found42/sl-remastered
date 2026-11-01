#include "audio.hpp"

#include <SDL3/SDL.h>

#include <cmath>

std::unique_ptr<AudioOutput> AudioOutput::create(std::string& error) {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    error = SDL_GetError();
    return nullptr;
  }
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
  if (count > maxQueuedSamples - queued) {
    error = "audio queue is full";
    return false;
  }
  if (count && !SDL_PutAudioStreamData(stream_, samples, static_cast<int>(count * sizeof(float)))) {
    error = SDL_GetError();
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    const float s = samples[i];
    meterSq_ += static_cast<double>(s) * s;
    if (s > meterPeak_)
      meterPeak_ = s;
    else if (-s > meterPeak_)
      meterPeak_ = -s;
  }
  meterFrames_ += count / 2;
  error.clear();
  return true;
}

bool AudioOutput::resume(std::string& error) {
  if (!SDL_ResumeAudioStreamDevice(stream_)) {
    error = SDL_GetError();
    return false;
  }
  error.clear();
  return true;
}

bool AudioOutput::pause(std::string& error) {
  if (!SDL_PauseAudioStreamDevice(stream_)) {
    error = SDL_GetError();
    return false;
  }
  error.clear();
  return true;
}

bool AudioOutput::clear(std::string& error) {
  if (!SDL_ClearAudioStream(stream_)) {
    error = SDL_GetError();
    return false;
  }
  error.clear();
  return true;
}

bool AudioOutput::deviceName(std::string& name, std::string& error) const {
  const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(stream_);
  if (!device) {
    error = SDL_GetError();
    return false;
  }
  const char* text = SDL_GetAudioDeviceName(device);
  if (!text) {
    error = SDL_GetError();
    return false;
  }
  name = text;
  error.clear();
  return true;
}

bool AudioOutput::queuedSamples(size_t& count, std::string& error) const {
  const int bytes = SDL_GetAudioStreamQueued(stream_);
  if (bytes < 0) {
    error = SDL_GetError();
    return false;
  }
  count = static_cast<size_t>(bytes) / sizeof(float);
  error.clear();
  return true;
}

bool AudioOutput::meter(double& rms, float& peak, size_t& frames,
                        std::string& error) {
  if (meterFrames_ == 0) {
    rms = 0.0;
    peak = 0.0f;
    frames = 0;
  } else {
    rms = std::sqrt(meterSq_ / static_cast<double>(meterFrames_ * 2));
    peak = meterPeak_;
    frames = meterFrames_;
  }
  meterSq_ = 0.0;
  meterPeak_ = 0.0f;
  meterFrames_ = 0;
  error.clear();
  return true;
}

std::string AudioOutput::describeDevice() const {
  const char* driver = SDL_GetCurrentAudioDriver();
  std::string result = driver && *driver ? driver : "unknown";
  const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(stream_);
  const char* name = device != 0 ? SDL_GetAudioDeviceName(device) : nullptr;
  if (name && *name) {
    result += " device '";
    result += name;
    result += "'";
  }
  return result;
}
