#include "sfx.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace {
// DirectSound attenuation table at identified-stock 0061b700, in hundredths of
// a dB.
constexpr int attenuation[] = {
    -10000, -3610, -3010, -2660, -2410, -2210, -2060, -1920, -1810, -1700,
    -1610,  -1530, -1450, -1380, -1320, -1260, -1200, -1150, -1100, -1050,
    -1010,  -970,  -930,  -890,  -850,  -820,  -780,  -750,  -720,  -690,
    -660,   -630,  -600,  -580,  -550,  -520,  -500,  -480,  -450,  -430,
    -410,   -390,  -370,  -350,  -330,  -310,  -290,  -270,  -250,  -230,
    -210,   -200,  -180,  -160,  -150,  -130,  -120,  -100,  -90,   -70,
    -60,    -40,   -30,   -10,   0};
} // namespace

struct SoundEffects::Impl {
  struct LoopData {
    std::vector<uint8_t> pcm;
    size_t offset = 0;
    size_t frameBytes = 0;
    std::atomic<bool> failed{false};
  };
  static void SDLCALL feedLoop(void *userdata, SDL_AudioStream *stream,
                               int additional, int) {
    auto &loop = *static_cast<LoopData *>(userdata);
    if (additional <= 0 || loop.failed.load())
      return;
    size_t needed = static_cast<size_t>(additional);
    needed =
        ((needed + loop.frameBytes - 1) / loop.frameBytes) * loop.frameBytes;
    while (needed) {
      const size_t bytes = std::min(needed, loop.pcm.size() - loop.offset);
      if (!SDL_PutAudioStreamData(stream, loop.pcm.data() + loop.offset,
                                  static_cast<int>(bytes))) {
        loop.failed.store(true);
        return;
      }
      needed -= bytes;
      loop.offset = (loop.offset + bytes) % loop.pcm.size();
    }
  }
  struct SpatialData {
    std::unique_ptr<float, decltype(&SDL_free)> pcm{nullptr, SDL_free};
    size_t samples = 0;
    std::array<float, 2048> scratch{};
    size_t offset = 0;
    std::atomic<float> left{1.0f}, right{1.0f};
    std::atomic<bool> failed{false};
  };
  static void SDLCALL feedSpatial(void *userdata, SDL_AudioStream *stream,
                                  int additional, int) {
    auto &data = *static_cast<SpatialData *>(userdata);
    if (additional <= 0 || data.failed.load())
      return;
    size_t needed = (static_cast<size_t>(additional) + 7) / 8 * 2;
    while (needed && data.offset < data.samples) {
      const size_t count =
          std::min({needed, data.scratch.size(), data.samples - data.offset});
      const float left = data.left.load(), right = data.right.load();
      for (size_t i = 0; i < count; i += 2) {
        data.scratch[i] = data.pcm.get()[data.offset + i] * left;
        data.scratch[i + 1] = data.pcm.get()[data.offset + i + 1] * right;
      }
      if (!SDL_PutAudioStreamData(stream, data.scratch.data(),
                                  static_cast<int>(count * sizeof(float)))) {
        data.failed.store(true);
        return;
      }
      data.offset += count;
      needed -= count;
    }
  }
  struct Pending {
    uint32_t resource;
    int32_t id;
    std::shared_ptr<const SoundSample> sample;
    float volume;
    std::array<float, 3> position;
    std::function<bool(Listener &, std::string &)> listener;
    std::function<uint32_t()> random;
    std::weak_ptr<void> lifetime;
  };
  struct Voice {
    uint32_t resource;
    int32_t id;
    SDL_AudioStream *stream;
    int openFreq;
    size_t frames;
    std::chrono::steady_clock::time_point done;
    std::unique_ptr<LoopData> loop;
    std::unique_ptr<SpatialData> spatial;
    int slot;
    int buffer;
    int instances;
    bool refreshed;
  };
  struct PanBank {
    const SoundSample *identity = nullptr;
    std::weak_ptr<const SoundSample> sample;
    bool managed = false;
    std::array<std::array<float, 2>, 8> values{};
    PanBank() {
      for (auto &value : values)
        value = {1.0f, 1.0f};
    }
  };
  std::map<uint32_t, PanBank> panBanks;
  std::vector<Voice> voices;
  std::array<std::optional<Pending>, 64> pending;
  std::array<float, 64> scores;
  std::array<int, 64> order;
  int processingSlot = -1;
  const std::function<uint32_t()> *processingRandom = nullptr;
  std::shared_ptr<const SoundSample> processingSample;
  Impl() {
    scores.fill(10000000000.0f);
    for (int i = 0; i < 64; ++i)
      order[i] = i;
  }
  Voice *voiceAt(int slot) {
    for (auto &voice : voices)
      if (voice.slot == slot)
        return &voice;
    return nullptr;
  }
  int freeSlot(bool named) {
    int found = -1;
    for (int i = 0; i < 64; ++i) {
      if (pending[i] || voiceAt(i))
        continue;
      found = i;
      if (!named)
        break;
    }
    return found;
  }
  int freeBuffer(uint32_t resource, int instances) const {
    for (int buffer = 0; buffer < std::min(instances, 8); ++buffer) {
      bool used = false;
      for (const auto &voice : voices)
        if (voice.resource == resource && voice.buffer == buffer)
          used = true;
      if (!used)
        return buffer;
    }
    return -1;
  }
  PanBank &panBank(uint32_t resource, const SoundSample &sample) {
    auto &bank = panBanks[resource];
    if (bank.identity != &sample || (bank.managed && bank.sample.expired()))
      bank = PanBank{};
    bank.identity = &sample;
    if (processingSample && processingSample.get() == &sample) {
      bank.sample = processingSample;
      bank.managed = true;
    }
    return bank;
  }
  // FUN_005d765d and 005d77b1 sort the persistent 64-slot index array.
  void sortRange(int lo, int hi) {
    if (lo >= hi)
      return;
    if (hi - lo + 1 <= 8) {
      for (int end = hi; end > lo; --end) {
        int largest = lo;
        for (int j = lo + 1; j <= end; ++j)
          if (scores[order[j]] > scores[order[largest]])
            largest = j;
        std::swap(order[largest], order[end]);
      }
      return;
    }
    std::swap(order[lo], order[lo + (hi - lo + 1) / 2]);
    int left = lo, right = hi + 1;
    for (;;) {
      do {
        ++left;
      } while (left <= hi && scores[order[left]] <= scores[order[lo]]);
      do {
        --right;
      } while (right > lo && scores[order[right]] >= scores[order[lo]]);
      if (left > right)
        break;
      std::swap(order[left], order[right]);
    }
    std::swap(order[lo], order[right]);
    sortRange(lo, right - 1);
    sortRange(left, hi);
  }
  ~Impl() {
    for (const auto &voice : voices)
      SDL_DestroyAudioStream(voice.stream);
  }
  void prune() {
    const auto now = std::chrono::steady_clock::now();
    voices.erase(std::remove_if(voices.begin(), voices.end(),
                                [&](const Voice &v) {
                                  if (v.loop || now < v.done ||
                                      SDL_GetAudioStreamQueued(v.stream) != 0)
                                    return false;
                                  if (!pending[v.slot])
                                    scores[v.slot] = 10000000000.0f;
                                  SDL_DestroyAudioStream(v.stream);
                                  return true;
                                }),
                 voices.end());
  }
  static std::chrono::steady_clock::time_point deadline(size_t frames,
                                                        int frequency) {
    const double seconds = static_cast<double>(frames) / frequency;
    return std::chrono::steady_clock::now() +
           std::chrono::duration_cast<std::chrono::steady_clock::duration>(
               std::chrono::duration<double>(seconds + 0.25));
  }
};
SoundEffects::SoundEffects() : impl_(std::make_unique<Impl>()) {}
SoundEffects::~SoundEffects() {
  impl_.reset();
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
std::unique_ptr<SoundEffects> SoundEffects::create(std::string &error) {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    error = SDL_GetError();
    return {};
  }
  error.clear();
  return std::unique_ptr<SoundEffects>(new SoundEffects());
}
static bool spatialMixImpl(const SoundSample &sample, float volume,
                           const std::array<float, 3> &position,
                           const SoundEffects::Listener &listener,
                           SoundEffects::SpatialMix &mix, bool requestCutoff,
                           std::string &error) {
  mix = {};
  error.clear();
  if (!std::isfinite(volume) || !std::isfinite(sample.minDistance) ||
      !std::isfinite(sample.maxDistance)) {
    error = "sfx: invalid spatial parameters";
    return false;
  }
  for (size_t i = 0; i < 3; ++i) {
    if (!std::isfinite(position[i]) || !std::isfinite(listener.position[i]) ||
        !std::isfinite(listener.forward[i]) || !std::isfinite(listener.up[i])) {
      error = "sfx: invalid listener or source position";
      return false;
    }
  }
  const float x = position[0] - listener.position[0];
  const float y = position[1] - listener.position[1];
  const float z = position[2] - listener.position[2];
  const float distance2 = x * x + y * y + z * z;
  // 00550560 and 005505e0 reject sources beyond 250 units, even
  // when the sound resource itself is marked non-spatial.
  if ((requestCutoff && distance2 > 62500.0f) || volume <= 0.01f) {
    mix.dropped = true;
    return true;
  }
  if ((sample.flags & 1) != 0) {
    mix.volume = std::min(volume, 1.0f);
    mix.dropped = static_cast<int>(mix.volume * 64.0f) == 0;
    return true;
  }
  const float minDistance = std::max(sample.minDistance, 0.001f);
  const float maxDistance = sample.maxDistance;
  if (!(maxDistance > 0.0f)) {
    error = "sfx: unsupported spatial distance range";
    return false;
  }
  float distance = std::sqrt(distance2);
  distance = std::max(distance, minDistance);
  distance = std::min(distance, maxDistance);
  const float range = maxDistance - minDistance;
  const float falloff = range > 0.0f ? (maxDistance - distance) / range : 1.0f;
  const float scale = std::min(1.0f, falloff * minDistance / distance);
  mix.volume = std::min(volume * scale, 1.0f);
  mix.dropped = static_cast<int>(mix.volume * 64.0f) == 0;
  const auto &f = listener.forward;
  const auto &u = listener.up;
  // 0055b260 constructs the right axis as up cross forward.
  const float rx = f[2] * u[1] - f[1] * u[2];
  const float ry = f[0] * u[2] - f[2] * u[0];
  const float rz = f[1] * u[0] - f[0] * u[1];
  const float front = f[0] * x + f[1] * y + f[2] * z;
  const float side = rx * x + ry * y + rz * z;
  const float vertical = u[0] * x + u[1] * y + u[2] * z;
  const float elevation =
      std::atan2(vertical, std::sqrt(front * front + side * side));
  const float angle = std::atan2(side, std::abs(front));
  const int pan =
      static_cast<int>((1.0 - std::abs(static_cast<double>(elevation))) *
                       static_cast<double>(angle) * 64.0);
  int panDb;
  if (pan != 0 && pan >= -64 && pan <= 64) {
    const int attenuationDb = attenuation[64 - std::abs(pan)];
    panDb = pan < 0 ? attenuationDb : -attenuationDb;
  } else {
    // 0055be07 passes the listener-relative Y float's raw bits to SetPan
    // for zero or out-of-table indices. DirectSound rejects values outside
    // [-10000, 10000], leaving the old buffer pan untouched.
    std::memcpy(&panDb, &y, sizeof(panDb));
    if (panDb < -10000 || panDb > 10000) {
      mix.panUnchanged = true;
      return true;
    }
  }
  const float quiet =
      std::pow(10.0f, -static_cast<float>(std::abs(panDb)) / 2000.0f);
  if (panDb < 0)
    mix.right = quiet;
  if (panDb > 0)
    mix.left = quiet;
  return true;
}

