#pragma once
// Run log: a per-run directory with run.log, meta.txt, result.txt and (on a
// signal) crash.txt, plus a fixed-size ring of recent events. The hook
// functions at the top are header-only on purpose: vm.cpp and the natives are
// compiled straight into many unit tests that do not link the recorder, and
// they must stay no-ops there.
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

// Persisted kinds also append a line to run.log. The others are too frequent
// and stay in the ring, which is dumped on exit with a failure and on a crash.
enum class EventKind : uint8_t {
  Input,
  Sound,
  Drop,
  Resource,
  Note,
  Error,
  Heartbeat,
  FrameSummary,
  NativeCall,
  CursorEvent,
  Thread,
  ScriptEnter,
  Count
};

// rank orders kinds by how likely they explain a failure: diagnose.py shows the
// newest events above kRoutineRank first.
struct KindInfo {
  const char *name;
  bool persist;
  uint8_t rank;
};

constexpr uint8_t kRoutineRank =
    3; // Thread and everything below it is routine noise

inline constexpr KindInfo kKindTable[] = {
    {"input", true, 8},     {"sound", true, 7},   {"drop", true, 9},
    {"resource", true, 6},  {"note", true, 5},    {"error", true, 10},
    {"heartbeat", true, 4}, {"frame", true, 4},   {"nativecall", false, 1},
    {"cursor", false, 2},   {"thread", false, 3}, {"scriptenter", false, 2},
};
static_assert(sizeof(kKindTable) / sizeof(kKindTable[0]) ==
                  static_cast<size_t>(EventKind::Count),
              "every EventKind needs a table row");

inline const KindInfo &kindInfo(EventKind kind) {
  return kKindTable[static_cast<size_t>(kind)];
}

constexpr size_t kKindCount = static_cast<size_t>(EventKind::Count);

// Full-run counters. The ring forgets old events; these do not.
enum class Counter : uint8_t {
  SoundStarted,
  SoundUpdated,
  SoundDropped,
  InputRecorded,
  Count
};
inline constexpr const char *kCounterNames[] = {
    "sound_started", "sound_updated", "sound_dropped", "input_recorded"};
static_assert(sizeof(kCounterNames) / sizeof(kCounterNames[0]) ==
                  static_cast<size_t>(Counter::Count),
              "every Counter needs a name");
inline std::atomic<uint64_t> g_kindCounts[kKindCount];
inline std::atomic<uint64_t> g_counters[static_cast<size_t>(Counter::Count)];
inline void count(Counter c) {
  g_counters[static_cast<size_t>(c)].fetch_add(1, std::memory_order_relaxed);
}

constexpr size_t kRingCapacity = 4096;
constexpr size_t kEventTextSize = 152;
constexpr size_t kDumpEvents = 4096;

struct Event {
  double t;
  uint32_t frame;
  uint8_t kind;
  char text[kEventTextSize];
};

// Writers claim a slot with an atomic increment and never allocate. A reader
// (including a signal handler) can see a slot that another thread is half way
// through writing. That costs at most one garbled line.
template <size_t Capacity> class EventRingT {
public:
  static constexpr size_t kCapacity = Capacity;
  void push(double t, uint32_t frame, EventKind kind, const char *text) {
    Event &slot =
        slots_[next_.fetch_add(1, std::memory_order_relaxed) & (Capacity - 1)];
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
    return n < Capacity ? static_cast<size_t>(n) : Capacity;
  }
  // Index 0 is the oldest retained event.
  const Event &at(size_t i) const {
    const uint64_t n = written();
    const uint64_t first = n < Capacity ? 0 : n - Capacity;
    return slots_[(first + i) & (Capacity - 1)];
  }
  std::vector<Event> last(size_t count) const {
    const size_t have = size(), take = count < have ? count : have;
    std::vector<Event> out;
    out.reserve(take);
    for (size_t i = have - take; i < have; ++i)
      out.push_back(at(i));
    return out;
  }
  void clear() { next_.store(0, std::memory_order_relaxed); }

private:
  Event slots_[Capacity];
  std::atomic<uint64_t> next_{0};
};
using EventRing = EventRingT<kRingCapacity>;

