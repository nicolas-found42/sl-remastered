// Object random sound scheduling and lifecycle (src/object_sound.hpp).
// Covers arm/disarm/teardown ordering against the LinkOwner contract: the
// runtime destructor detaches its timer links, so destroying an armed runtime
// without disarm expires its timers instead of dangling.
#include "object_sound.hpp"
#include "timers.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<ObjectSoundState>);
static_assert(!std::is_copy_assignable_v<ObjectSoundState>);
static_assert(!std::is_move_constructible_v<ObjectSoundState>);
static_assert(!std::is_move_assignable_v<ObjectSoundState>);

inline int failures = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);                      \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

namespace {
struct Fixture {
  TimerState timers;
  ObjectSoundState::Config config;
  ObjectSoundState *state = nullptr;
  float clock = 0;
  uint32_t seed = 1;

  Fixture() {
    config.windowClock = [&] { return clock; };
    config.randomState = &seed;
    state = new ObjectSoundState(timers, std::move(config));
  }
  ~Fixture() { delete state; }

  ObjectSoundRuntime::Entry entry(int32_t minC, int32_t maxC, float base,
                                  float variance, bool positional = false,
                                  uint32_t resource = 0x11) {
    ObjectSoundRuntime::Entry e;
    e.resourceId = resource;
    e.clockMin = minC;
    e.clockMax = maxC;
    e.baseDelay = base;
    e.delayVariance = variance;
    e.positional = positional;
    return e;
  }
};

// Delivers every expired timer through ObjectSoundState.
bool advance(ObjectSoundState &state, TimerState &timers, float now,
             std::string &error) {
  return timers.advance(
      now,
      [&](LinkOwner &target, int32_t, uint32_t event, int32_t param,
          std::string &err) {
        return state.deliver(target, event, param, err);
      },
      error);
}
} // namespace