bool SoundEffects::spatialMix(const SoundSample &sample, float volume,
                              const std::array<float, 3> &position,
                              const Listener &listener, SpatialMix &mix,
                              std::string &error) {
  return spatialMixImpl(sample, volume, position, listener, mix, true, error);
}

bool SoundEffects::queuePositional(
    uint32_t resourceId, int32_t id, std::shared_ptr<const SoundSample> sample,
    float volume, const std::array<float, 3> &position,
    const Listener &listener,
    std::function<bool(Listener &, std::string &)> liveListener,
    std::function<uint32_t()> random, std::weak_ptr<void> lifetime,
    std::string &error) {
  error.clear();
  if (!resourceId || !sample || !liveListener || !random ||
      lifetime.expired() || !std::isfinite(volume)) {
    error = "sfx: invalid deferred positional request";
    return false;
  }
  float distance2 = 0.0f;
  for (size_t i = 0; i < 3; ++i) {
    if (!std::isfinite(position[i]) || !std::isfinite(listener.position[i])) {
      error = "sfx: invalid deferred position";
      return false;
    }
    const float d = position[i] - listener.position[i];
    distance2 += d * d;
  }
  // 00550560/005505e0 reject before allocating a request slot.
  if (distance2 > 62500.0f || volume <= 0.01f)
    return true;
  impl_->prune();
  int slot = -1;
  if (id) {
    for (int i = 0; i < 64; ++i) {
      const auto &p = impl_->pending[i];
      auto *voice = impl_->voiceAt(i);
      if ((p && p->resource == resourceId && p->id == id) ||
          (voice && voice->resource == resourceId && voice->id == id)) {
        slot = i;
        break;
      }
    }
  }
  if (slot < 0)
    slot = impl_->freeSlot(id != 0);
  if (slot < 0)
    return true; // Stock request table full.
  impl_->scores[slot] = distance2 / (volume * volume);
  impl_->pending[slot] =
      Impl::Pending{resourceId,        id,
                    std::move(sample), volume,
                    position,          std::move(liveListener),
                    std::move(random), std::move(lifetime)};
  return true;
}

