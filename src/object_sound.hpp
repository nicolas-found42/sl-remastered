#pragma once

#include "owner_links.hpp"
#include "resource_sound.hpp"
#include "sfx.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class TimerState;
class ObjectSoundState;

// Stock object random sound scheduling and delivery (native timer id
// 0x80000080). Stock behavior, from local/census/all.c:
//  - FUN_00465410 parses up to 16 random-sound entries ("%d %f %f %f %f %f")
//    per object config into 0x24-byte slots (object field 0xbc4 is the count).
//  - Object init arms one timer, gated on the config flag (param_1[0x263]):
//    FUN_0048b750(now + rand*variance + base, 0x80000080,
//                 (randIndex << 16) | 1, 0) — never at deadline zero.
//  - FUN_004674f0 case 0x80, sub-event 1 (all.c:76883-76919): consumes rand
//    for the delay, then rand for the next entry index, then tests the window
//    clock (DAT_00640988) against the entry's [clockMin, clockMax): a normal
//    range needs clockMin <= clock < clockMax; min >= max wraps (clock >= min
//    OR clock < max, so min == max matches everything). In-window plays the
//    entry when its flag (+0x263) is set and reschedules with the entry
//    delay; out-of-window reschedules with the shared fallback constants
//    (0x005f0ecc + rand*0x005f127c) and skips the play. The timer always
//    reschedules with a fresh random entry index.
// Untraced stock behavior, reported honestly instead of approximated:
//  - Sub-event 3 (armed by other stock paths, e.g. all.c:76656) is an
//    object-destruction continuation, not a fade loop; it is rejected with an
//    explicit unsupported error here.
// Timers target the runtime owner through OwnerLink. Runtime and state teardown
// unlink the timer and registry in either destruction order.
class ObjectSoundRuntime {
public:
  static constexpr int32_t kTimerId = static_cast<int32_t>(0x80000080u);
  static constexpr uint32_t kSubEventPlay = 1;
  static constexpr size_t kMaxEntries = 16; // stock object field 0xbc4 bound

  struct Entry {
    uint32_t resourceId = 0; // stock table +0x265 (FUN_00544590 name hash)
    int32_t clockMin = 0;    // stock table +0x266, window clock seconds
    int32_t clockMax = 0;    // stock table +0x267
    float baseDelay = 0;     // stock table +0x268
    float delayVariance = 0; // stock table +0x269
    bool positional = false; // stock table +0x263 (gates the play)
  };

  // Stock object field 0x263: config declared random sound. ObjectParse sets
  // this from the object config; arming is a silent no-op when false, exactly
  // as stock skips FUN_0048b750 when param_1[0x263] == 0.
  bool randomSound = false;
  // Used when no live root physics body exists.
  std::array<float, 3> position{};
  // Callbacks are owned by the enclosing object runtime and refer only to its
  // stable resource links and physics registration.
  std::function<bool()> alive;
  std::function<bool(size_t, std::string &)> requestSound;
  std::function<std::shared_ptr<const SoundSample>(size_t)> sampleAt;
  std::function<bool(std::array<float, 3> &, std::string &)> positionAt;
  // Timer delivery target; must be stable while armed. LinkOwner has no
  // destructor (src/owner_links.hpp), so this destructor detaches every link
  // still attached to owner; queued timers then expire instead of dangling.
  // disarm() is still required for early removal and registry consistency.
  ObjectSoundRuntime() = default;
  ObjectSoundRuntime(const ObjectSoundRuntime &) = delete;
  ObjectSoundRuntime &operator=(const ObjectSoundRuntime &) = delete;
  ~ObjectSoundRuntime();
  LinkOwner owner;
  std::vector<Entry> entries;

private:
  friend class ObjectSoundState;
  ObjectSoundState *state_ = nullptr;
};

class ObjectSoundState {
public:
  struct Config {
    // Stock DAT_00640988 (game clock seconds mod 86400). Required for play
    // sub-events; without it delivery fails explicitly instead of guessing.
    std::function<float()> windowClock;
    // Host sound resource table. Returning null is a stock-style drop: the
    // request is discarded silently, delivery still succeeds.
    std::function<std::shared_ptr<const SoundSample>(uint32_t)> resolveSample;
    SoundEffects *effects = nullptr;
    std::function<bool(SoundEffects::Listener &, std::string &)> listener;
    // Stock master effect volume DAT_00612c58 as set through Sound.setVolume.
    std::function<float()> masterVolume;
    // Stock constants at 005f0ecc and 005f127c (30 and 60 seconds).
    float fallbackDelayBase = 30.0f;
    float fallbackDelayVariance = 60.0f;
    // MSVC rand state shared with the VM (src/natives/random.cpp LCG), so
    // SLRR_FORCE_SEED stock determinism covers object sound scheduling. Null
    // uses a private state.
    uint32_t *randomState = nullptr;
  };

  ObjectSoundState(TimerState &timers, Config config);
  ~ObjectSoundState();
  ObjectSoundState(const ObjectSoundState &) = delete;
  ObjectSoundState &operator=(const ObjectSoundState &) = delete;
  ObjectSoundState(ObjectSoundState &&) = delete;
  ObjectSoundState &operator=(ObjectSoundState &&) = delete;

  // Stock object init arming: schedules the first 0x80000080 timer at
  // now + rand*entries[0].delayVariance + entries[0].baseDelay with a random
  // entry index in the event high word. Never arms at the current time and
  // never arms when randomSound is false or the table is empty.
  bool arm(ObjectSoundRuntime &runtime, std::string &error);
  // REQUIRED before destroying an armed runtime (LinkOwner has no
  // destructor); removes the runtime's queued timers.
  void disarm(ObjectSoundRuntime &runtime);

  // dispatchTimerEvent (src/natives/game_type.cpp) routes timer id
  // 0x80000080 here with the preserved event word.
  bool deliver(LinkOwner &target, uint32_t event, int32_t param,
               std::string &error);

private:
  uint32_t randRaw(); // stock _rand() & 0x7fff over the MSVC LCG
  float randUnit();   // (raw) * 0x1.0002p-15f, same scale as Math.random
  bool playEntry(ObjectSoundRuntime &runtime, const ObjectSoundRuntime::Entry &,
                 std::string &error);
  bool schedule(ObjectSoundRuntime &runtime, float delay, uint32_t event,
                std::string &error);

  TimerState &timers_;
  Config config_;
  uint32_t privateRandom_ = 1;
  std::shared_ptr<void> pendingLifetime_ = std::make_shared<int>(0);
  // Non-owning; arm/disarm bracket the runtime's lifetime.
  std::vector<ObjectSoundRuntime *> armed_;
};