inline std::atomic<uint32_t> g_frame{0};

// Events that explain a failure (rank >= kImportantRank) also go to a small
// ring of their own: the main ring is dominated by routine native calls and
// forgets the notes and errors long before a stalled run is stopped.
constexpr uint8_t kImportantRank = 5;
constexpr size_t kImportantCapacity = 512;
using ImportantRing = EventRingT<kImportantCapacity>;

// Gaps: every note or error that says something is unsupported, missing or
// failed, deduplicated with its count and the frame it first appeared on. This
// is the list of what stands between the run and a full free ride.
constexpr size_t kGapCapacity = 96;
struct GapEntry {
  uint64_t key = 0;
  uint64_t count = 0;
  uint32_t firstFrame = 0, lastFrame = 0;
  double firstTime = 0;
  uint8_t kind = 0;
  char text[kEventTextSize] = {};
};
struct GapTable {
  GapEntry entries[kGapCapacity];
  size_t used = 0;
  uint64_t overflow = 0;
};
inline GapTable g_gaps;

inline bool isGapText(const char *text) {
  static constexpr const char *kWords[] = {
      "unsupported", "not implemented", "unavailable", "unknown",    "failed",
      "rejected",    "cannot",          "missing",     "unresolved", "invalid",
      "not found",   "exceeded",        "lost"};
  if (!text)
    return false;
  // "world state" lists car=unavailable before a car exists, and the stall
  // note must not be matched inside "install".
  if (std::strncmp(text, "world state", 11) == 0)
    return false;
  if (std::strncmp(text, "stall:", 6) == 0)
    return true;
  for (const char *word : kWords)
    if (std::strstr(text, word))
      return true;
  return false;
}
// Digits and hex runs vary per object; the key ignores them so one gap that
// names a different id each time stays one row.
inline uint64_t gapKey(const char *text) {
  uint64_t hash = 1469598103934665603ull;
  bool inNumber = false;
  for (; *text; ++text) {
    const char c = *text;
    const bool digit = (c >= '0' && c <= '9') ||
                       (inNumber && ((c >= 'a' && c <= 'f') || c == 'x'));
    if (digit) {
      if (!inNumber)
        hash = (hash ^ '#') * 1099511628211ull;
      inNumber = true;
      continue;
    }
    inNumber = false;
    hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
  }
  return hash;
}
inline void recordGap(EventKind kind, const char *text, uint32_t frame,
                      double t) {
  const uint64_t key = gapKey(text);
  for (size_t i = 0; i < g_gaps.used; ++i)
    if (g_gaps.entries[i].key == key) {
      ++g_gaps.entries[i].count;
      g_gaps.entries[i].lastFrame = frame;
      return;
    }
  if (g_gaps.used == kGapCapacity) {
    ++g_gaps.overflow;
    return;
  }
  GapEntry &e = g_gaps.entries[g_gaps.used++];
  e.key = key;
  e.count = 1;
  e.firstFrame = e.lastFrame = frame;
  e.firstTime = t;
  e.kind = static_cast<uint8_t>(kind);
  std::snprintf(e.text, sizeof e.text, "%s", text);
}

