// Real-device sound-effects exercise. Unlike sfx_unit this does not force
// SDL_AUDIODRIVER=dummy: it skips when only the dummy backend or no playback
// device exists, and otherwise corroborates on the actual output device the
// stock-backed behaviors the unit test already pins: overlapping voices, the
// per-resource instance arbitration (00550980 sorted admission, state 1 to 0
// drop), named-voice updates, targeted stop, loop refresh teardown and full
// voice-table drain after transitions and repeats.
#include "rpk.hpp"
#include "sfx.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#define CHECK(x)                                                \
  do {                                                          \
    if (!(x)) {                                                 \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
      std::abort();                                             \
    }                                                           \
  } while (0)

namespace {
bool waitDrained(SoundEffects& effects, std::chrono::milliseconds limit) {
  const auto end = std::chrono::steady_clock::now() + limit;
  for (;;) {
    if (effects.voiceCount() == 0)
      return true;
    if (std::chrono::steady_clock::now() >= end)
      return false;
    SDL_Delay(20);
  }
}
} // namespace

int main() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    std::printf("sfx device test: skip: audio init failed: %s\n",
                SDL_GetError());
    return 0;
  }
  const char* driver = SDL_GetCurrentAudioDriver();
  if (!driver || std::string(driver) == "dummy") {
    std::printf("sfx device test: skip: no real audio backend (driver=%s)\n",
                driver ? driver : "none");
    return 0;
  }
  SDL_AudioSpec deviceSpec{};
  int deviceCount = 0;
  SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&deviceCount);
  const char* deviceName = devices && deviceCount > 0
                               ? SDL_GetAudioDeviceName(devices[0])
                               : nullptr;
  const bool haveDevice =
      SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &deviceSpec,
                               nullptr) &&
      deviceName;
  CHECK(haveDevice);
  std::printf("sfx device test: driver '%s' device '%s' %d Hz %d channels\n",
              driver, deviceName, deviceSpec.freq, deviceSpec.channels);
  SDL_free(devices);

  std::ifstream file("ref/raw/sound.rpk", std::ios::binary);
  CHECK(file);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  const auto pack = rpkParse(bytes.data(), bytes.size());
  CHECK(pack.ok);
  // Real mounted type-6 sound entries, exactly the resources the Free Ride
  // startup precaches (0x1b..0x24 in the live runs).
  std::vector<const RpkEntry*> entries;
  for (const auto& e : pack.pack.entries) {
    if (e.resType != 6 || e.payloadSize == 0)
      continue;
    const uint32_t id = ((e.superId & 0xFFFFu) << 16) | e.typeId;
    bool seen = false;
    for (const auto* picked : entries) {
      const uint32_t pickedId =
          ((picked->superId & 0xFFFFu) << 16) | picked->typeId;
      if (pickedId == id)
        seen = true;
    }
    if (!seen)
      entries.push_back(&e);
  }
  CHECK(!entries.empty());
  std::string error;
  auto effects = SoundEffects::create(error);
  CHECK(effects);
  std::vector<std::unique_ptr<SoundSample>> samples, loopCandidates;
  std::vector<uint32_t> ids, loopIds;
  for (const auto* entry : entries) {
    auto sample = loadSoundSample(bytes, *entry, "ref/raw", error);
    if (!sample)
      continue;
    if (samples.size() < 4 && loopCandidates.empty()) {
      ids.push_back(entry->superId);
      samples.push_back(std::move(sample));
    } else {
      loopIds.push_back(entry->superId);
      loopCandidates.push_back(std::move(sample));
    }
  }
  CHECK(!samples.empty());
  CHECK(!loopCandidates.empty());
  // Keep the resident-voice counts stable during the assertions: the
  // arbitration resource is the longest remaining sample.
  size_t longest = 0;
  for (size_t i = 1; i < loopCandidates.size(); ++i)
    if (loopCandidates[i]->pcm.size() > loopCandidates[longest]->pcm.size())
      longest = i;
  std::swap(loopCandidates[0], loopCandidates[longest]);
  std::swap(loopIds[0], loopIds[longest]);
  // The loop case uses a different sound entry than the overlap voices.
  const std::unique_ptr<SoundSample>* loopSample = nullptr;
  uint32_t loopResource = 0;
  for (size_t i = 1; i < loopCandidates.size(); ++i) {
    if (loopCandidates[i]->instances >= 1) {
      loopSample = &loopCandidates[i];
      loopResource = loopIds[i];
      break;
    }
  }

  // Overlapping voices: every mounted entry starts its own audible voice on
  // the real device and stays resident while playing.
  for (size_t i = 0; i < samples.size(); ++i)
    CHECK(effects->play(ids[i], 0, *samples[i], 0.5f, 1.0f, error, nullptr));
  CHECK(effects->voiceCount() == samples.size());
  // The effects-volume control reaches the device path: volume 0.25 maps to a
  // lower quantized DirectSound attenuation level than 0.5 (0061b700), and a
  // request that rounds to silence is refused instead of playing inaudibly.
  SoundSample quiet = *samples[0];
  CHECK(!effects->play(0x7a120, 0, quiet, 0.012f, 1.0f, error));
  CHECK(error.find("volume rounds to silence") != std::string::npos);

  // Per-resource arbitration on a fresh resource with no resident voice:
  // stock 00550980 admits at most min(instances, 8) voices per sound and
  // zeroes the excess requests (state 1 to 0, no error, nplay still id'd).
  const int cap = std::min(loopCandidates[0]->instances, 8);
  CHECK(cap >= 1);
  const uint32_t capResource = loopIds[0];
  int started = 0, dropped = 0;
  for (int i = 0; i < 10; ++i) {
    SoundEffects::Outcome outcome = SoundEffects::Outcome::Started;
    CHECK(effects->play(capResource, i + 1, *loopCandidates[0], 0.5f, 1.0f,
                        error, &outcome));
    (outcome == SoundEffects::Outcome::Started ? started : dropped)++;
  }
  CHECK(started == cap);
  CHECK(dropped == 10 - cap);
  // Targeted stop: one named voice of the resource disappears; everything
  // else survives. Transitions leave no obsolete voices behind.
  effects->stop(capResource, 1);
  const size_t expectedAfterStop =
      static_cast<size_t>(samples.size()) + static_cast<size_t>(cap) - 1;
  CHECK(effects->voiceCount() == expectedAfterStop);

  // Loop refresh: the request that creates a looping voice counts as this
  // sound tick's refresh; if the following tick has no request, the obsolete
  // voice is released (port contract for repeated effects/transitions).
  CHECK(loopSample);
  CHECK(effects->play(loopResource, 9, **loopSample, 0.5f, 1.0f, error,
                      nullptr, true));
  CHECK(effects->tick(error));
  CHECK(effects->voiceCount() == expectedAfterStop + 1); // fresh loop lives
  CHECK(effects->tick(error));
  CHECK(effects->voiceCount() == expectedAfterStop); // stale loop released
  // A loop re-requested every tick (named update path) stays alive.
  CHECK(effects->play(loopResource, 9, **loopSample, 0.5f, 1.0f, error,
                      nullptr, true));
  CHECK(effects->tick(error));
  CHECK(effects->play(loopResource, 9, **loopSample, 0.5f, 1.0f, error,
                      nullptr, true));
  CHECK(effects->tick(error));
  CHECK(effects->voiceCount() == expectedAfterStop + 1);
  effects->stop(loopResource, 9);
  CHECK(effects->voiceCount() == expectedAfterStop);

  // Repeated non-looping effects must eventually drain to zero voices on the
  // real device: nothing is retained after the samples finish.
  CHECK(waitDrained(*effects, std::chrono::seconds(30)));
  CHECK(effects->tick(error));
  CHECK(effects->voiceCount() == 0);

  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  std::printf("sfx device test: ok\n");
  return 0;
}
