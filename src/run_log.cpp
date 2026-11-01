#include "run_log.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <SDL3/SDL_version.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <exception>
#include <execinfo.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <libproc.h>
#include <mach-o/dyld.h>
#include <mutex>
#include <poll.h>
#include <pthread.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace runlog {

namespace {

Hub &theHub() {
  static Hub hub;
  return hub;
}

bool writeAll(int fd, const char *data, size_t size) {
  while (size) {
    const ssize_t n = ::write(fd, data, size);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    data += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}

// Everything the signal handler needs, filled in before any signal can arrive.
// The handler may only read it and call async-signal-safe functions.
struct CrashState {
  char crashPath[1024] = {};
  char resultPath[1024] = {};
  char countersPath[1024] = {};
  char diagDir[1024] = {};
  double start = 0;
  std::atomic<bool> handling{false};
  std::atomic<bool> dumped{false};
  std::atomic<bool> installed{false};
  char note[256] = {};
};
CrashState g_crash;

constexpr int kFatalSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
constexpr int kStopSignals[] = {SIGTERM, SIGINT};

const char *signalName(int sig) {
  switch (sig) {
  case SIGSEGV:
    return "SIGSEGV";
  case SIGBUS:
    return "SIGBUS";
  case SIGILL:
    return "SIGILL";
  case SIGFPE:
    return "SIGFPE";
  case SIGABRT:
    return "SIGABRT";
  case SIGTERM:
    return "SIGTERM";
  case SIGINT:
    return "SIGINT";
  default:
    return "SIGNAL";
  }
}

// Output buffer that formats numbers by hand: snprintf is not
// async-signal-safe.
struct Out {
  int fd;
  char buf[1024];
  size_t n = 0;
  char *sink = nullptr; // fd < 0: copy into memory instead of writing, used by
                        // formatCounters
  size_t sinkCap = 0, sinkLen = 0;
  explicit Out(int f) : fd(f) {}
  Out(char *mem, size_t cap) : fd(-1), sink(mem), sinkCap(cap) {}
  void flush() {
    if (n && fd >= 0)
      writeAll(fd, buf, n);
    else if (n && sink)
      for (size_t i = 0; i < n && sinkLen + 1 < sinkCap; ++i)
        sink[sinkLen++] = buf[i];
    n = 0;
  }
  void ch(char c) {
    if (n == sizeof buf)
      flush();
    buf[n++] = c;
  }
  void str(const char *s) {
    for (; s && *s; ++s)
      ch(*s);
  }
  void strn(const char *s, size_t max) {
    for (size_t i = 0; i < max && s[i]; ++i)
      ch(s[i]);
  }
  void u64(uint64_t v) {
    char tmp[24];
    size_t len = 0;
    do {
      tmp[len++] = static_cast<char>('0' + v % 10);
      v /= 10;
    } while (v);
    while (len)
      ch(tmp[--len]);
  }
  void padded(uint64_t v, int width) {
    char tmp[24];
    int len = 0;
    do {
      tmp[len++] = static_cast<char>('0' + v % 10);
      v /= 10;
    } while (v);
    for (int i = len; i < width; ++i)
      ch('0');
    while (len)
      ch(tmp[--len]);
  }
  void hex(uint64_t v) {
    str("0x");
    bool started = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
      const unsigned digit = static_cast<unsigned>((v >> shift) & 0xf);
      if (digit || started || shift == 0) {
        ch("0123456789abcdef"[digit]);
        started = true;
      }
    }
  }
  void seconds3(double t) {
    if (t < 0)
      t = 0;
    const uint64_t ms = static_cast<uint64_t>(t * 1000.0 + 0.5);
    padded(ms / 1000, 2);
    ch('.');
    padded(ms % 1000, 3);
  }
  void eventLine(const Event &e) {
    str("[+");
    seconds3(e.t);
    str(" f");
    u64(e.frame);
    str("] ");
    str(e.kind < static_cast<uint8_t>(EventKind::Count)
            ? kKindTable[e.kind].name
            : "?");
    ch(' ');
    strn(e.text, kEventTextSize - 1);
    ch('\n');
  }
};

void dumpRecentEvents(Out &out, size_t count) {
  const EventRing &ring = theHub().ring;
  const size_t have = ring.size(), take = count < have ? count : have;
  for (size_t i = have - take; i < have; ++i)
    out.eventLine(ring.at(i));
}

void writeCountersFile(const char *path) {
  if (!path[0])
    return;
  const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return;
  static char text[2048]; // preallocated: the crash handler must not allocate
  const size_t len = formatCounters(text, sizeof text);
  writeAll(fd, text, len);
  ::close(fd);
}

void writeResultAsync(int sig, const char *status, const char *reason) {
  const int fd = ::open(g_crash.resultPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return;
  Out out(fd);
  out.str("status=");
  out.str(status);
  out.str("\nexit_code=");
  out.u64(static_cast<uint64_t>(128 + sig));
  out.str("\nsignal=");
  out.str(signalName(sig));
  out.str("\nseconds=");
  out.seconds3(monotonicSeconds() - g_crash.start);
  out.str("\nframes=");
  out.u64(g_frame.load(std::memory_order_relaxed));
  out.str("\nreason=");
  out.str(reason);
  out.ch('\n');
  out.flush();
  ::close(fd);
}

void dumpImportantEvents(Out &out) {
  const ImportantRing &ring = theHub().important;
  const size_t have = ring.size();
  for (size_t i = 0; i < have; ++i)
    out.eventLine(ring.at(i));
}

void dumpGaps(Out &out) {
  const size_t used = g_gaps.used < kGapCapacity ? g_gaps.used : kGapCapacity;
  for (size_t i = 0; i < used; ++i) {
    const GapEntry &g = g_gaps.entries[i];
    out.str("x");
    out.u64(g.count);
    out.str(" first f");
    out.u64(g.firstFrame);
    out.str(" last f");
    out.u64(g.lastFrame);
    out.str(" ");
    out.str(g.kind < static_cast<uint8_t>(EventKind::Count)
                ? kKindTable[g.kind].name
                : "?");
    out.str(": ");
    out.strn(g.text, kEventTextSize - 1);
    out.ch('\n');
  }
  if (g_gaps.overflow) {
    out.str("(+");
    out.u64(g_gaps.overflow);
    out.str(" further gap lines not tallied)\n");
  }
}

// One JSON object per line, oldest first, committed lines only (a writer that a
// signal interrupted mid-line leaves its slot uncommitted and it is skipped).
// Signal-safe: no allocation.
void dumpTrace(Out &out, const TraceChannel &channel, size_t maxLines) {
  const uint64_t written = channel.written();
  const uint64_t have = written < kTraceCapacity ? written : kTraceCapacity;
  const uint64_t take = maxLines < have ? maxLines : have;
  for (uint64_t index = written - take; index < written; ++index) {
    const char *line = channel.line(index);
    if (!line)
      continue;
    out.strn(line, kTraceLineSize - 1);
    out.ch('\n');
  }
}

int g_traceFds[kTraceChannels] = {-1, -1, -1};

// Non-signal paths installed as the runlog trace hooks.
void traceStreamImpl(const char *channel, const char *line) {
  if (!g_crash.diagDir[0])
    return;
  for (size_t i = 0; i < kTraceChannels; ++i) {
    if (std::strcmp(g_traces[i].name, channel) != 0)
      continue;
    if (g_traceFds[i] < 0) {
      const std::string path =
          std::string(g_crash.diagDir) + "/" + channel + "-full.jsonl";
      g_traceFds[i] =
          ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
      if (g_traceFds[i] < 0)
        return;
    }
    // One write(2) per line so a crash keeps every completed step.
    std::string text(line);
    text.push_back('\n');
    writeAll(g_traceFds[i], text.data(), text.size());
    return;
  }
}

void traceSaveImpl(const char *channel, const char *file) {
  if (!g_crash.diagDir[0])
    return;
  for (const TraceChannel &c : g_traces) {
    if (std::strcmp(c.name, channel) != 0)
      continue;
    const std::string path = std::string(g_crash.diagDir) + "/" + file;
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
      return;
    Out out(fd);
    dumpTrace(out, c, kTraceCapacity);
    out.flush();
    ::close(fd);
    return;
  }
}

// Everything a stopped or crashed run needs to be explained from crash.txt
// alone: the milestones and gaps that the routine-call flood pushed out of the
// ring, and the frame loop's latest thread/world dumps.
void dumpDiagnostics(Out &out) {
  out.str("--- gaps (unsupported/failed/lost, deduplicated) ---\n");
  dumpGaps(out);
  out.str("--- important events (errors, gaps, scheduler and world milestones) "
          "---\n");
  dumpImportantEvents(out);
  for (const SnapshotSlot &s : g_snapshots) {
    if (!s.name[0])
      continue;
    out.str("--- snapshot ");
    out.str(s.name);
    out.str(" (frame ");
    out.u64(s.frame);
    out.str(") ---\n");
    out.strn(s.text, kSnapshotText - 1);
    out.ch('\n');
  }
  for (const TraceChannel &c : g_traces) {
    if (!c.name[0] || !c.written())
      continue;
    out.str("--- trace ");
    out.str(c.name);
    out.str(" (newest lines, oldest first; ");
    out.u64(c.written());
    out.str(" steps recorded) ---\n");
    dumpTrace(out, c, 256);
  }
}

// Non-signal path: refreshed on the heartbeat and at finish so the files exist
// for runs that end cleanly, are killed with SIGKILL, or are read while the run
// is still going.
void writeDiagnosticFiles() {
  if (!g_crash.diagDir[0])
    return;
  const std::string dir = g_crash.diagDir;
  auto write = [&](const char *name, auto &&fill) {
    const std::string path = dir + "/" + name;
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
      return;
    Out out(fd);
    fill(out);
    out.flush();
    ::close(fd);
  };
  write("gaps.txt", [](Out &out) { dumpGaps(out); });
  for (const SnapshotSlot &s : g_snapshots) {
    if (!s.name[0])
      continue;
    write((std::string(s.name) + ".txt").c_str(), [&](Out &out) {
      out.str("frame ");
      out.u64(s.frame);
      out.ch('\n');
      out.strn(s.text, kSnapshotText - 1);
      out.ch('\n');
    });
  }
  for (const TraceChannel &c : g_traces) {
    if (!c.name[0] || !c.written())
      continue;
    write((std::string(c.name) + ".jsonl").c_str(),
          [&](Out &out) { dumpTrace(out, c, kTraceCapacity); });
  }
}

// Runs in the signal handler and from the terminate handler, so it uses only
// open/write/close, hand-formatted numbers and backtrace_symbols_fd.
// backtrace_symbols_fd is documented as safe but is the one piece that can
// still fail on a corrupted stack; the dump is best effort and the events above
// it are already written by then.
void dumpAndRecord(int sig, void *faultAddress, bool terminating) {
  if (g_crash.dumped.exchange(true))
    return;
  // The reader thread copies pipe data to run.log asynchronously. Give it a
  // moment to catch up so the last printed line is in the log.
  timespec pause{0, 40 * 1000 * 1000};
  nanosleep(&pause, nullptr);
  char reason[160];
  size_t r = 0;
  auto append = [&](const char *s) {
    for (; s && *s && r + 1 < sizeof reason; ++s)
      reason[r++] = *s;
  };
  if (g_crash.note[0])
    append(g_crash.note);
  else if (terminating) {
    append("terminated by ");
    append(signalName(sig));
  } else {
    append("crashed with ");
    append(signalName(sig));
  }
  reason[r] = 0;
  const int fd = ::open(g_crash.crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    Out out(fd);
    out.str("signal=");
    out.str(signalName(sig));
    out.str("\nnumber=");
    out.u64(static_cast<uint64_t>(sig));
    out.str("\nfault_address=");
    out.hex(reinterpret_cast<uint64_t>(faultAddress));
    out.str("\nframe=");
    out.u64(g_frame.load(std::memory_order_relaxed));
    out.str("\nseconds=");
    out.seconds3(monotonicSeconds() - g_crash.start);
    if (g_crash.note[0]) {
      out.str("\nnote=");
      out.str(g_crash.note);
    }
    out.str("\n--- last events ---\n");
    dumpRecentEvents(out, kDumpEvents);
    dumpDiagnostics(out);
    out.str("--- backtrace (best effort) ---\n");
    out.flush();
    void *frames[48];
    const int count = backtrace(frames, 48);
    backtrace_symbols_fd(frames, count, fd);
    ::close(fd);
  }
  writeCountersFile(g_crash.countersPath);
  writeResultAsync(sig, terminating ? "terminated" : "crashed", reason);
}

void onSignal(int sig, siginfo_t *info, void *) {
  const bool terminating = sig == SIGTERM || sig == SIGINT;
  if (!g_crash.handling.exchange(true))
    dumpAndRecord(sig, info ? info->si_addr : nullptr, terminating);
  struct sigaction defaults{};
  defaults.sa_handler = SIG_DFL;
  sigemptyset(&defaults.sa_mask);
  sigaction(sig, &defaults, nullptr);
  if (terminating)
    _exit(128 + sig);
  raise(sig);
}

void onTerminate() {
  const char *prefix = "uncaught exception: ";
  size_t n = std::strlen(prefix);
  std::memcpy(g_crash.note, prefix, n);
  std::string what = "unknown type";
  try {
    if (auto e = std::current_exception())
      std::rethrow_exception(e);
    what = "terminate called with no active exception";
  } catch (const std::exception &e) {
    what = e.what();
  } catch (...) {
  }
  for (size_t i = 0; i < what.size() && n + 1 < sizeof g_crash.note; ++i)
    g_crash.note[n++] = (what[i] == '\n' || what[i] == '\r') ? ' ' : what[i];
  g_crash.note[n] = 0;
  dumpAndRecord(SIGABRT, nullptr, false);
  std::abort();
}

void installHandlers() {
  static char altStack[64 * 1024];
  stack_t ss{};
  ss.ss_sp = altStack;
  ss.ss_size = sizeof altStack;
  sigaltstack(&ss, nullptr);
  void *warm[4];
  backtrace(warm, 4); // the first call can allocate, so do it outside a handler
  struct sigaction sa{};
  sa.sa_sigaction = onSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
  for (int sig : kFatalSignals)
    sigaction(sig, &sa, nullptr);
  for (int sig : kStopSignals)
    sigaction(sig, &sa, nullptr);
  std::set_terminate(onTerminate);
  g_crash.installed = true;
}

std::string utcStamp(time_t when, const char *format) {
  tm parts{};
  gmtime_r(&when, &parts);
  char buf[64];
  std::strftime(buf, sizeof buf, format, &parts);
  return buf;
}

std::string shellQuote(const std::string &s) {
  std::string out = "'";
  for (char c : s)
    out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}

std::string runCommand(const std::string &command) {
  std::string out;
  if (FILE *pipe = popen(command.c_str(), "r")) {
    char buf[256];
    while (std::fgets(buf, sizeof buf, pipe) && out.size() < 4096)
      out += buf;
    pclose(pipe);
  }
  while (!out.empty() &&
         (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
    out.pop_back();
  out.erase(0, out.find_first_not_of(' '));
  return out;
}

std::string sha256File(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return "unknown";
  CC_SHA256_CTX ctx;
  CC_SHA256_Init(&ctx);
  static char chunk[1 << 16];
  while (in.read(chunk, sizeof chunk) || in.gcount() > 0)
    CC_SHA256_Update(&ctx, chunk, static_cast<CC_LONG>(in.gcount()));
  if (in.bad())
    return "unknown";
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_Final(digest, &ctx);
  std::string hex;
  for (unsigned char b : digest) {
    hex += "0123456789abcdef"[b >> 4];
    hex += "0123456789abcdef"[b & 15];
  }
  return hex;
}

std::string executablePath() {
  char buf[4096];
  uint32_t size = sizeof buf;
  if (_NSGetExecutablePath(buf, &size) != 0)
    return "unknown";
  std::error_code ec;
  const fs::path canonical = fs::weakly_canonical(buf, ec);
  return ec ? std::string(buf) : canonical.string();
}

std::string quoteArg(const std::string &arg) {
  if (!arg.empty() && arg.find_first_of(" \t\"'\\") == std::string::npos)
    return arg;
  return shellQuote(arg);
}

constexpr uintmax_t kSourceMaxBytes = 128 * 1024 * 1024;
constexpr size_t kSourceMaxEntries = 20000;

std::string sha256Text(const std::string &text) {
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(text.data(), static_cast<CC_LONG>(text.size()), digest);
  std::string hex;
  for (unsigned char b : digest) {
    hex += "0123456789abcdef"[b >> 4];
    hex += "0123456789abcdef"[b & 15];
  }
  return hex;
}

bool collectSource(const fs::path &root, std::string &manifest,
                   std::string &reason) {
  std::error_code ec;
  std::vector<fs::path> paths{"CMakeLists.txt"};
  size_t entries = 0;
  uintmax_t bytes = 0;
  for (const char *top : {"src", "tools", "tests"}) {
    const fs::path tree = root / top;
    if (!fs::is_directory(fs::symlink_status(tree, ec)) || ec) {
      reason = "source directory unavailable";
      return false;
    }
    for (fs::recursive_directory_iterator it(tree, ec), end; !ec && it != end;
         it.increment(ec)) {
      if (++entries > kSourceMaxEntries) {
        reason = "source entry limit exceeded";
        return false;
      }
      const auto status = it->symlink_status(ec);
      if (ec)
        break;
      const auto rel = it->path().lexically_relative(root);
      if (fs::is_directory(status)) {
        if (it->path().filename() == "__pycache__" ||
            it->path().filename() == "build" ||
            it->path().filename() == ".scratch" ||
            it->path().filename() == "local")
          it.disable_recursion_pending();
      } else if (fs::is_regular_file(status)) {
        const auto name = rel.filename().string();
        const auto ext = rel.extension().string();
        if (name == ".DS_Store" || ext == ".pyc" || ext == ".o" ||
            ext == ".a" || ext == ".dylib" || ext == ".so" || ext == ".dll" ||
            ext == ".exe" || ext == ".bin" || ext == ".tmp" || ext == ".log" ||
            ext == ".d")
          continue;
        paths.push_back(rel);
      }
    }
    if (ec) {
      reason = "cannot enumerate source";
      return false;
    }
  }
  std::sort(paths.begin(), paths.end());
  std::ostringstream out;
  for (const auto &relative : paths) {
    const std::string name = relative.generic_string();
    if (name.find_first_of("\r\n\t") != std::string::npos) {
      reason = "unsafe source filename";
      return false;
    }
    const fs::path file = root / relative;
    if (!fs::is_regular_file(fs::symlink_status(file, ec)) || ec) {
      reason = "source file unavailable";
      return false;
    }
    const auto size = fs::file_size(file, ec);
    if (ec || size > kSourceMaxBytes - bytes) {
      reason = "source byte limit exceeded or file unavailable";
      return false;
    }
    bytes += size;
    std::ifstream input(file, std::ios::binary);
    if (!input) {
      reason = "source file unreadable";
      return false;
    }
    char head[8192];
    input.read(head, sizeof head);
    if (input.bad()) {
      reason = "source file unreadable";
      return false;
    }
    if (std::find(head, head + input.gcount(), '\0') != head + input.gcount())
      continue;
    const std::string hash = sha256File(file.string());
    if (hash == "unknown") {
      reason = "source file unreadable";
      return false;
    }
    out << name << '\t' << hash << '\n';
  }
  manifest = out.str();
  return true;
}

struct SourceCapture {
  std::string hash = "unknown", root = "unavailable", status = "unavailable";
};
SourceCapture captureSource(const std::string &configuredRoot,
                            const fs::path &runDir) {
  SourceCapture result;
  const fs::path compiledFile(__FILE__);
  if (configuredRoot.empty() && !compiledFile.is_absolute()) {
    result.status = "unavailable: source root not embedded";
    return result;
  }
  std::error_code ec;
  const fs::path root = fs::canonical(
      configuredRoot.empty() ? compiledFile.parent_path().parent_path()
                             : fs::path(configuredRoot),
      ec);
  if (ec) {
    result.status = "unavailable: source root missing";
    return result;
  }
  result.root = root.string();
  const auto marker = root / "CMakeLists.txt";
  if (!fs::is_regular_file(fs::symlink_status(marker, ec)) || ec) {
    result.status = "unavailable: source root marker missing";
    return result;
  }
  const auto markerSize = fs::file_size(marker, ec);
  if (ec || markerSize > 1024 * 1024) {
    result.status = "unavailable: source root marker unreadable or oversized";
    return result;
  }
  std::ifstream in(marker);
  std::string config((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
  if (!in ||
      config.find("project(sl_port LANGUAGES CXX)") == std::string::npos) {
    result.status = "unavailable: source root marker mismatch";
    return result;
  }
  std::string first, second, reason;
  if (!collectSource(root, first, reason) ||
      !collectSource(root, second, reason)) {
    result.status = "unavailable: " + reason;
    return result;
  }
  if (first != second) {
    result.status = "unavailable: source changed during capture";
    return result;
  }
  std::ofstream output(runDir / "source-manifest.txt", std::ios::binary);
  output << first;
  output.close();
  if (!output) {
    fs::remove(runDir / "source-manifest.txt", ec);
    result.status = "unavailable: cannot write source manifest";
    return result;
  }
  result.hash = sha256Text(first);
  result.status =
      "captured; working tree observed at launch; build identity unavailable";
  return result;
}

constexpr uintmax_t kSaveSnapshotBytes = 16 * 1024 * 1024;
constexpr size_t kSaveSnapshotFiles = 1024;
constexpr size_t kSaveSnapshotDirectories = 1024;

std::string processStartTime() {
  proc_bsdinfo info{};
  if (proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &info, sizeof info) !=
      sizeof info)
    return "unavailable";
  char value[64];
  std::snprintf(value, sizeof value, "%llu.%06llu",
                static_cast<unsigned long long>(info.pbi_start_tvsec),
                static_cast<unsigned long long>(info.pbi_start_tvusec));
  return value;
}

bool containsPath(const fs::path &parent, const fs::path &child) {
  auto p = parent.begin(), c = child.begin();
  for (; p != parent.end(); ++p, ++c)
    if (c == child.end() || *p != *c)
      return false;
  return true;
}

std::string snapshotSave(const std::string &dataRoot, const fs::path &runDir) {
  if (dataRoot.empty())
    return "unavailable: no data root";
  std::error_code ec;
  const fs::path source = fs::path(dataRoot) / "save";
  auto status = fs::symlink_status(source, ec);
  if (ec || !fs::is_directory(status))
    return "unavailable: save directory missing or unsafe";
  const fs::path sourcePath = fs::canonical(source, ec);
  if (ec || containsPath(sourcePath, runDir))
    return "unavailable: unsafe snapshot destination";
  std::vector<fs::path> files, directories;
  uintmax_t bytes = 0;
  for (fs::recursive_directory_iterator it(source, ec), end; !ec && it != end;
       it.increment(ec)) {
    const auto entryStatus = it->symlink_status(ec);
    if (ec)
      break;
    if (fs::is_directory(entryStatus)) {
      if (directories.size() == kSaveSnapshotDirectories)
        return "unavailable: save exceeds snapshot limits or cannot be read";
      directories.push_back(it->path().lexically_relative(source));
      continue;
    }
    if (!fs::is_regular_file(entryStatus))
      return "unavailable: save contains a symlink or special file";
    const auto size = it->file_size(ec);
    if (ec || size > kSaveSnapshotBytes - bytes ||
        files.size() == kSaveSnapshotFiles)
      return "unavailable: save exceeds snapshot limits or cannot be read";
    bytes += size;
    files.push_back(it->path().lexically_relative(source));
  }
  if (ec)
    return "unavailable: cannot enumerate save";
  std::sort(files.begin(), files.end());
  const fs::path dest = runDir / "initial-save";
  fs::create_directory(dest, ec);
  if (ec)
    return "unavailable: cannot create snapshot";
  for (const auto &relative : directories) {
    fs::create_directories(dest / relative, ec);
    if (ec)
      break;
  }
  std::ostringstream manifest;
  manifest << "format=1\nfiles=" << files.size() << "\nbytes=" << bytes << "\n";
  for (const auto &relative : files) {
    if (ec)
      break;
    // Newlines would make the manifest ambiguous.
    const std::string name = relative.generic_string();
    if (name.find_first_of("\r\n\t") != std::string::npos) {
      ec = std::make_error_code(std::errc::invalid_argument);
      break;
    }
    const fs::path src = source / relative, dst = dest / relative;
    fs::create_directories(dst.parent_path(), ec);
    if (ec)
      break;
    const std::string before = sha256File(src.string());
    if (before == "unknown") {
      ec = std::make_error_code(std::errc::io_error);
      break;
    }
    fs::copy_file(src, dst, fs::copy_options::none, ec);
    if (ec || sha256File(dst.string()) != before ||
        sha256File(src.string()) != before) {
      ec = std::make_error_code(std::errc::io_error);
      break;
    }
    manifest << before << "  " << name << "\n";
  }
  // Re-enumerate after copying so additions, removals, and later edits are not
  // silently represented as a complete snapshot. Concurrent writers can still
  // change the save after this check; capture is not an atomic filesystem
  // snapshot.
  if (!ec) {
    std::vector<fs::path> afterFiles, afterDirectories;
    for (fs::recursive_directory_iterator it(source, ec), end; !ec && it != end;
         it.increment(ec)) {
      const auto entryStatus = it->symlink_status(ec);
      if (ec)
        break;
      const auto relative = it->path().lexically_relative(source);
      if (fs::is_directory(entryStatus) &&
          afterDirectories.size() < kSaveSnapshotDirectories)
        afterDirectories.push_back(relative);
      else if (fs::is_regular_file(entryStatus) &&
               afterFiles.size() < kSaveSnapshotFiles)
        afterFiles.push_back(relative);
      else {
        ec = std::make_error_code(std::errc::invalid_argument);
        break;
      }
    }
    std::sort(afterFiles.begin(), afterFiles.end());
    std::sort(afterDirectories.begin(), afterDirectories.end());
    std::sort(directories.begin(), directories.end());
    if (afterFiles != files || afterDirectories != directories)
      ec = std::make_error_code(std::errc::io_error);
    for (const auto &relative : files) {
      if (ec)
        break;
      const auto current = sha256File((source / relative).string());
      if (current == "unknown" ||
          current != sha256File((dest / relative).string()))
        ec = std::make_error_code(std::errc::io_error);
    }
  }
  if (!ec) {
    std::ofstream out(runDir / "initial-save-manifest.txt");
    out << manifest.str();
    if (!out)
      ec = std::make_error_code(std::errc::io_error);
  }
  if (ec) {
    std::error_code ignored;
    fs::remove_all(dest, ignored);
    fs::remove(runDir / "initial-save-manifest.txt", ignored);
    return "unavailable: save changed or snapshot failed";
  }
  return "captured";
}

} // namespace

std::string formatLine(double t, uint32_t frame, const char *tag,
                       const char *text) {
  char head[48];
  std::snprintf(head, sizeof head, "[+%06.3f f%u] ", t < 0 ? 0.0 : t, frame);
  return std::string(head) + tag + " " + (text ? text : "");
}

size_t formatCounters(char *buf, size_t cap) {
  if (!cap)
    return 0;
  Out out(buf, cap);
  for (size_t i = 0; i < kKindCount; ++i) {
    out.str("event_");
    out.str(kKindTable[i].name);
    out.ch('=');
    out.u64(g_kindCounts[i].load(std::memory_order_relaxed));
    out.ch('\n');
  }
  for (size_t i = 0; i < static_cast<size_t>(Counter::Count); ++i) {
    out.str(kCounterNames[i]);
    out.ch('=');
    out.u64(g_counters[i].load(std::memory_order_relaxed));
    out.ch('\n');
  }
  out.str("frames=");
  out.u64(g_frame.load(std::memory_order_relaxed));
  out.ch('\n');
  out.flush();
  buf[out.sinkLen < cap ? out.sinkLen : cap - 1] = 0;
  return out.sinkLen;
}

namespace {

struct InputState {
  int fd = -1;
  bool havePending = false, haveWritten = false;
  float pendingX = 0, pendingY = 0;
  int writtenX = 0, writtenY = 0;
  double lastWrite = -1;
  unsigned notedButtons = 0;
} g_input;

void writeInputLine(const std::string &line) {
  if (g_input.fd < 0)
    return;
  const std::string withNewline = line + "\n";
  writeAll(g_input.fd, withNewline.data(), withNewline.size());
  count(Counter::InputRecorded);
}

void writePendingMotion(double t) {
  if (!g_input.havePending)
    return;
  g_input.havePending = false;
  const int x = static_cast<int>(std::lround(g_input.pendingX)),
            y = static_cast<int>(std::lround(g_input.pendingY));
  if (g_input.haveWritten && x == g_input.writtenX && y == g_input.writtenY)
    return;
  g_input.writtenX = x;
  g_input.writtenY = y;
  g_input.haveWritten = true;
  g_input.lastWrite = t;
  writeInputLine(formatInputLine(t, "move", std::to_string(x).c_str(),
                                 std::to_string(y).c_str()));
}

} // namespace

std::string formatInputLine(double t, const char *verb, const char *a,
                            const char *b) {
  char head[32];
  std::snprintf(head, sizeof head, "%.3f ", t < 0 ? 0.0 : t);
  std::string line = std::string(head) + verb;
  if (a)
    line += std::string(" ") + a;
  if (b)
    line += std::string(" ") + b;
  return line;
}

void inputMotion(double t, float x, float y) {
  if (g_input.fd < 0)
    return;
  g_input.pendingX = x;
  g_input.pendingY = y;
  g_input.havePending = true;
  if (g_input.lastWrite < 0 || t - g_input.lastWrite >= 0.010)
    writePendingMotion(t);
}

void inputFlush(double t) {
  if (g_input.fd >= 0 && g_input.havePending && t - g_input.lastWrite >= 0.010)
    writePendingMotion(t);
}

void inputButton(double t, bool down) {
  if (g_input.fd < 0)
    return;
  writePendingMotion(t);
  writeInputLine(formatInputLine(t, down ? "down" : "up"));
}

void inputKey(double t, const char *name, bool down) {
  if (g_input.fd < 0)
    return;
  writePendingMotion(t);
  writeInputLine(formatInputLine(t, down ? "keydown" : "keyup", name));
}

void inputUnsupportedButton(double t, int sdlButton) {
  if (g_input.fd < 0 || sdlButton < 0 || sdlButton > 31 ||
      (g_input.notedButtons & (1u << sdlButton)))
    return;
  g_input.notedButtons |= 1u << sdlButton;
  char text[96];
  std::snprintf(
      text, sizeof text,
      "mouse button %d at +%.3fs is not recorded: only the left button replays",
      sdlButton, t);
  event(EventKind::Note, "%s", text);
  writeInputLine(std::string("# ") + text);
}

struct RunRecorder::Impl {
  std::string dir;
  std::string logPath;
  int logFd = -1;
  std::mutex logMutex;
  double start = 0;
  bool finished = false;
  bool capturing = false;
  uint64_t checkpointSequence = 0;

  struct Stream {
    int original = -1, pipeRead = -1, pipeWrite = -1;
    std::string pending;
    std::string reason;
    std::vector<std::string> stack;
    bool tracking = false;
  } streams[2];
  std::thread reader;
  mutable std::mutex reasonMutex;

  void writeLogLine(const std::string &line) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFd < 0)
      return;
    std::string withNewline = line;
    withNewline += '\n';
    writeAll(logFd, withNewline.data(), withNewline.size());
  }

  void checkpoint() {
    uint64_t written = 0;
    const auto events = theHub().snapshot(kDumpEvents, written);
    const uint64_t sequence = checkpointSequence + 1;
    const std::string temporary = dir + "/flight.tmp";
    const std::string target =
        dir + "/flight-" + std::to_string(sequence % 2) + ".log";
    std::ostringstream text;
    text << "# checkpoint=" << sequence << " ring_written=" << written
         << " retained=" << events.size()
         << " slots=2 overwrites_older_checkpoints=1\n";
    for (const auto &e : events) {
      const char *kind = e.kind < static_cast<uint8_t>(EventKind::Count)
                             ? kKindTable[e.kind].name
                             : "?";
      text << formatLine(e.t, e.frame, kind, e.text) << '\n';
    }
    const auto data = text.str();
    const int fd =
        ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
      writeLogLine(formatLine(monotonicSeconds() - start, g_frame.load(),
                              "note", "flight checkpoint open failed"));
      return;
    }
    const bool ok = writeAll(fd, data.data(), data.size()) && ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    if (!ok || !closed || ::rename(temporary.c_str(), target.c_str()) != 0) {
      ::unlink(temporary.c_str());
      writeLogLine(formatLine(monotonicSeconds() - start, g_frame.load(),
                              "note", "flight checkpoint write failed"));
      return;
    }
    checkpointSequence = sequence;
  }

  void onCapturedLine(int index, const std::string &line) {
    const char *tag = index == 0 ? "out" : "err";
    writeLogLine(formatLine(monotonicSeconds() - start,
                            g_frame.load(std::memory_order_relaxed), tag,
                            line.c_str()));
    Stream &s = streams[index];
    std::lock_guard<std::mutex> lock(reasonMutex);
    if (line.rfind("startup probe:", 0) == 0) {
      s.reason = line;
      s.stack.clear();
      s.tracking = true;
    } else if (s.tracking && line.rfind("  ", 0) == 0) {
      s.stack.push_back(line);
    } else {
      s.tracking = false;
    }
  }

  void feed(int index, const char *data, size_t size) {
    Stream &s = streams[index];
    s.pending.append(data, size);
    size_t from = 0;
    for (size_t nl; (nl = s.pending.find('\n', from)) != std::string::npos;
         from = nl + 1) {
      std::string line = s.pending.substr(from, nl - from);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      onCapturedLine(index, line);
    }
    s.pending.erase(0, from);
    if (s.pending.size() > 65536) {
      onCapturedLine(index, s.pending);
      s.pending.clear();
    }
  }

  void readerLoop() {
    char buf[8192];
    bool open[2] = {true, true};
    while (open[0] || open[1]) {
      pollfd fds[2];
      for (int i = 0; i < 2; ++i)
        fds[i] = {open[i] ? streams[i].pipeRead : -1, POLLIN, 0};
      if (poll(fds, 2, -1) < 0) {
        if (errno == EINTR)
          continue;
        break;
      }
      for (int i = 0; i < 2; ++i) {
        if (!open[i] || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
          continue;
        const ssize_t n = ::read(streams[i].pipeRead, buf, sizeof buf);
        if (n > 0) {
          writeAll(streams[i].original, buf, static_cast<size_t>(n));
          feed(i, buf, static_cast<size_t>(n));
        } else if (n == 0 || errno != EINTR) {
          open[i] = false;
        }
      }
    }
    for (int i = 0; i < 2; ++i)
      if (!streams[i].pending.empty()) {
        onCapturedLine(i, streams[i].pending);
        streams[i].pending.clear();
      }
  }

  bool startCapture() {
    std::fflush(stdout);
    std::fflush(stderr);
    for (int i = 0; i < 2; ++i) {
      int fds[2];
      if (::pipe(fds) != 0)
        return false;
      streams[i].pipeRead = fds[0];
      streams[i].pipeWrite = fds[1];
      streams[i].original = ::dup(i + 1);
      if (streams[i].original < 0)
        return false;
    }
    for (int i = 0; i < 2; ++i)
      ::dup2(streams[i].pipeWrite, i + 1);
    // stdout was line buffered on a terminal; a pipe would make it block
    // buffered and hide the last lines of a crash.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    // A termination signal must reach a thread that can run the handler
    // quickly, not this reader.
    sigset_t block, previous;
    sigemptyset(&block);
    for (int sig : kStopSignals)
      sigaddset(&block, sig);
    pthread_sigmask(SIG_BLOCK, &block, &previous);
    reader = std::thread([this] { readerLoop(); });
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    capturing = true;
    return true;
  }

  void stopCapture() {
    if (!capturing)
      return;
    capturing = false;
    std::fflush(stdout);
    std::fflush(stderr);
    for (int i = 0; i < 2; ++i)
      ::dup2(streams[i].original, i + 1);
    for (int i = 0; i < 2; ++i)
      ::close(streams[i].pipeWrite);
    reader.join();
    for (int i = 0; i < 2; ++i) {
      ::close(streams[i].pipeRead);
      ::close(streams[i].original);
    }
  }

  std::vector<std::string> reasonLines() const {
    std::lock_guard<std::mutex> lock(reasonMutex);
    // stderr is where the probe reports failures; fall back to stdout.
    for (int i : {1, 0}) {
      const Stream &s = streams[i];
      if (s.reason.empty())
        continue;
      std::vector<std::string> out{s.reason};
      out.insert(out.end(), s.stack.begin(), s.stack.end());
      return out;
    }
    return {};
  }
};

RunRecorder::RunRecorder() : impl_(new Impl) {}

RunRecorder::~RunRecorder() {
  if (!impl_->finished)
    finish(1);
  g_hub.store(nullptr, std::memory_order_release);
  if (g_input.fd >= 0)
    ::close(g_input.fd);
  g_input.fd = -1;
  g_crash.countersPath[0] = 0;
  if (impl_->logFd >= 0)
    ::close(impl_->logFd);
}

const std::string &RunRecorder::dir() const { return impl_->dir; }

std::vector<std::string> RunRecorder::reasonLines() const {
  return impl_->reasonLines();
}

std::unique_ptr<RunRecorder> RunRecorder::start(const RunOptions &options,
                                                std::string &error) {
  std::string base = options.baseDir;
  if (base.empty()) {
    const char *env = std::getenv("SLRR_RUN_DIR");
    base = env && *env ? env : "local/runs";
  }
  const time_t now = std::time(nullptr);
  const std::string name =
      utcStamp(now, "%Y%m%dT%H%M%SZ") + "-" + std::to_string(getpid());
  std::error_code ec;
  const fs::path reference = fs::weakly_canonical(options.referenceRoot, ec);
  if (ec) {
    error = "cannot resolve ref directory";
    return nullptr;
  }
  const fs::path resolvedBase = fs::weakly_canonical(base, ec);
  if (ec || containsPath(reference, resolvedBase) ||
      containsPath(resolvedBase, reference)) {
    error = "run directory must be outside ref and must not contain it";
    return nullptr;
  }
  if (!options.gameRoot.empty()) {
    const auto game = fs::weakly_canonical(options.gameRoot, ec);
    if (ec || containsPath(game, resolvedBase) || containsPath(resolvedBase, game)) {
      error = "run directory must not overlap installed game";
      return nullptr;
    }
  }
  fs::create_directories(base, ec);
  fs::path dir = fs::path(base) / name;
  for (int suffix = 2; !fs::create_directory(dir, ec); ++suffix) {
    if (ec || suffix > 50) {
      error = "cannot create run directory " + dir.string();
      return nullptr;
    }
    dir = fs::path(base) / (name + "-" + std::to_string(suffix));
  }
  const fs::path absolute = fs::canonical(dir, ec);
  if (ec) {
    error = "cannot resolve run directory";
    return nullptr;
  }
  fs::permissions(absolute, fs::perms::owner_all, fs::perm_options::replace,
                  ec);
  if (ec) {
    error = "cannot make run directory private";
    return nullptr;
  }

  std::unique_ptr<RunRecorder> recorder(new RunRecorder);
  Impl &impl = *recorder->impl_;
  impl.dir = absolute.string();
  impl.logPath = (absolute / "run.log").string();
  impl.logFd =
      ::open(impl.logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (impl.logFd < 0) {
    error = "cannot open " + impl.logPath;
    return nullptr;
  }
  impl.start = monotonicSeconds();

  // Meta first: it forks helper processes, which must not inherit the capture
  // pipes.
  std::ostringstream meta;
  std::string argvLine;
  for (const auto &a : options.argv)
    argvLine += (argvLine.empty() ? "" : " ") + quoteArg(a);
  struct stat st{};
  const std::string exe = executablePath();
  meta << "argv: " << argvLine << "\n";
  for (size_t i = 0; i < options.argv.size(); ++i)
    meta << "arg[" << i << "]: " << options.argv[i] << "\n";
  meta << "cwd: " << fs::current_path(ec).string() << "\n";
  meta << "pid: " << getpid() << "\n";
  meta << "process_start_unix: " << processStartTime() << "\n";
  meta << "start_utc: " << utcStamp(now, "%Y-%m-%dT%H:%M:%SZ") << "\n";
  meta << "executable: " << exe << "\n";
  meta << "executable_mtime_utc: "
       << (stat(exe.c_str(), &st) == 0
               ? utcStamp(st.st_mtime, "%Y-%m-%dT%H:%M:%SZ")
               : "unknown")
       << "\n";
  meta << "exe_sha256: " << sha256File(exe) << "\n";
  const char *srcHash = std::getenv("SLRR_SRC_HASH");
  const auto source = captureSource(options.sourceRoot, absolute);
  meta << "src_tree_sha256: " << source.hash << "\n";
  meta << "source_identity: " << source.status << "\n";
  meta << "source_root: " << source.root << "\n";
  meta << "source_limits: 20000 entries, 134217728 bytes; symlinks and "
          "binaries excluded\n";
  meta << "launcher_src_tree_sha256: "
       << (srcHash && *srcHash ? srcHash : "unavailable") << "\n";
  const std::string head =
      runCommand("git rev-parse --verify -q HEAD 2>/dev/null");
  meta << "git_head: " << (head.empty() ? "unavailable" : head) << "\n";
  const std::string dirty =
      runCommand("git status --short 2>/dev/null && printf '__STATUS_OK__\\n'");
  const auto marker = dirty.rfind("__STATUS_OK__");
  if (marker == std::string::npos)
    meta << "git_changed_files: unknown\n";
  else {
    const std::string entries = dirty.substr(0, marker);
    meta << "git_changed_files: "
         << std::count(entries.begin(), entries.end(), '\n') << "\n";
  }
  meta << "game_root: " << options.gameRoot << "\n";
  meta << "reference_root: " << reference.string() << "\n";
  meta << "data_root: " << options.dataRoot << "\n";
  // The launcher reports a prelaunch inventory; it does not trace game reads.
  std::string gameDataStatus = "unavailable";
  std::string gameDataPath = "unavailable";
  std::string gameDataHash = "unavailable";
  const char *manifest = std::getenv("SLRR_GAME_DATA_MANIFEST");
  const char *manifestRoot = std::getenv("SLRR_GAME_DATA_ROOT");
  const char *manifestHash = std::getenv("SLRR_GAME_DATA_SHA256");
  const char *manifestStatus = std::getenv("SLRR_GAME_DATA_STATUS");
  if (manifest && manifestRoot && manifestHash && manifestStatus &&
      (std::string(manifestStatus) == "complete" ||
       std::string(manifestStatus) == "incomplete")) {
    std::error_code dataEc;
    const auto sourcePath = fs::path(manifest);
    const auto sourceStatus = fs::symlink_status(sourcePath, dataEc);
    const auto actualRoot = fs::canonical(options.gameRoot, dataEc);
    const auto recordedRoot = fs::canonical(manifestRoot, dataEc);
    if (!dataEc && fs::is_regular_file(sourceStatus) &&
        actualRoot == recordedRoot &&
        fs::file_size(sourcePath, dataEc) <= 16 * 1024 * 1024 && !dataEc) {
      const auto before = sha256File(sourcePath.string());
      const auto dest = absolute / "game-data-manifest.json";
      if (before != "unknown" && before == manifestHash &&
          fs::copy_file(sourcePath, dest, fs::copy_options::none, dataEc) &&
          !dataEc && sha256File(dest.string()) == before &&
          sha256File(sourcePath.string()) == before) {
        gameDataStatus = std::string("launcher-reported ") + manifestStatus;
        gameDataPath = dest.string();
        gameDataHash = before;
      } else {
        fs::remove(dest, dataEc);
      }
    }
  }
  meta << "game_data_status: " << gameDataStatus << "\n";
  meta << "game_data_manifest: " << gameDataPath << "\n";
  meta << "game_data_manifest_sha256: " << gameDataHash << "\n";
  meta << "game_data_scope: prelaunch inventory; not files read or later "
          "mutations\n";
  std::string md5 = "none";
  if (!options.dataRoot.empty()) {
    const fs::path opt =
        fs::path(options.dataRoot) / "save" / "game" / "options";
    if (fs::is_regular_file(opt, ec)) {
      md5 = runCommand("/sbin/md5 -q " + shellQuote(opt.string()) +
                       " 2>/dev/null");
      if (md5.empty())
        md5 = "unknown";
    }
  }
  meta << "options_md5: " << md5 << "\n";
  meta << "initial_save_snapshot: " << snapshotSave(options.dataRoot, absolute)
       << "\n";
  meta << "initial_save_limits: 1024 files, 1024 directories, 16777216 bytes; "
          "symlinks and special files excluded\n";
  const int sdl = SDL_GetVersion();
  meta << "sdl_version: " << SDL_VERSIONNUM_MAJOR(sdl) << "."
       << SDL_VERSIONNUM_MINOR(sdl) << "." << SDL_VERSIONNUM_MICRO(sdl) << "\n";
  utsname un{};
  const std::string product = runCommand("sw_vers -productVersion 2>/dev/null");
  meta << "macos: " << (product.empty() ? "unknown" : product) << "\n";
  meta << "kernel: "
       << (uname(&un) == 0
               ? std::string(un.sysname) + " " + un.release + " " + un.machine
               : "unknown")
       << "\n";
#ifdef SLRR_BUILD_TYPE
  meta << "build_type: " << SLRR_BUILD_TYPE << "\n";
#else
  meta << "build_type: unknown\n";
#endif
  meta << "script: "
       << (options.scriptPath.empty() ? "none" : options.scriptPath) << "\n";
  meta << "run_dir: " << impl.dir << "\n";
  {
    std::ofstream out(absolute / "meta.txt");
    out << meta.str();
  }
  if (!options.scriptPath.empty()) {
    fs::copy_file(options.scriptPath, absolute / "script.txt",
                  fs::copy_options::overwrite_existing, ec);
  }

  const fs::path latest = fs::path(base) / "latest";
  fs::remove(latest, ec);
  fs::create_directory_symlink(absolute, latest, ec);

  Hub &hub = theHub();
  hub.ring.clear();
  hub.start = impl.start;
  hub.context = &impl;
  hub.checkpoint = [](void *context) {
    static_cast<Impl *>(context)->checkpoint();
  };
  hub.persist = [](void *context, const Event &e) {
    auto *self = static_cast<Impl *>(context);
    const char *name = e.kind < static_cast<uint8_t>(EventKind::Count)
                           ? kKindTable[e.kind].name
                           : "?";
    self->writeLogLine(formatLine(e.t, e.frame, name, e.text));
  };
  setFrame(0);
  for (auto &c : g_kindCounts)
    c.store(0, std::memory_order_relaxed);
  for (auto &c : g_counters)
    c.store(0, std::memory_order_relaxed);
  g_input = InputState{};
  g_input.fd = ::open((absolute / "input.txt").c_str(),
                      O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
  std::snprintf(g_crash.countersPath, sizeof g_crash.countersPath, "%s",
                (absolute / "counters.txt").c_str());
  std::snprintf(g_crash.diagDir, sizeof g_crash.diagDir, "%s",
                absolute.c_str());
  g_gaps = GapTable{};
  for (auto &s : g_snapshots)
    s = SnapshotSlot{};
  hub.important.clear();
  for (size_t i = 0; i < kTraceChannels; ++i) {
    g_traces[i].next.store(0, std::memory_order_relaxed);
    for (auto &word : g_traces[i].committed)
      word.store(0, std::memory_order_relaxed);
    g_traces[i].stream.store(false, std::memory_order_relaxed);
    if (g_traceFds[i] >= 0)
      ::close(g_traceFds[i]);
    g_traceFds[i] = -1;
  }
  g_traceStreamHook = &traceStreamImpl;
  g_traceSaveHook = &traceSaveImpl;
  g_traceFlushHook = &flushDiagnostics;
  g_hub.store(&hub, std::memory_order_release);

  if (options.installHandlers) {
    std::snprintf(g_crash.crashPath, sizeof g_crash.crashPath, "%s",
                  (absolute / "crash.txt").c_str());
    std::snprintf(g_crash.resultPath, sizeof g_crash.resultPath, "%s",
                  (absolute / "result.txt").c_str());
    g_crash.start = impl.start;
    g_crash.dumped = false;
    g_crash.handling = false;
    g_crash.note[0] = 0;
    installHandlers();
  }
  if (options.captureStreams && !impl.startCapture())
    error = "stdout capture unavailable; terminal output only";
  eventText(EventKind::Note, "run started");
  return recorder;
}

int RunRecorder::finish(int exitCode) {
  Impl &impl = *impl_;
  if (impl.finished)
    return exitCode;
  impl.finished = true;
  const double seconds = monotonicSeconds() - impl.start;
  const uint32_t frames = g_frame.load(std::memory_order_relaxed);
  char summary[kEventTextSize];
  std::snprintf(summary, sizeof summary, "exit_code=%d frames=%u avg_fps=%.1f",
                exitCode, frames, seconds > 0 ? frames / seconds : 0.0);
  eventText(EventKind::FrameSummary, summary);
  impl.stopCapture();
  writeCountersFile(g_crash.countersPath);
  writeDiagnosticFiles();
  const auto reason = impl.reasonLines();
  const bool ok = exitCode == 0;
  if (!ok) {
    impl.writeLogLine("--- last events ---");
    for (const Event &e : theHub().ring.last(kDumpEvents)) {
      const char *name = e.kind < static_cast<uint8_t>(EventKind::Count)
                             ? kKindTable[e.kind].name
                             : "?";
      impl.writeLogLine(formatLine(e.t, e.frame, name, e.text));
    }
  }
  std::ofstream result(fs::path(impl.dir) / "result.txt");
  result << "status=" << (ok ? "ok" : "failed") << "\n";
  result << "exit_code=" << exitCode << "\n";
  result << "signal=none\n";
  char secs[32];
  std::snprintf(secs, sizeof secs, "%.3f", seconds);
  result << "seconds=" << secs << "\n";
  result << "frames=" << frames << "\n";
  result << "reason=" << (ok || reason.empty() ? "" : reason.front()) << "\n";
  for (size_t i = 1; !ok && i < reason.size(); ++i)
    result << "stack=" << reason[i] << "\n";
  result.close();
  return exitCode;
}

void frameTick(uint32_t frame) {
  setFrame(frame);
  Hub *hub = g_hub.load(std::memory_order_acquire);
  if (!hub)
    return;
  static double lastBeat = -1, lastSecond = -1, lastStart = -1;
  static uint32_t beatFrames = 0, secondFrames = 0;
  const double now = monotonicSeconds();
  if (lastStart != hub->start) {
    lastStart = hub->start;
    lastBeat = lastSecond = now;
    beatFrames = secondFrames = 0;
  }
  ++beatFrames;
  ++secondFrames;
  if (now - lastSecond >= 1.0) {
    const double fps = secondFrames / (now - lastSecond);
    if (fps < 20.0)
      event(EventKind::FrameSummary, "slow: fps=%.1f frame=%u", fps, frame);
    lastSecond = now;
    secondFrames = 0;
  }
  if (now - lastBeat >= 5.0) {
    const double fps = beatFrames / (now - lastBeat);
    char last[96] = "none";
    uint64_t written = 0;
    const auto recent = hub->snapshot(kDumpEvents, written);
    for (size_t i = recent.size(); i-- > 0;) {
      const Event &e = recent[i];
      const auto kind = static_cast<EventKind>(e.kind);
      if (kind == EventKind::Heartbeat || kind == EventKind::FrameSummary ||
          e.kind >= static_cast<uint8_t>(EventKind::Count))
        continue;
      std::snprintf(last, sizeof last, "%s: %s", kindInfo(kind).name, e.text);
      break;
    }
    event(EventKind::Heartbeat, "frame=%u fps=%.1f last_event=%s", frame, fps,
          last);
    if (hub->checkpoint)
      hub->checkpoint(hub->context);
    writeDiagnosticFiles();
    lastBeat = now;
    beatFrames = 0;
  }
}

void flushDiagnostics() { writeDiagnosticFiles(); }

} // namespace runlog