// Named text snapshots refreshed by the frame loop (thread dump, world state).
// The crash handler copies them into crash.txt, so a run that is killed while
// stuck still says where every script thread was.
constexpr size_t kSnapshotSlots = 4;
constexpr size_t kSnapshotText = 24 * 1024;
struct SnapshotSlot {
  char name[24] = {};
  char text[kSnapshotText] = {};
  uint32_t frame = 0;
};
inline SnapshotSlot g_snapshots[kSnapshotSlots];
inline void setSnapshot(const char *name, const std::string &text) {
  SnapshotSlot *slot = nullptr;
  for (auto &s : g_snapshots)
    if (std::strcmp(s.name, name) == 0) {
      slot = &s;
      break;
    }
  if (!slot)
    for (auto &s : g_snapshots)
      if (!s.name[0]) {
        std::snprintf(s.name, sizeof s.name, "%s", name);
        slot = &s;
        break;
      }
  if (!slot)
    return;
  const size_t n =
      text.size() < kSnapshotText - 1 ? text.size() : kSnapshotText - 1;
  std::memcpy(slot->text, text.data(), n);
  slot->text[n] = 0;
  slot->frame = g_frame.load(std::memory_order_relaxed);
}

// Trace channels: a subsystem's step history as preformatted lines (one JSON
// object each). The newest kTraceCapacity lines stay in memory; the heartbeat,
// the end of the run and the crash handler write them out (<name>.jsonl and a
// section of crash.txt), so a run that panics, hangs or is killed still leaves
// the steps that led there. Writers do not allocate.
constexpr size_t kTraceChannels = 3;
constexpr size_t kTraceCapacity = 512;
// Four wheels plus eight contact-pass slots can exceed 2 KiB. Keep the
// complete step, including its anomaly/error fields, in the bounded ring.
constexpr size_t kTraceLineSize = 4096;
struct TraceChannel {
  char name[24] = {};
  char lines[kTraceCapacity][kTraceLineSize];
  std::atomic<uint64_t> next{0};
  std::atomic<bool> stream{
      false}; // also append every line to <name>-full.jsonl
  std::atomic<uint64_t> committed[kTraceCapacity]; // index + 1 once readable
  uint64_t written() const { return next.load(std::memory_order_relaxed); }
  // The line with this absolute index, or null if it was overwritten, never
  // claimed, or claimed but not committed (a writer was interrupted).
  const char *line(uint64_t index) const {
    const uint64_t n = written();
    if (index >= n || n - index > kTraceCapacity)
      return nullptr;
    const size_t slot = index & (kTraceCapacity - 1);
    return committed[slot].load(std::memory_order_acquire) == index + 1
               ? lines[slot]
               : nullptr;
  }
};
inline TraceChannel g_traces[kTraceChannels];
// Hooks installed by RunRecorder (run_log.cpp). The header-only users below
// stay linkable in unit tests that do not link the recorder; the hooks are
// null there and every call is a no-op.
inline void (*g_traceStreamHook)(const char *channel,
                                 const char *line) = nullptr;
inline void (*g_traceSaveHook)(const char *channel, const char *file) = nullptr;
inline void (*g_traceFlushHook)() = nullptr;