bool SoundEffects::playPositional(uint32_t resourceId, int32_t id,
                                  const SoundSample &sample, float volume,
                                  float pitch,
                                  const std::array<float, 3> &position,
                                  const Listener &listener, std::string &error,
                                  Outcome *outcome) {
  SpatialMix mix;
  if (!spatialMixImpl(sample, volume, position, listener, mix,
                      impl_->processingSlot < 0, error))
    return false;
  auto report = [&](Outcome value) {
    if (outcome)
      *outcome = value;
    error.clear();
    return true;
  };
  if (mix.dropped) {
    // The stock backend sets pan before noticing a volume level of zero.
    if (impl_->processingSlot >= 0 && !mix.panUnchanged) {
      impl_->prune();
      const auto *voice = impl_->voiceAt(impl_->processingSlot);
      const int buffer = voice
                             ? voice->buffer
                             : impl_->freeBuffer(resourceId, sample.instances);
      if (buffer >= 0)
        impl_->panBank(resourceId, sample).values[buffer] = {mix.left,
                                                             mix.right};
    }
    return report(Outcome::Dropped);
  }
  if (std::isfinite(pitch) && pitch <= 0.0f && !impl_->processingRandom) {
    error = "sfx: deferred random pitch at sound tick unsupported";
    return false;
  }
  const int frameBytes =
      SDL_AUDIO_BYTESIZE(sample.spec.format) * sample.spec.channels;
  if (!resourceId || sample.spec.channels < 1 ||
      sample.spec.channels > ((sample.flags & 1) ? 2 : 1) ||
      sample.spec.freq <= 0 || frameBytes <= 0 || sample.pcm.empty() ||
      sample.pcm.size() % static_cast<size_t>(frameBytes) ||
      sample.pcm.size() >
          static_cast<size_t>(std::numeric_limits<int>::max()) ||
      !std::isfinite(pitch) || (pitch <= 0.0f && !impl_->processingRandom)) {
    error = "sfx: invalid mono positional sample or pitch";
    return false;
  }
  auto nextFrequency = [&] {
    const double requested =
        pitch > 0.0f
            ? static_cast<double>(sample.spec.freq) * pitch
            : static_cast<double>(sample.spec.freq) +
                  static_cast<int>((*impl_->processingRandom)() & 0xfff) - 2048;
    return static_cast<int>(std::clamp(requested, 100.0, 200000.0));
  };
  const float gain = std::pow(
      10.0f,
      static_cast<float>(attenuation[static_cast<int>(mix.volume * 64.0f)]) /
          2000.0f);
  impl_->prune();
  if (id) {
    for (auto &voice : impl_->voices) {
      if (voice.resource != resourceId || voice.id != id)
        continue;
      if (!voice.spatial) {
        error = "sfx: changing active voice spatial mode is unsupported";
        return false;
      }
      const int frequency = nextFrequency();
      if (!SDL_SetAudioStreamGain(voice.stream, gain) ||
          !SDL_SetAudioStreamFrequencyRatio(
              voice.stream, static_cast<float>(frequency) / voice.openFreq)) {
        error = SDL_GetError();
        if (outcome)
          *outcome = Outcome::Dropped;
        return true;
      }
      if (!mix.panUnchanged) {
        voice.spatial->left.store(mix.left);
        voice.spatial->right.store(mix.right);
        impl_->panBank(resourceId, sample).values[voice.buffer] = {mix.left,
                                                                   mix.right};
      }
      if (impl_->processingSlot < 0) {
        const float x = position[0] - listener.position[0];
        const float y = position[1] - listener.position[1];
        const float z = position[2] - listener.position[2];
        impl_->scores[voice.slot] = (x * x + y * y + z * z) / (volume * volume);
      }
      voice.done = Impl::deadline(voice.frames, frequency);
      return report(Outcome::Updated);
    }
  }
  if (impl_->voices.size() >= 64)
    return report(Outcome::Dropped);
  const auto count = std::count_if(
      impl_->voices.begin(), impl_->voices.end(),
      [&](const Impl::Voice &v) { return v.resource == resourceId; });
  if (sample.instances <= 0 || count >= std::min(sample.instances, 8))
    return report(Outcome::Dropped);
  const int slot = impl_->processingSlot >= 0 ? impl_->processingSlot
                                              : impl_->freeSlot(id != 0);
  if (slot < 0)
    return report(Outcome::Dropped);
  const int buffer = impl_->freeBuffer(resourceId, sample.instances);
  if (buffer < 0)
    return report(Outcome::Dropped);
  auto &pan = impl_->panBank(resourceId, sample).values[buffer];
  const auto gains =
      mix.panUnchanged ? pan : std::array<float, 2>{mix.left, mix.right};
  SDL_AudioSpec stereo{SDL_AUDIO_F32, 2, sample.spec.freq};
  Uint8 *converted = nullptr;
  int length = 0;
  if (!SDL_ConvertAudioSamples(&sample.spec, sample.pcm.data(),
                               static_cast<int>(sample.pcm.size()), &stereo,
                               &converted, &length)) {
    error = SDL_GetError();
    return false;
  }
  if (!converted || length <= 0 || length % (2 * sizeof(float)) != 0) {
    SDL_free(converted);
    error = "sfx: invalid converted positional sample";
    return false;
  }
  auto data = std::make_unique<Impl::SpatialData>();
  data->pcm.reset(reinterpret_cast<float *>(converted));
  data->samples = static_cast<size_t>(length) / sizeof(float);
  data->left.store(gains[0]);
  data->right.store(gains[1]);
  SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &stereo, nullptr, nullptr);
  if (!stream) {
    error = SDL_GetError();
    if (outcome)
      *outcome = Outcome::Dropped;
    return true;
  }
  const int frequency = nextFrequency();
  if (!SDL_SetAudioStreamGain(stream, gain) ||
      !SDL_SetAudioStreamFrequencyRatio(stream, static_cast<float>(frequency) /
                                                    stereo.freq) ||
      !SDL_SetAudioStreamGetCallback(stream, Impl::feedSpatial, data.get()) ||
      !SDL_ResumeAudioStreamDevice(stream)) {
    error = SDL_GetError();
    SDL_DestroyAudioStream(stream);
    if (outcome)
      *outcome = Outcome::Dropped;
    return true;
  }
  if (!mix.panUnchanged)
    pan = gains;
  if (impl_->processingSlot < 0) {
    const float x = position[0] - listener.position[0];
    const float y = position[1] - listener.position[1];
    const float z = position[2] - listener.position[2];
    impl_->scores[slot] = (x * x + y * y + z * z) / (volume * volume);
  }
  const size_t frames = data->samples / 2;
  impl_->voices.push_back({resourceId,
                           id,
                           stream,
                           stereo.freq,
                           frames,
                           Impl::deadline(frames, frequency),
                           {},
                           std::move(data),
                           slot,
                           buffer,
                           sample.instances,
                           true});
  return report(Outcome::Started);
}
bool SoundEffects::play(uint32_t resourceId, int32_t id,
                        const SoundSample &sample, float volume, float pitch,
                        std::string &error, Outcome *outcome, bool loop) {
  auto report = [&](Outcome value) {
    if (outcome)
      *outcome = value;
    error.clear();
    return true;
  };
  if (!resourceId || sample.pcm.empty() ||
      sample.pcm.size() >
          static_cast<size_t>(std::numeric_limits<int>::max()) ||
      sample.spec.freq <= 0 || sample.spec.channels <= 0 ||
      !std::isfinite(volume) || !std::isfinite(pitch) || volume <= 0.01f ||
      volume > 1.0f || pitch <= 0) {
    error = "sfx: unsupported sample, volume or pitch";
    return false;
  }
  const int bytesPerFrame =
      SDL_AUDIO_BYTESIZE(sample.spec.format) * sample.spec.channels;
  if (bytesPerFrame <= 0 ||
      sample.pcm.size() % static_cast<size_t>(bytesPerFrame)) {
    error = "sfx: invalid sample format or incomplete frame";
    return false;
  }
  const int level = static_cast<int>(volume * 64.0f);
  if (!level) {
    error = "sfx: volume rounds to silence";
    return false;
  }
  const float gain =
      std::pow(10.0f, static_cast<float>(attenuation[level]) / 2000.0f);
  const double requested = static_cast<double>(sample.spec.freq) * pitch;
  const int frequency =
      static_cast<int>(std::clamp(requested, 100.0, 200000.0));
  impl_->prune();
  if (id) {
    for (auto &voice : impl_->voices)
      if (voice.resource == resourceId && voice.id == id) {
        if (voice.spatial) {
          error = "sfx: changing active voice spatial mode is unsupported";
          return false;
        }
        if (static_cast<bool>(voice.loop) != loop) {
          error = "sfx: changing loop mode of an active voice is unsupported";
          return false;
        }
        if (!SDL_SetAudioStreamGain(voice.stream, gain) ||
            !SDL_SetAudioStreamFrequencyRatio(
                voice.stream, static_cast<float>(frequency) /
                                  static_cast<float>(voice.openFreq))) {
          error = SDL_GetError();
          if (outcome)
            *outcome = Outcome::Dropped;
          return true;
        }
        voice.refreshed = true;
        voice.done = Impl::deadline(voice.frames, frequency);
        return report(Outcome::Updated);
      }
  }
  if (impl_->voices.size() >= 64)
    return report(Outcome::Dropped);
  const auto perResource = std::count_if(
      impl_->voices.begin(), impl_->voices.end(),
      [&](const Impl::Voice &v) { return v.resource == resourceId; });
  if (sample.instances <= 0 || perResource >= std::min(sample.instances, 8))
    return report(Outcome::Dropped);
  const int slot = impl_->freeSlot(id != 0);
  if (slot < 0)
    return report(Outcome::Dropped);
  const int buffer = impl_->freeBuffer(resourceId, sample.instances);
  if (buffer < 0)
    return report(Outcome::Dropped);
  SDL_AudioSpec spec = sample.spec;
  spec.freq = frequency;
  // Stock keeps going without a usable device (0055b9a0 returns -1 when
  // DAT_00784cac is 0), so a host audio failure drops the voice and reports the
  // reason through `error`.
  SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!stream) {
    error = SDL_GetError();
    if (outcome)
      *outcome = Outcome::Dropped;
    return true;
  }
  auto loopData = loop ? std::make_unique<Impl::LoopData>() : nullptr;
  if (loopData) {
    loopData->pcm = sample.pcm;
    loopData->frameBytes = static_cast<size_t>(bytesPerFrame);
  }
  if (!SDL_SetAudioStreamGain(stream, gain) ||
      (loopData
           ? !SDL_SetAudioStreamGetCallback(stream, Impl::feedLoop,
                                            loopData.get())
           : (!SDL_PutAudioStreamData(stream, sample.pcm.data(),
                                      static_cast<int>(sample.pcm.size())) ||
              !SDL_FlushAudioStream(stream))) ||
      !SDL_ResumeAudioStreamDevice(stream)) {
    error = SDL_GetError();
    SDL_DestroyAudioStream(stream);
    if (outcome)
      *outcome = Outcome::Dropped;
    return true;
  }
  impl_->panBank(resourceId, sample).values[buffer] = {1.0f, 1.0f};
  impl_->scores[slot] = 0.0f;
  const size_t frames = sample.pcm.size() / static_cast<size_t>(bytesPerFrame);
  impl_->voices.push_back({resourceId,
                           id,
                           stream,
                           spec.freq,
                           frames,
                           Impl::deadline(frames, frequency),
                           std::move(loopData),
                           {},
                           slot,
                           buffer,
                           sample.instances,
                           true});
  return report(Outcome::Started);
}
bool SoundEffects::tick(std::string &error) {
  error.clear();
  impl_->prune();
  for (int i = 0; i < 64; ++i) {
    if (impl_->pending[i] && impl_->pending[i]->lifetime.expired()) {
      impl_->pending[i].reset();
      if (!impl_->voiceAt(i))
        impl_->scores[i] = 10000000000.0f;
    }
  }
  impl_->sortRange(0, 63);
  std::array<uint32_t, 64> acceptedResources{};
  int accepted = 0;
  // FUN_00550980 first walks every sorted slot, admitting up to the device
  // and per-resource limits before it starts or updates any voice.
  for (int slot : impl_->order) {
    auto *voice = impl_->voiceAt(slot);
    auto &request = impl_->pending[slot];
    if (!voice && !request)
      continue;
    const uint32_t resource = request ? request->resource : voice->resource;
    const int instances =
        request ? request->sample->instances : voice->instances;
    int sameResource = 0;
    for (int i = 0; i < accepted; ++i)
      if (acceptedResources[i] == resource)
        ++sameResource;
    if (accepted < 64 && instances > 0 &&
        sameResource < std::min(instances, 8)) {
      acceptedResources[accepted++] = resource;
      continue;
    }
    if (voice) {
      for (auto it = impl_->voices.begin(); it != impl_->voices.end(); ++it) {
        if (it->slot != slot)
          continue;
        SDL_DestroyAudioStream(it->stream);
        impl_->voices.erase(it);
        break;
      }
    }
    request.reset();
    impl_->scores[slot] = 10000000000.0f;
  }
  bool failed = false;
  for (auto it = impl_->voices.begin(); it != impl_->voices.end();) {
    const bool spatialFailed = it->spatial && it->spatial->failed.load();
    if (spatialFailed ||
        (it->loop && (it->loop->failed.load() || !it->refreshed))) {
      failed = failed || spatialFailed || (it->loop && it->loop->failed.load());
      const int slot = it->slot;
      SDL_DestroyAudioStream(it->stream);
      it = impl_->voices.erase(it);
      if (!impl_->pending[slot])
        impl_->scores[slot] = 10000000000.0f;
    } else {
      it->refreshed = false;
      ++it;
    }
  }
  if (failed) {
    error = "sfx: SDL failed to feed a voice";
    return false;
  }
  for (int slot : impl_->order) {
    if (!impl_->pending[slot])
      continue;
    auto request = std::move(*impl_->pending[slot]);
    impl_->pending[slot].reset();
    const auto guard = request.lifetime.lock();
    if (!guard) {
      if (!impl_->voiceAt(slot))
        impl_->scores[slot] = 10000000000.0f;
      continue;
    }
    Listener listener;
    if (!request.listener(listener, error))
      return false;
    impl_->processingSlot = slot;
    impl_->processingRandom = &request.random;
    impl_->processingSample = request.sample;
    Outcome outcome{};
    const bool ok = playPositional(request.resource, request.id,
                                   *request.sample, request.volume, 0.0f,
                                   request.position, listener, error, &outcome);
    impl_->processingRandom = nullptr;
    impl_->processingSample.reset();
    impl_->processingSlot = -1;
    if (!ok)
      return false;
    if (outcome == Outcome::Dropped) {
      if (impl_->voiceAt(slot))
        stop(request.resource, request.id);
      if (!impl_->voiceAt(slot))
        impl_->scores[slot] = 10000000000.0f;
    }
  }
  impl_->prune();
  error.clear();
  return true;
}
void SoundEffects::stop(uint32_t resourceId, int32_t id) {
  for (int slot = 0; slot < 64; ++slot) {
    auto &request = impl_->pending[slot];
    if (request && request->resource == resourceId && request->id == id) {
      request.reset();
      if (!impl_->voiceAt(slot))
        impl_->scores[slot] = 10000000000.0f;
    }
  }
  for (auto it = impl_->voices.begin(); it != impl_->voices.end(); ++it) {
    if (it->resource == resourceId && it->id == id) {
      const int slot = it->slot;
      SDL_DestroyAudioStream(it->stream);
      impl_->voices.erase(it);
      if (!impl_->pending[slot])
        impl_->scores[slot] = 10000000000.0f;
      return;
    }
  }
}
size_t SoundEffects::voiceCount() const {
  impl_->prune();
  return impl_->voices.size();
}
