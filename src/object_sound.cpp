#include "object_sound.hpp"

#include "timers.hpp"
#include <algorithm>

namespace {
// Stock _rand() mapping (src/natives/random.cpp mathRandom): MSVC LCG, raw
// 15-bit result; the stock object dispatch consumes (u & 0x7fff) directly and
// scales by 0x1.0002p-15f (DAT_005f0dbc family), with no Math.random clamp.
constexpr float kRandScale = 0x1.0002p-15f;
} // namespace

ObjectSoundState::ObjectSoundState(TimerState &timers, Config config)
    : timers_(timers), config_(std::move(config)) {}

ObjectSoundRuntime::~ObjectSoundRuntime() {
  if (state_)
    state_->disarm(*this);
  while (owner.head)
    setLinkOwner(*owner.head, nullptr);
}

ObjectSoundState::~ObjectSoundState() {
  while (!armed_.empty())
    disarm(*armed_.back());
}

uint32_t ObjectSoundState::randRaw() {
  uint32_t &state = config_.randomState ? *config_.randomState : privateRandom_;
  state = state * uint32_t{0x343fd} + uint32_t{0x269ec3};
  return (state >> 16) & 0x7fff;
}

float ObjectSoundState::randUnit() {
  return static_cast<float>(randRaw()) * kRandScale;
}

bool ObjectSoundState::schedule(ObjectSoundRuntime &runtime, float delay,
                                uint32_t event, std::string &error) {
  // Stock FUN_0048b750(delay, 0x80000080, event, 0): the timer id stays
  // 0x80000080 and the request word travels in the timer payload.
  if (!timers_.add(&runtime.owner, delay, ObjectSoundRuntime::kTimerId, error,
                   event, 0))
    return false;
  return !runtime.requestSound || runtime.requestSound(event >> 16, error);
}

bool ObjectSoundState::arm(ObjectSoundRuntime &runtime, std::string &error) {
  error.clear();
  // Stock object init (all.c:75935-75946): the first timer is armed only when
  // the config flag param_1[0x263] != 0, at now + rand*variance + base using
  // entry 0's delay fields with a random entry index — never at the current
  // time, so a freshly armed object is never audible immediately.
  if (!runtime.randomSound || runtime.entries.empty())
    return true;
  if (!runtime.owner.value) {
    error = "object sound owner identity unavailable";
    return false;
  }
  if (runtime.state_ && runtime.state_ != this) {
    error = "object sound already armed by another state";
    return false;
  }
  if (runtime.entries.size() > ObjectSoundRuntime::kMaxEntries) {
    error = "object random sound table exceeds stock 16-entry bound";
    return false;
  }
  if (std::find(armed_.begin(), armed_.end(), &runtime) != armed_.end())
    return true;
  const auto &first = runtime.entries.front();
  const float delay = randUnit() * first.delayVariance + first.baseDelay;
  const uint32_t index =
      randRaw() * static_cast<uint32_t>(runtime.entries.size()) >> 15;
  if (!schedule(runtime, delay,
                (index << 16) | ObjectSoundRuntime::kSubEventPlay, error)) {
    timers_.removeAll(&runtime.owner);
    return false;
  }
  armed_.push_back(&runtime);
  runtime.state_ = this;
  return true;
}

void ObjectSoundState::disarm(ObjectSoundRuntime &runtime) {
  if (runtime.state_ != this)
    return;
  timers_.removeAll(&runtime.owner);
  const auto it = std::find(armed_.begin(), armed_.end(), &runtime);
  if (it != armed_.end())
    armed_.erase(it);
  runtime.state_ = nullptr;
}

bool ObjectSoundState::playEntry(ObjectSoundRuntime &runtime,
                                 const ObjectSoundRuntime::Entry &entry,
                                 std::string &error) {
  if (!config_.effects || !config_.masterVolume || !config_.listener) {
    error = "object sound: playback host unavailable";
    return false;
  }
  const size_t index = static_cast<size_t>(&entry - runtime.entries.data());
  std::shared_ptr<const SoundSample> sample =
      runtime.sampleAt
          ? runtime.sampleAt(index)
          : (config_.resolveSample ? config_.resolveSample(entry.resourceId)
                                   : nullptr);
  if (!sample) {
    error.clear();
    return true; // Stock discards unavailable sound resources.
  }
  SoundEffects::Listener listener;
  if (!config_.listener(listener, error))
    return false;
  auto position = runtime.position;
  if (runtime.positionAt && !runtime.positionAt(position, error))
    return false;
  return config_.effects->queuePositional(
      entry.resourceId, static_cast<int32_t>(runtime.owner.value), sample,
      config_.masterVolume() * sample->volume, position, listener,
      config_.listener, [this] { return randRaw(); }, pendingLifetime_, error);
}

bool ObjectSoundState::deliver(LinkOwner &target, uint32_t event, int32_t param,
                               std::string &error) {
  error.clear();
  // The timer target is the runtime's owner link. The runtime's destructor
  // detaches remaining links, so a live owner here guarantees a live runtime.
  const auto it = std::find_if(armed_.begin(), armed_.end(),
                               [&](auto *r) { return &r->owner == &target; });
  if (it == armed_.end()) {
    error = "object sound timer target unavailable";
    return false;
  }
  ObjectSoundRuntime &runtime = **it;
  // A retained runtime may outlive a detached external world node. Its
  // physics root is already released; expire this timer without rescheduling.
  if (runtime.alive && !runtime.alive())
    return true;
  const uint32_t subEvent = event & 0xffffu;
  if (subEvent != ObjectSoundRuntime::kSubEventPlay) {
    // Stock sub-event 3 (armed by other paths, e.g. all.c:76656) is an
    // object-destruction continuation whose census body is not traced here;
    // report it instead of inventing behavior.
    error = "object sound sub-event unsupported";
    return false;
  }
  (void)param;
  // FUN_004674f0 case 0x80, sub-event 1 (all.c:76883-76919). Stock consumes
  // rand for the delay first, then rand for the next entry index, then tests
  // the window — the order matters for SLRR_FORCE_SEED determinism.
  const uint32_t index = event >> 16;
  if (index >= runtime.entries.size()) {
    error = "object sound event index out of range";
    return false;
  }
  if (!config_.windowClock) {
    error = "object sound window clock unavailable";
    return false;
  }
  const auto &entry = runtime.entries[index];
  const float clock = config_.windowClock();
  // Stock: min < max is a normal half-open range; min >= max wraps (all.c
  // 76890-76898), and min == max then matches everything.
  const bool inWindow =
      entry.clockMin < entry.clockMax
          ? (entry.clockMin <= clock && clock < entry.clockMax)
          : (clock >= entry.clockMin || clock < entry.clockMax);
  float delay = randUnit() * entry.delayVariance + entry.baseDelay;
  const uint32_t next =
      randRaw() * static_cast<uint32_t>(runtime.entries.size()) >> 15;
  if (inWindow && entry.positional && !playEntry(runtime, entry, error))
    return false;
  if (!inWindow) {
    // Out of window: stock reschedules with the shared fallback constants
    // (0x005f0ecc + rand*0x005f127c) and skips the play entirely.
    delay =
        config_.fallbackDelayBase + randUnit() * config_.fallbackDelayVariance;
  }
  return schedule(runtime, delay,
                  (next << 16) | ObjectSoundRuntime::kSubEventPlay, error);
}
