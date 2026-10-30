#pragma once
// Run log: a per-run directory with run.log, meta.txt, result.txt and (on a signal) crash.txt, plus a
// fixed-size ring of recent events. The hook functions at the top are header-only on purpose: vm.cpp and the
// natives are compiled straight into many unit tests that do not link the recorder, and they must stay no-ops
// there.
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace runlog {

// Persisted kinds also append a line to run.log. The others are too frequent and stay in the ring, which is
// dumped on exit with a failure and on a crash.
enum class EventKind : uint8_t {
  Input, Sound, Drop, Resource, Note, Error, Heartbeat, FrameSummary,
  NativeCall, CursorEvent, Thread, ScriptEnter,
  Count
};

// rank orders kinds by how likely they explain a failure: diagnose.py shows the newest events above kRoutineRank first.
struct KindInfo { const char* name; bool persist; uint8_t rank; };

constexpr uint8_t kRoutineRank = 3;  // Thread and everything below it is routine noise

inline constexpr KindInfo kKindTable[] = {
    {"input", true, 8},       {"sound", true, 7},       {"drop", true, 9},       {"resource", true, 6},
    {"note", true, 5},        {"error", true, 10},      {"heartbeat", true, 4},  {"frame", true, 4},
    {"nativecall", false, 1}, {"cursor", false, 2},     {"thread", false, 3},    {"scriptenter", false, 2},
};
static_assert(sizeof(kKindTable) / sizeof(kKindTable[0]) == static_cast<size_t>(EventKind::Count),
              "every EventKind needs a table row");

inline const KindInfo& kindInfo(EventKind kind) { return kKindTable[static_cast<size_t>(kind)]; }

constexpr size_t kKindCount = static_cast<size_t>(EventKind::Count);

// Full-run counters. The ring forgets old events; these do not.
enum class Counter : uint8_t { SoundStarted, SoundUpdated, SoundDropped, InputRecorded, Count };
inline constexpr const char* kCounterNames[] = {"sound_started", "sound_updated", "sound_dropped", "input_recorded"};
static_assert(sizeof(kCounterNames) / sizeof(kCounterNames[0]) == static_cast<size_t>(Counter::Count),
              "every Counter needs a name");
inline std::atomic<uint64_t> g_kindCounts[kKindCount];
inline std::atomic<uint64_t> g_counters[static_cast<size_t>(Counter::Count)];
inline void count(Counter c) { g_counters[static_cast<size_t>(c)].fetch_add(1, std::memory_order_relaxed); }

constexpr size_t kRingCapacity = 4096;
constexpr size_t kEventTextSize = 152;
constexpr size_t kDumpEvents = 4096;

struct Event {
  double t;
  uint32_t frame;
  uint8_t kind;
  char text[kEventTextSize];
};

// Writers claim a slot with an atomic increment and never allocate. A reader (including a signal handler) can
// see a slot that another thread is half way through writing. That costs at most one garbled line.
class EventRing {
public:
  void push(double t, uint32_t frame, EventKind kind, const char* text) {
    Event& slot = slots_[next_.fetch_add(1, std::memory_order_relaxed) & (kRingCapacity - 1)];
    slot.t = t;
    slot.frame = frame;
    slot.kind = static_cast<uint8_t>(kind);
    size_t i = 0;
    for (; text && text[i] && i + 1 < kEventTextSize; ++i) {
      const unsigned char c = static_cast<unsigned char>(text[i]);
      slot.text[i] = (c < 0x20 || c == 0x7f) ? ' ' : static_cast<char>(c);
    }
    slot.text[i] = 0;
    slot.text[kEventTextSize - 1] = 0;
  }
  uint64_t written() const { return next_.load(std::memory_order_relaxed); }
  size_t size() const {
    const uint64_t n = written();
    return n < kRingCapacity ? static_cast<size_t>(n) : kRingCapacity;
  }
  // Index 0 is the oldest retained event.
  const Event& at(size_t i) const {
    const uint64_t n = written();
    const uint64_t first = n < kRingCapacity ? 0 : n - kRingCapacity;
    return slots_[(first + i) & (kRingCapacity - 1)];
  }
  std::vector<Event> last(size_t count) const {
    const size_t have = size(), take = count < have ? count : have;
    std::vector<Event> out;
    out.reserve(take);
    for (size_t i = have - take; i < have; ++i) out.push_back(at(i));
    return out;
  }
  void clear() { next_.store(0, std::memory_order_relaxed); }

private:
  Event slots_[kRingCapacity];
  std::atomic<uint64_t> next_{0};
};

inline double monotonicSeconds() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

using PersistFn = void (*)(void* context, const Event& event);

struct Hub {
  EventRing ring;
  double start = 0;
  PersistFn persist = nullptr;
  void* context = nullptr;
  void (*checkpoint)(void* context) = nullptr;
  std::mutex ringMutex;
  std::vector<Event> snapshot(size_t count, uint64_t& written) {
    std::lock_guard<std::mutex> lock(ringMutex);
    written = ring.written();
    return ring.last(count);
  }
  void emit(EventKind kind, const char* text);
};

inline std::atomic<Hub*> g_hub{nullptr};
inline std::atomic<uint32_t> g_frame{0};

inline bool active() { return g_hub.load(std::memory_order_acquire) != nullptr; }
inline void setFrame(uint32_t frame) { g_frame.store(frame, std::memory_order_relaxed); }

inline void Hub::emit(EventKind kind, const char* text) {
  const double t = monotonicSeconds() - start;
  const uint32_t frame = g_frame.load(std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(ringMutex);
    // A run of random draws otherwise displaces the calls that led to it.
    const bool randomDraw = kind == EventKind::NativeCall && text &&
        std::strncmp(text, "Math.random()F", 14) == 0;
    const bool repeated = randomDraw && ring.size() &&
        ring.at(ring.size() - 1).kind == static_cast<uint8_t>(kind) &&
        std::strncmp(ring.at(ring.size() - 1).text, "Math.random()F", 14) == 0;
    if (!repeated) ring.push(t, frame, kind, text);
  }
  g_kindCounts[static_cast<size_t>(kind)].fetch_add(1, std::memory_order_relaxed);
  if (persist && kindInfo(kind).persist) {
    Event copy;
    copy.t = t;
    copy.frame = frame;
    copy.kind = static_cast<uint8_t>(kind);
    std::snprintf(copy.text, sizeof copy.text, "%s", text ? text : "");
    persist(context, copy);
  }
}

// Text is already formatted; use when the caller built the line itself.
inline void eventText(EventKind kind, const char* text) {
  if (Hub* hub = g_hub.load(std::memory_order_acquire)) hub->emit(kind, text);
}

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
inline void event(EventKind kind, const char* fmt, ...) {
  Hub* hub = g_hub.load(std::memory_order_acquire);
  if (!hub) return;
  char text[kEventTextSize];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(text, sizeof text, fmt, args);
  va_end(args);
  hub->emit(kind, text);
}

// "[+SS.mmm fFRAME] tag text"
std::string formatLine(double t, uint32_t frame, const char* tag, const char* text);

// counters.txt body (key=value lines), formatted without snprintf or allocation so a signal handler can use it.
// Returns the length written (cut off at cap - 1) and always NUL-terminates.
size_t formatCounters(char* buf, size_t cap);

// Real input recording to input.txt in the run directory, in the --script format. `t` is seconds since the frame
// loop started. They do nothing when no run directory is open. Every record is written with its own write(2) so a
// crash keeps everything up to the last event.
std::string formatInputLine(double t, const char* verb, const char* a = nullptr, const char* b = nullptr);
void inputMotion(double t, float x, float y);  // window points; coalesced to one record per 10 ms
void inputButton(double t, bool down);         // left button
void inputKey(double t, const char* scancodeName, bool down);
void inputUnsupportedButton(double t, int sdlButton);  // noted once per button, not replayable
void inputFlush(double t);                              // writes a held-back motion once 10 ms have passed

struct RunOptions {
  std::string baseDir;  // empty: $SLRR_RUN_DIR, else local/runs
  std::vector<std::string> argv;
  std::string gameRoot, dataRoot, scriptPath;
  std::string sourceRoot;  // empty: source root embedded by the compiler
  bool captureStreams = true;
  bool installHandlers = true;
};

class RunRecorder {
public:
  // Creates the run directory and everything in it. Returns null and sets `error` if the directory cannot be made.
  static std::unique_ptr<RunRecorder> start(const RunOptions& options, std::string& error);
  ~RunRecorder();
  RunRecorder(const RunRecorder&) = delete;
  RunRecorder& operator=(const RunRecorder&) = delete;

  const std::string& dir() const;
  // Writes result.txt and restores the original stdout and stderr. Safe to call twice. Returns exitCode.
  int finish(int exitCode);
  // The last "startup probe:" line with the indented lines that followed it.
  std::vector<std::string> reasonLines() const;

private:
  RunRecorder();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Call once per frame. Appends a heartbeat every 5 seconds and a frame line each second below 20 fps.
void frameTick(uint32_t frame);

}  // namespace runlog