inline TraceChannel *traceChannel(const char *name) {
  for (auto &c : g_traces)
    if (std::strcmp(c.name, name) == 0)
      return &c;
  for (auto &c : g_traces)
    if (!c.name[0]) {
      std::snprintf(c.name, sizeof c.name, "%s", name);
      return &c;
    }
  return nullptr;
}
// Publishing is claim, fill, commit: a slot is readable only once its commit
// word equals index + 1. A signal that interrupts a writer between claim and
// commit leaves the slot uncommitted, so a dump skips it instead of printing a
// half-written line. (Readers assume the writer is the interrupted thread or
// is quiescent; the dump does not lock.)
inline uint64_t traceClaim(TraceChannel *channel) {
  const uint64_t index = channel->next.fetch_add(1, std::memory_order_relaxed);
  channel->committed[index & (kTraceCapacity - 1)].store(
      0, std::memory_order_release);
  return index;
}
inline void traceFill(TraceChannel *channel, uint64_t index, const char *line) {
  char *slot = channel->lines[index & (kTraceCapacity - 1)];
  size_t i = 0;
  for (; line[i] && i + 1 < kTraceLineSize; ++i)
    slot[i] = line[i];
  slot[i] = 0;
}
inline void traceCommit(TraceChannel *channel, uint64_t index) {
  channel->committed[index & (kTraceCapacity - 1)].store(
      index + 1, std::memory_order_release);
}
inline void tracePush(TraceChannel *channel, const char *line) {
  if (!channel || !line)
    return;
  const uint64_t index = traceClaim(channel);
  traceFill(channel, index, line);
  traceCommit(channel, index);
  if (channel->stream.load(std::memory_order_relaxed) && g_traceStreamHook)
    g_traceStreamHook(channel->name,
                      channel->lines[index & (kTraceCapacity - 1)]);
}
// printf prints a non-finite float as nan, -nan, inf or -inf, which is not
// JSON. Rewrites each such bare value (after ':', ',' or '[' and before ',',
// ']' or '}') as null. Not for signal handlers.
inline void traceSanitizeJson(char *text, size_t capacity) {
  if (!text || capacity < 2)
    return;
  char out[kTraceLineSize];
  size_t n = 0;
  const size_t limit = (capacity < sizeof out ? capacity : sizeof out) - 1;
  const size_t length = std::strlen(text);
  for (size_t i = 0; i < length && n < limit;) {
    const bool boundary = i == 0 || text[i - 1] == ':' || text[i - 1] == ',' ||
                          text[i - 1] == '[';
    if (boundary) {
      size_t j = i;
      if (text[j] == '-')
        ++j;
      if ((std::strncmp(text + j, "nan", 3) == 0 ||
           std::strncmp(text + j, "inf", 3) == 0) &&
          (text[j + 3] == ',' || text[j + 3] == ']' || text[j + 3] == '}' ||
           text[j + 3] == 0)) {
        for (const char c : {'n', 'u', 'l', 'l'})
          if (n < limit)
            out[n++] = c;
        i = j + 3;
        continue;
      }
    }
    out[n++] = text[i++];
  }
  out[n] = 0;
  std::memcpy(text, out, n + 1);
}
// Copies the channel's current ring into <file> in the run directory.
inline void traceSave(const char *channel, const char *file) {
  if (g_traceSaveHook)
    g_traceSaveHook(channel, file);
}
// Writes every channel's <name>.jsonl, gaps.txt and snapshots now.
inline void traceFlush() {
  if (g_traceFlushHook)
    g_traceFlushHook();
}

inline double monotonicSeconds() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<double>(ts.tv_sec) +
         static_cast<double>(ts.tv_nsec) * 1e-9;
}

using PersistFn = void (*)(void *context, const Event &event);

struct Hub {
  EventRing ring;
  ImportantRing important;
  double start = 0;
  PersistFn persist = nullptr;
  void *context = nullptr;
  void (*checkpoint)(void *context) = nullptr;
  std::mutex ringMutex;
  std::vector<Event> snapshot(size_t count, uint64_t &written) {
    std::lock_guard<std::mutex> lock(ringMutex);
    written = ring.written();
    return ring.last(count);
  }
  void emit(EventKind kind, const char *text);
};

inline std::atomic<Hub *> g_hub{nullptr};

inline bool active() {
  return g_hub.load(std::memory_order_acquire) != nullptr;
}
inline void setFrame(uint32_t frame) {
  g_frame.store(frame, std::memory_order_relaxed);
}

