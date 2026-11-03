#include "audio.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

bool AudioOutput::configurePrivateCapture(const std::string& path, std::string& error) {
  if (path.empty() || !std::filesystem::path(path).is_absolute() ||
      SDL_WasInit(SDL_INIT_AUDIO) || std::filesystem::exists(path) ||
      std::filesystem::exists(path + ".spec.jsonl")) {
    error = "private audio capture requires a new absolute file before audio initialization";
    return false;
  }
  // SDL's disk backend consumes the physical final mix without opening a host
  // audio device. Override inherited dummy/coreaudio settings before any open.
  // At 48 kHz, 960 frames is exactly 20 ms in disk's integer-ms device clock.
  if (!SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "disk", SDL_HINT_OVERRIDE) ||
      !SDL_SetHintWithPriority(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, path.c_str(), SDL_HINT_OVERRIDE) ||
      !SDL_SetHintWithPriority(SDL_HINT_AUDIO_DISK_TIMESCALE, "1.0", SDL_HINT_OVERRIDE) ||
      !SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "960", SDL_HINT_OVERRIDE)) {
    error = SDL_GetError();
    return false;
  }
  error.clear();
  return true;
}

std::unique_ptr<AudioOutput> AudioOutput::create(std::string& error) {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    error = SDL_GetError();
    return nullptr;
  }
  const char* requestedDriver = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
  const char* actualDriver = SDL_GetCurrentAudioDriver();
  if (requestedDriver && std::strcmp(requestedDriver, "disk") == 0 &&
      (!actualDriver || std::strcmp(actualDriver, "disk") != 0)) {
    error = "private audio capture did not select disk backend";
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return nullptr;
  }
  const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
  SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!stream) {
    error = SDL_GetError();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return nullptr;
  }
  if (actualDriver && std::strcmp(actualDriver, "disk") == 0) {
    const char* destination = SDL_GetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE);
    SDL_AudioSpec physical{};
    int frames = 0;
    if (!destination || !*destination ||
        !SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &physical, &frames)) {
      error = "private audio capture format is unavailable";
      SDL_DestroyAudioStream(stream);
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      return nullptr;
    }
    // Durable machine-readable format; do not reconstruct PCM layout from a
    // human log that may rotate during a long Run. Every opened logical output
    // must agree, otherwise the capture reader refuses the mixed-format file.
    std::ofstream metadata(std::string(destination) + ".spec.jsonl", std::ios::app);
    metadata << "{\"schema\":1,\"driver\":\"disk\",\"encoding\":\""
             << (physical.format == SDL_AUDIO_F32LE ? "f32le" : physical.format == SDL_AUDIO_S16LE ? "s16le" : "unsupported")
             << "\",\"channels\":" << physical.channels << ",\"rate\":" << physical.freq
             << ",\"sample_frames\":" << frames << "}\n";
    metadata.close();
    if (!metadata) {
      error = "cannot retain private audio capture format";
      SDL_DestroyAudioStream(stream);
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      return nullptr;
    }
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
  SDL_AudioSpec spec{};
  int frames = 0;
  if (device && SDL_GetAudioDeviceFormat(device, &spec, &frames)) {
    result += " format=" + std::string(SDL_GetAudioFormatName(spec.format));
    result += " channels=" + std::to_string(spec.channels);
    result += " hz=" + std::to_string(spec.freq) + " frames=" + std::to_string(frames);
  }
  return result;
}
