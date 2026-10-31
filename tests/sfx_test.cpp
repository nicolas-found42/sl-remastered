#include "rpk.hpp"
#include "sfx.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
#include <fstream>
#include <iterator>
#include <vector>

int main() {
  SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1);
  std::ifstream file("ref/raw/sound.rpk", std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  const auto pack = rpkParse(bytes.data(), bytes.size());
  CHECK(pack.ok);
  const RpkEntry *entry = nullptr;
  for (const auto &e : pack.pack.entries)
    if (e.typeId == 0x1d)
      entry = &e;
  CHECK(entry);
  std::string error;
  auto sample = loadSoundSample(bytes, *entry, "ref/raw", error);
  CHECK(sample);
  auto effects = SoundEffects::create(error);
  CHECK(effects);
  // 00550560/0055bb20: source distance is clamped to the resource
  // min/max, then multiplied by inverse distance and linear range fade.
  SoundEffects::Listener listener{{0, 0, 0}, {0, 0, 1}, {0, 1, 0}};
  SoundSample spatialSample = *sample;
  spatialSample.flags = 0;
  spatialSample.minDistance = 5;
  spatialSample.maxDistance = 100;
  SoundEffects::SpatialMix mix;
  CHECK(SoundEffects::spatialMix(spatialSample, 1, {0, 0, 10}, listener, mix,
                                 error));
  CHECK(!mix.dropped && std::abs(mix.volume - 90.0f / 190.0f) < 1e-5f);
  CHECK(mix.left == 1 && mix.right == 1);
  CHECK(SoundEffects::spatialMix(spatialSample, 1, {1, 0, 10}, listener, mix,
                                 error));
  CHECK(!mix.dropped && mix.left < 1 && mix.right == 1);
  CHECK(SoundEffects::spatialMix(spatialSample, 1, {-1, 0, 10}, listener, mix,
                                 error));
  CHECK(!mix.dropped && mix.right < 1 && mix.left == 1);
  // 0055be07 forwards the Y float's raw bits when the pan index is outside
  // the table; zero is accepted and ordinary nonzero Y leaves the old pan.
  CHECK(SoundEffects::spatialMix(spatialSample, 1, {10, 0, 0}, listener, mix,
                                 error));
  CHECK(!mix.panUnchanged && mix.left == 1 && mix.right == 1);
  CHECK(SoundEffects::spatialMix(spatialSample, 1, {10, 1, 0}, listener, mix,
                                 error));
  CHECK(mix.panUnchanged);
  CHECK(SoundEffects::spatialMix(spatialSample, 1, {251, 0, 0}, listener, mix,
                                 error));
  CHECK(mix.dropped);
  SoundEffects::Outcome outcome{};
  {
    SoundSample mono;
    mono.spec = {SDL_AUDIO_F32, 1, 48000};
    mono.pcm.resize(4800 * sizeof(float));
    mono.instances = 1;
    mono.minDistance = 5;
    mono.maxDistance = 100;
    CHECK(effects->playPositional(33, 1, mono, 1, 1, {1, 0, 10}, listener,
                                  error, &outcome));
    CHECK(outcome == SoundEffects::Outcome::Started);
    CHECK(effects->playPositional(33, 1, mono, 1, 1, {-1, 0, 10}, listener,
                                  error, &outcome));
    CHECK(outcome == SoundEffects::Outcome::Updated);
    effects->stop(33, 1);
  }
  {
    auto queued = std::make_shared<SoundSample>();
    queued->spec = {SDL_AUDIO_F32, 1, 48000};
    queued->pcm.resize(4800 * sizeof(float));
    queued->instances = 1;
    queued->minDistance = 5;
    queued->maxDistance = 100;
    auto lifetime = std::make_shared<int>(1);
    std::vector<int> draws;
    auto liveListener = [&](SoundEffects::Listener &out, std::string &) {
      out = listener;
      return true;
    };
    CHECK(effects->queuePositional(
        34, 1, queued, 1, {0, 0, 10}, listener, liveListener,
        [&] {
          draws.push_back(1);
          return 0u;
        },
        lifetime, error));
    CHECK(effects->queuePositional(
        34, 2, queued, 1, {0, 0, 5}, listener, liveListener,
        [&] {
          draws.push_back(2);
          return 0u;
        },
        lifetime, error));
    queued.reset(); // The request retains the sample.
    CHECK(draws.empty());
    CHECK(effects->tick(error));
    CHECK(draws.size() == 1 && draws[0] == 2);
    CHECK(effects->voiceCount() == 1);
    effects->stop(34, 2);
  }
  {
    auto queued = std::make_shared<SoundSample>();
    queued->spec = {SDL_AUDIO_F32, 1, 48000};
    queued->pcm.resize(4800 * sizeof(float));
    queued->instances = 1;
    auto lifetime = std::make_shared<int>(1);
    int draws = 0;
    auto liveListener = [&](SoundEffects::Listener &out, std::string &) {
      out = listener;
      return true;
    };
    CHECK(effects->queuePositional(
        35, 1, queued, 1, {0, 0, 5}, listener, liveListener,
        [&] {
          ++draws;
          return 0u;
        },
        lifetime, error));
    lifetime.reset();
    CHECK(effects->tick(error));
    CHECK(draws == 0 && effects->voiceCount() == 0);
  }
  CHECK(effects->play(29, 10, *sample, 1.0f, 1.0f, error, &outcome) &&
        outcome == SoundEffects::Outcome::Started);
  CHECK(effects->play(29, 11, *sample, 0.5f, 1.0f, error, &outcome) &&
        outcome == SoundEffects::Outcome::Started);
  CHECK(effects->voiceCount() == 2);
  // Stock state 3 (FUN_0055c120): a named voice that is playing gets new volume
  // and pitch, not a second voice.
  CHECK(effects->play(29, 10, *sample, 0.5f, 1.5f, error, &outcome) &&
        outcome == SoundEffects::Outcome::Updated);
  CHECK(effects->voiceCount() == 2);
  // Stock drops requests beyond the per-sound instance limit (FUN_00550980
  // state 1 to 0) without an error.
  SoundSample one = *sample;
  one.instances = 1;
  CHECK(effects->play(30, 0, one, 1.0f, 1.0f, error, &outcome) &&
        outcome == SoundEffects::Outcome::Started);
  CHECK(effects->play(30, 0, one, 1.0f, 1.0f, error, &outcome) &&
        outcome == SoundEffects::Outcome::Dropped);
  CHECK(effects->play(30, 5, one, 1.0f, 1.0f, error, &outcome) &&
        outcome == SoundEffects::Outcome::Dropped);
  CHECK(effects->voiceCount() == 3);
  one.instances = 0;
  CHECK(effects->play(31, 0, one, 1.0f, 1.0f, error, &outcome) &&
        outcome == SoundEffects::Outcome::Dropped);
  CHECK(effects->voiceCount() == 3);
  effects->stop(30, 0);
  CHECK(effects->voiceCount() == 2);
  effects->stop(29, 10);
  CHECK(effects->voiceCount() == 1);
  effects->stop(30, 11);
  CHECK(effects->voiceCount() == 1);
  effects->stop(29, 11);
  CHECK(effects->voiceCount() == 0);
  CHECK(!effects->play(29, 12, *sample, 1.0f, 0.0f, error));
  // Queued PCM is owned by SDL, not by the resource that supplied it. Once a
  // short voice drains, its stream is destroyed and its instance slot becomes
  // available.
  {
    SoundSample temporary;
    temporary.spec = {SDL_AUDIO_F32, 1, 48000};
    temporary.pcm.resize(4800 * sizeof(float));
    temporary.instances = 1;
    CHECK(effects->play(32, 1, temporary, 1.0f, 1.0f, error, &outcome));
    CHECK(outcome == SoundEffects::Outcome::Started);
    temporary.pcm.clear();
  }
  CHECK(effects->voiceCount() == 1);
  const Uint64 deadline = SDL_GetTicks() + 3000;
  while (effects->voiceCount() && SDL_GetTicks() < deadline)
    SDL_Delay(10);
  CHECK(effects->voiceCount() == 0);
  SoundSample shortSample;
  shortSample.spec = {SDL_AUDIO_F32, 1, 48000};
  shortSample.pcm.resize(4800 * sizeof(float));
  shortSample.instances = 1;
  CHECK(effects->play(32, 2, shortSample, 1.0f, 1.0f, error, &outcome));
  CHECK(outcome == SoundEffects::Outcome::Started);
  effects->stop(32, 2);
  shortSample.pcm.push_back(0);
  CHECK(!effects->play(32, 3, shortSample, 1.0f, 1.0f, error, &outcome));
  CHECK(!error.empty() && effects->voiceCount() == 0);
  // Stock loop bit 1 keeps a voice only while its named request is refreshed.
  shortSample.pcm.pop_back();
  CHECK(effects->play(32, 4, shortSample, 1.0f, 1.0f, error, &outcome, true));
  CHECK(outcome == SoundEffects::Outcome::Started);
  CHECK(effects->tick(error));
  SDL_Delay(
      350); // Several sample lengths: the callback must continue feeding PCM.
  CHECK(effects->voiceCount() == 1);
  CHECK(effects->play(32, 4, shortSample, 0.5f, 1.2f, error, &outcome, true));
  CHECK(outcome == SoundEffects::Outcome::Updated && effects->tick(error));
  CHECK(effects->voiceCount() == 1);
  CHECK(effects->tick(error) && effects->voiceCount() == 0);
  CHECK(effects->play(32, 4, shortSample, 1.0f, 1.0f, error, &outcome, true));
  CHECK(outcome == SoundEffects::Outcome::Started);
  CHECK(!effects->play(32, 4, shortSample, 1.0f, 1.0f, error, &outcome));
  effects->stop(32, 4);
  CHECK(effects->voiceCount() == 0);
}