inline void Hub::emit(EventKind kind, const char *text) {
  const double t = monotonicSeconds() - start;
  const uint32_t frame = g_frame.load(std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(ringMutex);
    // A run of random draws otherwise displaces the calls that led to it.
    const bool randomDraw = kind == EventKind::NativeCall && text &&
                            std::strncmp(text, "Math.random()F", 14) == 0;
    const bool repeated =
        randomDraw && ring.size() &&
        ring.at(ring.size() - 1).kind == static_cast<uint8_t>(kind) &&
        std::strncmp(ring.at(ring.size() - 1).text, "Math.random()F", 14) == 0;
    if (!repeated)
      ring.push(t, frame, kind, text);
    const bool gap = (kind == EventKind::Note || kind == EventKind::Error ||
                      kind == EventKind::Drop) &&
                     isGapText(text);
    // Notes are only important when they are a gap or a scheduler/world
    // milestone: car and part construction alone emits hundreds.
    const bool milestone = kind == EventKind::Note && text &&
                           (std::strncmp(text, "monitor ", 8) == 0 ||
                            std::strncmp(text, "vmthread ", 9) == 0 ||
                            std::strncmp(text, "world ", 6) == 0 ||
                            std::strncmp(text, "loading ", 8) == 0);
    if (kind == EventKind::Error || kind == EventKind::Drop || gap || milestone)
      important.push(t, frame, kind, text);
    if (gap)
      recordGap(kind, text, frame, t);
  }
  g_kindCounts[static_cast<size_t>(kind)].fetch_add(1,
                                                    std::memory_order_relaxed);
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
inline void eventText(EventKind kind, const char *text) {
  if (Hub *hub = g_hub.load(std::memory_order_acquire))
    hub->emit(kind, text);
}

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
inline void event(EventKind kind, const char *fmt, ...) {
  Hub *hub = g_hub.load(std::memory_order_acquire);
  if (!hub)
    return;
  char text[kEventTextSize];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(text, sizeof text, fmt, args);
  va_end(args);
  hub->emit(kind, text);
}

// "[+SS.mmm fFRAME] tag text"
std::string formatLine(double t, uint32_t frame, const char *tag,
                       const char *text);

// counters.txt body (key=value lines), formatted without snprintf or allocation
// so a signal handler can use it. Returns the length written (cut off at cap -
// 1) and always NUL-terminates.
size_t formatCounters(char *buf, size_t cap);

// Real input recording to input.txt in the run directory, in the --script
// format. `t` is seconds since the frame loop started. They do nothing when no
// run directory is open. Every record is written with its own write(2) so a
// crash keeps everything up to the last event.
std::string formatInputLine(double t, const char *verb, const char *a = nullptr,
                            const char *b = nullptr);
void inputMotion(double t, float x,
                 float y); // window points; coalesced to one record per 10 ms
void inputButton(double t, bool down); // left button
void inputKey(double t, const char *scancodeName, bool down);
void inputUnsupportedButton(
    double t, int sdlButton); // noted once per button, not replayable
void inputFlush(double t); // writes a held-back motion once 10 ms have passed

struct RunOptions {
  std::string baseDir; // empty: $SLRR_RUN_DIR, else local/runs
  std::vector<std::string> argv;
  std::string gameRoot, dataRoot, scriptPath;
  std::string referenceRoot = "ref";
  std::string sourceRoot; // empty: source root embedded by the compiler
  bool captureStreams = true;
  bool installHandlers = true;
};

class RunRecorder {
public:
  // Creates the run directory and everything in it. Returns null and sets
  // `error` if the directory cannot be made.
  static std::unique_ptr<RunRecorder> start(const RunOptions &options,
                                            std::string &error);
  ~RunRecorder();
  RunRecorder(const RunRecorder &) = delete;
  RunRecorder &operator=(const RunRecorder &) = delete;

  const std::string &dir() const;
  // Writes result.txt and restores the original stdout and stderr. Safe to call
  // twice. Returns exitCode.
  int finish(int exitCode);
  // The last "startup probe:" line with the indented lines that followed it.
  std::vector<std::string> reasonLines() const;

private:
  RunRecorder();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Call once per frame. Appends a heartbeat every 5 seconds and a frame line
// each second below 20 fps.
void frameTick(uint32_t frame);

// Rewrites gaps.txt and one <name>.txt per snapshot in the run directory now
// (the heartbeat does it every 5 s). Use after setSnapshot when the dump must
// be on disk before the run can be killed.
void flushDiagnostics();

} // namespace runlog
