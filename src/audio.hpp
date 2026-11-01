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
  // "<driver> device '<name>'" of the opened playback device, for evidence.
  std::string describeDevice() const;
  // Name of the bound playback device (e.g. "MacBook Pro Speakers"), empty
  // on failure. Lets a run prove it opened real hardware, not a dummy sink.
  bool deviceName(std::string& name, std::string& error) const;
  // Windowed level of the samples handed to the real device since the last
  // call: RMS and peak as linear amplitudes plus the frame count. Proves the
  // queued signal is non-silent; it says nothing about acoustic loudness.
  bool meter(double& rms, float& peak, size_t& frames, std::string& error);

private:
  explicit AudioOutput(SDL_AudioStream* stream) : stream_(stream) {}
  SDL_AudioStream* stream_;
  double meterSq_ = 0.0; // sum of squares of queued samples
  float meterPeak_ = 0.0f;
  size_t meterFrames_ = 0; // stereo frames queued since last meter read
};