int main() {
  std::string error;

  // Arming is never immediate: with zero variance the first deadline is
  // strictly in the future, and no timer exists when randomSound is false.
  {
    Fixture f;
    ObjectSoundRuntime runtime;
    runtime.owner.value = 1;
    runtime.randomSound = false;
    runtime.entries.push_back(f.entry(0, 100, 5.0f, 0.0f));
    CHECK(f.state->arm(runtime, error) && error.empty());
    CHECK(f.timers.size() == 0);
    runtime.randomSound = true;
    CHECK(f.state->arm(runtime, error));
    CHECK(f.timers.size() == 1);
    // Deadline must be now + base, not now (stock never arms at time zero
    // delay on this path).
    CHECK(f.timers.advance(
        4.9f,
        [](LinkOwner &, int32_t, uint32_t, int32_t, std::string &) {
          return true;
        },
        error));
    CHECK(f.timers.size() == 1);
    f.state->disarm(runtime);
    bool delivered = false;
    CHECK(f.timers.advance(
        10.0f,
        [&](LinkOwner &, int32_t, uint32_t, int32_t, std::string &) {
          delivered = true;
          return true;
        },
        error));
    CHECK(!delivered);
  }

  // Delivery always reschedules: one timer before and after, deadline pushed
  // forward, and a non-positional entry never touches playback (null host).
  {
    Fixture f;
    f.clock = 50; // inside [0, 100)
    ObjectSoundRuntime runtime;
    runtime.owner.value = 1;
    runtime.randomSound = true;
    runtime.entries.push_back(f.entry(0, 100, 2.0f, 0.0f));
    CHECK(f.state->arm(runtime, error));
    CHECK(advance(*f.state, f.timers, 5.0f, error) && error.empty());
    CHECK(f.timers.size() == 1);
    CHECK(f.timers.advance(
        6.9f,
        [](LinkOwner &, int32_t, uint32_t, int32_t, std::string &) {
          return true;
        },
        error));
    CHECK(f.timers.size() == 1); // rescheduled at ~7.0, not yet due
  }
  // Each newly scheduled entry gets its own stock resource demand.
  {
    Fixture f;
    ObjectSoundRuntime runtime;
    runtime.owner.value = 1;
    runtime.randomSound = true;
    runtime.entries.push_back(f.entry(0, 100, 2.0f, 0.0f));
    runtime.entries.push_back(f.entry(0, 100, 2.0f, 0.0f));
    std::vector<size_t> requests;
    runtime.requestSound = [&](size_t index, std::string &) {
      requests.push_back(index);
      return true;
    };
    CHECK(f.state->arm(runtime, error));
    CHECK(requests.size() == 1);
    size_t delivered = 2;
    CHECK(f.timers.advance(
        3.0f,
        [&](LinkOwner &target, int32_t, uint32_t event, int32_t param,
            std::string &err) {
          delivered = event >> 16;
          return f.state->deliver(target, event, param, err);
        },
        error));
    CHECK(requests.size() == 2 && requests[0] == delivered && requests[1] < 2);
  }
  {
    Fixture f;
    ObjectSoundRuntime runtime;
    runtime.owner.value = 1;
    runtime.randomSound = true;
    runtime.entries.push_back(f.entry(0, 100, 2.0f, 0.0f));
    runtime.requestSound = [](size_t, std::string &err) {
      err = "resource demand failed";
      return false;
    };
    CHECK(!f.state->arm(runtime, error));
    bool delivered = false;
    CHECK(f.timers.advance(
        10.0f,
        [&](LinkOwner &, int32_t, uint32_t, int32_t, std::string &) {
          delivered = true;
          return true;
        },
        error));
    CHECK(!delivered);
  }

  // Window gating: out-of-window delivery skips playback but still
  // reschedules (positional entry with no playback host would error in-window
  // only).
  {
    Fixture f;
    f.clock = 500; // outside [0, 100)
    ObjectSoundRuntime runtime;
    runtime.owner.value = 1;
    runtime.randomSound = true;
    runtime.entries.push_back(f.entry(0, 100, 1.0f, 0.0f, true));
    CHECK(f.state->arm(runtime, error));
    // Out of window: no play attempted, so the missing playback host is not
    // an error; the fallback delay reschedules the timer.
    CHECK(advance(*f.state, f.timers, 5.0f, error) && error.empty());
    CHECK(f.timers.size() == 1);
  }

  // Delivery consumes the two selection draws even when positional playback
  // fails. The stock code computes delay and next index before attempting it.
  {
    Fixture f;
    ObjectSoundRuntime runtime;
    runtime.owner.value = 1;
    runtime.randomSound = true;
    runtime.entries.push_back(f.entry(0, 100, 1.0f, 0.0f, true));
    CHECK(f.state->arm(runtime, error));
    uint32_t expected = f.seed;
    expected = expected * uint32_t{0x343fd} + uint32_t{0x269ec3};
    expected = expected * uint32_t{0x343fd} + uint32_t{0x269ec3};
    CHECK(!advance(*f.state, f.timers, 5.0f, error));
    CHECK(f.seed == expected);
  }

  // The stock out-of-window constants are 30 seconds plus up to 60.
  {
    ObjectSoundState::Config config;
    CHECK(config.fallbackDelayBase == 30.0f);
    CHECK(config.fallbackDelayVariance == 60.0f);
  }

  // Wrap-around window includes 500; positional delivery is unsupported.
  {
    Fixture f;
    f.clock = 500;
    ObjectSoundRuntime runtime;
    runtime.owner.value = 1;
    runtime.randomSound = true;
    runtime.entries.push_back(f.entry(100, 50, 1.0f, 0.0f, true));
    CHECK(f.state->arm(runtime, error));
    CHECK(!advance(*f.state, f.timers, 5.0f, error));
    CHECK(!error.empty());
  }

  // min == max matches everything (same wrap rule).
  {
    Fixture f;
    f.clock = 12345;
    ObjectSoundRuntime runtime;
    runtime.owner.value = 1;
    runtime.randomSound = true;
    runtime.entries.push_back(f.entry(60, 60, 1.0f, 0.0f, true));
    CHECK(f.state->arm(runtime, error));
    CHECK(!advance(*f.state, f.timers, 5.0f, error));
    CHECK(!error.empty());
  }

  // Teardown ordering: destroying an armed runtime WITHOUT disarm must not
  // dangle — its queued timer expires instead of delivering (LinkOwner has
  // no destructor; the runtime detaches its own links).
  {
    Fixture f;
    {
      ObjectSoundRuntime runtime;
      runtime.owner.value = 1;
      runtime.randomSound = true;
      runtime.entries.push_back(f.entry(0, 100, 50.0f, 0.0f));
      CHECK(f.state->arm(runtime, error));
      CHECK(f.timers.size() == 1);
      // Destroyed without disarm; owner.head links must be detached.
    }
    bool delivered = false;
    CHECK(f.timers.advance(
        100.0f,
        [&](LinkOwner &, int32_t, uint32_t, int32_t, std::string &) {
          delivered = true;
          return true;
        },
        error));
    CHECK(!delivered); // detached: timer expired silently
    CHECK(f.timers.size() == 0);
  }
  {
    Fixture f;
    bool attached = true;
    ObjectSoundRuntime runtime;
    runtime.owner.value = 1;
    runtime.randomSound = true;
    runtime.entries.push_back(f.entry(0, 100, 1.0f, 0.0f, true));
    runtime.alive = [&] { return attached; };
    CHECK(f.state->arm(runtime, error));
    attached = false;
    CHECK(advance(*f.state, f.timers, 5.0f, error));
    CHECK(f.timers.size() == 0);
  }

  // A missing owner identity cannot silently arm a timer.
  {
    Fixture f;
    ObjectSoundRuntime runtime;
    runtime.randomSound = true;
    runtime.entries.push_back(f.entry(0, 100, 1.0f, 0.0f));
    CHECK(!f.state->arm(runtime, error));
    CHECK(f.timers.size() == 0);
  }

  // Destroying the state first unlinks its outstanding runtime and timer.
  {
    TimerState timers;
    ObjectSoundRuntime runtime;
    runtime.owner.value = 1;
    runtime.randomSound = true;
    ObjectSoundRuntime::Entry entry;
    entry.baseDelay = 1;
    runtime.entries.push_back(entry);
    {
      ObjectSoundState state(timers, {});
      CHECK(state.arm(runtime, error));
    }
    bool delivered = false;
    CHECK(timers.advance(
        2.0f,
        [&](LinkOwner &, int32_t, uint32_t, int32_t, std::string &) {
          delivered = true;
          return true;
        },
        error));
    CHECK(!delivered);
  }

  // Unsupported sub-event and out-of-range index fail explicitly.
  {
    Fixture f;
    ObjectSoundRuntime runtime;
    runtime.owner.value = 1;
    runtime.randomSound = true;
    runtime.entries.push_back(f.entry(0, 100, 1.0f, 0.0f));
    CHECK(f.state->arm(runtime, error));
    CHECK(!f.state->deliver(runtime.owner, 3, 0, error));
    CHECK(error == "object sound sub-event unsupported");
    CHECK(!f.state->deliver(runtime.owner, (7 << 16) | 1, 0, error));
    CHECK(error == "object sound event index out of range");
    f.state->disarm(runtime);
  }

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("object_sound_test ok\n");
  return 0;
}
