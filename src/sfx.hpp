#pragma once
#include "resource_sound.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Separate SDL streams share the default device, so effects mix with music.
class SoundEffects {
public:
  static std::unique_ptr<SoundEffects> create(std::string &error);
  ~SoundEffects();
  SoundEffects(const SoundEffects &) = delete;
  SoundEffects &operator=(const SoundEffects &) = delete;
  // Started: a new voice. Updated: a named voice that was already playing got
  // the new volume and pitch (stock request state 3, FUN_0055c120). Dropped:
  // stock discards the request without an error when the voice table (64), the
  // per-sound instance limit (FUN_0055b900, at most 8) or the device has no
  // room (FUN_00550980 state 1 to 0, FUN_005505e0 returning -1); nplay still
  // returns the id.
  enum class Outcome { Started, Updated, Dropped };
  struct Listener {
    std::array<float, 3> position{};
    std::array<float, 3> forward{};
    std::array<float, 3> up{};
  };
  struct SpatialMix {
    float volume = 0.0f;
    float left = 1.0f;
    float right = 1.0f;
    bool dropped = false;
    bool panUnchanged = false;
  };
  // Software DirectSound path at 00550560/005505e0/0055bb20.
  // False means the input or a stock backend branch cannot be represented.
  static bool spatialMix(const SoundSample &sample, float volume,
                         const std::array<float, 3> &position,
                         const Listener &listener, SpatialMix &mix,
                         std::string &error);
  bool playPositional(uint32_t resourceId, int32_t id,
                      const SoundSample &sample, float volume, float pitch,
                      const std::array<float, 3> &position,
                      const Listener &listener, std::string &error,
                      Outcome *outcome = nullptr);
  // Stock object request: copies the request and retains the sample until the
  // sound tick; the weak owner prevents calls into a destroyed random state.
  bool
  queuePositional(uint32_t resourceId, int32_t id,
                  std::shared_ptr<const SoundSample> sample, float volume,
                  const std::array<float, 3> &position,
                  const Listener &listener,
                  std::function<bool(Listener &, std::string &)> liveListener,
                  std::function<uint32_t()> random,
                  std::weak_ptr<void> lifetime, std::string &error);
  // Non-positional voices only. SDL copies one-shot PCM; loops retain their own
  // copy. A loop must be refreshed by named play requests each sound frame.
  // Unsupported requests return false; SDL device failures produce Dropped
  // with an error, while stock capacity drops have an empty error.
  bool play(uint32_t resourceId, int32_t id, const SoundSample &sample,
            float volume, float pitch, std::string &error,
            Outcome *outcome = nullptr, bool loop = false);
  // Call once after each frame's sound requests. Stock FUN_00550980 stops a
  // looping request that was not refreshed since the preceding sound tick.
  // Returns false if an SDL loop-feed operation failed asynchronously.
  bool tick(std::string &error);
  void stop(uint32_t resourceId, int32_t id);
  size_t voiceCount() const;

private:
  SoundEffects();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
