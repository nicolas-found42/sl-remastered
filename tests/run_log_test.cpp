// Unit tests for the run log: ring behavior, line format, result.txt and
// pruning.
#include "run_log.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

using namespace runlog;
namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);      \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

static std::string slurp(const fs::path &p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static void ringWraparound() {
  auto ring = std::make_unique<EventRing>();
  const size_t total = kRingCapacity + 100;
  for (size_t i = 0; i < total; ++i) {
    char text[32];
    std::snprintf(text, sizeof text, "event %zu", i);
    ring->push(static_cast<double>(i), static_cast<uint32_t>(i),
               EventKind::Note, text);
  }
  CHECK(ring->size() == kRingCapacity);
  CHECK(std::string(ring->at(0).text) == "event 100");
  CHECK(std::string(ring->at(kRingCapacity - 1).text) ==
        "event " + std::to_string(total - 1));
  const auto tail = ring->last(200);
  CHECK(tail.size() == 200);
  for (size_t i = 0; i < tail.size(); ++i)
    CHECK(tail[i].frame == total - 200 + i);
}

static void ringThreadsDoNotLoseSlots() {
  auto ring = std::make_unique<EventRing>();
  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t)
    threads.emplace_back([&] {
      for (int i = 0; i < 500; ++i)
        ring->push(0, 0, EventKind::Thread, "x");
    });
  for (auto &t : threads)
    t.join();
  CHECK(ring->written() == 2000);
}

static void randomBurstKeepsHistory() {
  auto hub = std::make_unique<Hub>();
  hub->emit(EventKind::NativeCall, "Part.create()V -> void");
  for (int i = 0; i < 5000; ++i)
    hub->emit(EventKind::NativeCall, "Math.random()F -> 0.5");
  hub->emit(EventKind::NativeCall, "Part.install()V -> void");
  CHECK(hub->ring.size() == 3);
  CHECK(std::string(hub->ring.at(0).text).find("Part.create") !=
        std::string::npos);
  CHECK(std::string(hub->ring.at(2).text).find("Part.install") !=
        std::string::npos);
}

static void truncationAndSanitizing() {
  auto ring = std::make_unique<EventRing>();
  const std::string huge(1000, 'a');
  ring->push(0, 0, EventKind::Error, huge.c_str());
  CHECK(std::strlen(ring->at(0).text) == kEventTextSize - 1);
  ring->push(0, 0, EventKind::Error, "line one\nline two\t!");
  CHECK(std::string(ring->at(1).text) == "line one line two !");
  ring->push(0, 0, EventKind::Error, nullptr);
  CHECK(std::string(ring->at(2).text).empty());
}

static void kindTable() {
  for (EventKind k : {EventKind::Input, EventKind::Sound, EventKind::Drop,
                      EventKind::Resource, EventKind::Note, EventKind::Error,
                      EventKind::Heartbeat, EventKind::FrameSummary})
    CHECK(kindInfo(k).persist);
  for (EventKind k : {EventKind::NativeCall, EventKind::CursorEvent,
                      EventKind::Thread, EventKind::ScriptEnter})
    CHECK(!kindInfo(k).persist);
  CHECK(std::string(kindInfo(EventKind::Drop).name) == "drop");
}

static void hooksAreNoOpsWithoutRecorder() {
  CHECK(!active());
  event(EventKind::Error, "nobody is listening %d", 1);
  setFrame(5);
}

static void formatLineShape() {
  CHECK(formatLine(12.3456, 77, "out", "hello") == "[+12.346 f77] out hello");
  CHECK(formatLine(0.5, 0, "err", "x") == "[+00.500 f0] err x");
}

static void recorderWritesFilesAndResult(const fs::path &base) {
  RunOptions options;
  options.baseDir = base.string();
  options.argv = {"startup_probe", "--init"};
  options.gameRoot = "ref/raw";
  options.captureStreams = false;
  options.installHandlers = false;
  std::string error;
  auto recorder = RunRecorder::start(options, error);
  CHECK(recorder != nullptr);
  if (!recorder)
    return;
  const fs::path dir = recorder->dir();
  CHECK(fs::exists(dir / "run.log"));
  CHECK(fs::exists(dir / "meta.txt"));
  CHECK(!fs::exists(dir / "crash.txt"));
  CHECK(fs::is_symlink(base / "latest"));
  CHECK(slurp(dir / "meta.txt").find("argv: startup_probe --init") !=
        std::string::npos);
  CHECK(slurp(dir / "meta.txt").find("sdl_version: 3.") != std::string::npos);
  CHECK(
      slurp(dir / "meta.txt")
          .find("source_identity: captured; working tree observed at launch") !=
      std::string::npos);
  CHECK(fs::exists(dir / "source-manifest.txt"));

  setFrame(42);
  event(EventKind::Sound, "Dropped resource 0x1 id %d", 7);
  event(EventKind::NativeCall, "Foo.bar()V");
  const std::string log = slurp(dir / "run.log");
  CHECK(log.find("f42] sound Dropped resource 0x1 id 7") != std::string::npos);
  CHECK(log.find("Foo.bar") == std::string::npos);

  Hub *hub = g_hub.load();
  CHECK(hub && hub->checkpoint);
  if (hub && hub->checkpoint) {
    hub->checkpoint(hub->context);
    CHECK(slurp(dir / "flight-1.log").find("checkpoint=1") !=
          std::string::npos);
    CHECK(slurp(dir / "flight-1.log").find("Foo.bar()V") != std::string::npos);
    event(EventKind::NativeCall, "after first checkpoint");
    hub->checkpoint(hub->context);
    CHECK(slurp(dir / "flight-0.log").find("checkpoint=2") !=
          std::string::npos);
    hub->checkpoint(hub->context);
    CHECK(slurp(dir / "flight-1.log").find("checkpoint=3") !=
          std::string::npos);
    CHECK(slurp(dir / "flight-0.log").find("checkpoint=2") !=
          std::string::npos);
    CHECK(!fs::exists(dir / "flight.tmp"));
  }

  recorder->finish(3);
  const std::string result = slurp(dir / "result.txt");
  CHECK(result.find("status=failed\n") != std::string::npos);
  CHECK(result.find("exit_code=3\n") != std::string::npos);
  CHECK(result.find("frames=42\n") != std::string::npos);
  CHECK(result.find("signal=none\n") != std::string::npos);
  const std::string after = slurp(dir / "run.log");
  CHECK(after.find("--- last events ---") != std::string::npos);
  CHECK(after.find("Foo.bar()V") != std::string::npos);
  recorder.reset();
  CHECK(!active());

  auto ok = RunRecorder::start(options, error);
  CHECK(ok != nullptr);
  if (ok) {
    const fs::path okDir = ok->dir();
    ok->finish(0);
    CHECK(slurp(okDir / "result.txt").find("status=ok\n") != std::string::npos);
    CHECK(slurp(okDir / "run.log").find("--- last events ---") ==
          std::string::npos);
  }
}

static void retentionAndSave(const fs::path &base) {
  fs::create_directories(base / "data/save/game");
  std::ofstream(base / "data/save/game/options") << "original";
  fs::create_directories(base / "data/save/empty");
  for (int i = 0; i < 60; ++i) {
    char name[64];
    std::snprintf(name, sizeof name, "20260101T0000%02dZ-%d", i, 100 + i);
    fs::create_directories(base / "runs" / name);
    std::ofstream(base / "runs" / name / "result.txt")
        << (i % 2 ? "status=ok\n" : "status=failed\n");
  }
  fs::create_directories(base / "runs/20260101T010000Z-77");
  RunOptions options;
  options.baseDir = (base / "runs").string();
  options.dataRoot = (base / "data").string();
  options.captureStreams = false;
  options.installHandlers = false;
  std::string error;
  auto recorder = RunRecorder::start(options, error);
  CHECK(recorder != nullptr);
  if (!recorder)
    return;
  const fs::path dir = recorder->dir();
  const auto meta = slurp(dir / "meta.txt");
  CHECK(meta.find("process_start_unix: unavailable") == std::string::npos);
  CHECK(meta.find("process_start_unix: ") != std::string::npos);
  CHECK(meta.find("initial_save_snapshot: captured") != std::string::npos);
  CHECK(meta.find("build identity unavailable") != std::string::npos ||
        meta.find("source_identity: unavailable") != std::string::npos);
  CHECK(slurp(dir / "initial-save/game/options") == "original");
  CHECK(fs::is_directory(dir / "initial-save/empty"));
  CHECK(slurp(dir / "initial-save-manifest.txt").find("game/options") !=
        std::string::npos);
  CHECK((fs::status(dir).permissions() & fs::perms::group_all) ==
        fs::perms::none);
  CHECK((fs::status(dir).permissions() & fs::perms::others_all) ==
        fs::perms::none);
  std::ofstream(base / "data/save/game/options") << "changed";
  CHECK(slurp(dir / "initial-save/game/options") == "original");
  CHECK(fs::exists(base / "runs/20260101T000000Z-100"));
  CHECK(fs::exists(base / "runs/20260101T000001Z-101"));
  CHECK(fs::exists(base / "runs/20260101T010000Z-77"));
  recorder->finish(0);
  recorder.reset();
  fs::create_symlink(base / "data/save/game/options", base / "data/save/link");
  auto unsafe = RunRecorder::start(options, error);
  CHECK(unsafe != nullptr);
  if (unsafe) {
    CHECK(slurp(fs::path(unsafe->dir()) / "meta.txt")
              .find("initial_save_snapshot: unavailable: save contains a "
                    "symlink") != std::string::npos);
    CHECK(!fs::exists(fs::path(unsafe->dir()) / "initial-save"));
    unsafe->finish(0);
  }
  unsafe.reset();
  fs::remove(base / "data/save/link");
  std::ofstream(base / "data/save/large");
  fs::resize_file(base / "data/save/large", 16 * 1024 * 1024 + 1);
  auto large = RunRecorder::start(options, error);
  CHECK(large != nullptr);
  if (large) {
    CHECK(slurp(fs::path(large->dir()) / "meta.txt")
              .find("initial_save_snapshot: unavailable: save exceeds snapshot "
                    "limits") != std::string::npos);
    CHECK(!fs::exists(fs::path(large->dir()) / "initial-save"));
    large->finish(0);
  }
  large.reset();
  options.baseDir = "ref";
  CHECK(!RunRecorder::start(options, error));
  CHECK(error.find("outside ref") != std::string::npos);
}

static void sourceProvenance(const fs::path &base) {
  const auto root = base / "source";
  for (const auto *name : {"src", "tools", "tests"})
    fs::create_directories(root / name);
  std::ofstream(root / "CMakeLists.txt") << "project(sl_port LANGUAGES CXX)\n";
  std::ofstream(root / "src/a.cpp") << "first\n";
  std::ofstream(root / "src/ignored.o") << "binary build output";
  fs::create_symlink(root / "src/a.cpp", root / "src/link.cpp");
  RunOptions options;
  options.baseDir = (base / "runs").string();
  options.sourceRoot = root.string();
  options.captureStreams = false;
  options.installHandlers = false;
  std::string error;
  auto first = RunRecorder::start(options, error);
  CHECK(first != nullptr);
  if (!first)
    return;
  const auto firstDir = fs::path(first->dir());
  const auto firstMeta = slurp(firstDir / "meta.txt");
  const auto firstManifest = slurp(firstDir / "source-manifest.txt");
  CHECK(firstMeta.find("source_identity: captured; working tree observed at "
                       "launch; build identity unavailable") !=
        std::string::npos);
  CHECK(
      firstManifest ==
      "CMakeLists."
      "txt\te57378446a0ccbab67da9a6067fdeddb12f319dfcabd7345eee014c70968f6ad\n"
      "src/"
      "a."
      "cpp\tb640e840b19d378660b32fb51ae18d67dccb4a8596a29e7bd72c1b2ae5928f41"
      "\n");
  CHECK(
      firstMeta.find(
          "src_tree_sha256: "
          "ae8e51682f396728be8b3b52cdfac0375aa7074c3599de457abc7d5889a79839") !=
      std::string::npos);
  CHECK(firstManifest.find("ignored.o") == std::string::npos);
  CHECK(firstManifest.find("link.cpp") == std::string::npos);
  first->finish(0);
  first.reset();
  std::ofstream(root / "src/a.cpp") << "second\n";
  auto second = RunRecorder::start(options, error);
  CHECK(second != nullptr);
  if (!second)
    return;
  CHECK(slurp(fs::path(second->dir()) / "source-manifest.txt") !=
        firstManifest);
  second->finish(0);
  second.reset();
  std::ofstream(root / "CMakeLists.txt") << "wrong project\n";
  auto missing = RunRecorder::start(options, error);
  CHECK(missing != nullptr);
  if (missing) {
    const auto dir = fs::path(missing->dir());
    CHECK(
        slurp(dir / "meta.txt")
            .find(
                "source_identity: unavailable: source root marker mismatch") !=
        std::string::npos);
    CHECK(slurp(dir / "meta.txt").find("src_tree_sha256: unknown") !=
          std::string::npos);
    CHECK(!fs::exists(dir / "source-manifest.txt"));
    missing->finish(0);
  }
}

static void gameDataProvenance(const fs::path &base) {
  fs::create_directories(base / "game");
  const auto manifest = base / "manifest.json";
  std::ofstream(manifest) << "abc";
  RunOptions options;
  options.baseDir = (base / "runs").string();
  options.gameRoot = (base / "game").string();
  options.captureStreams = false;
  options.installHandlers = false;
  std::string error;
  unsetenv("SLRR_GAME_DATA_MANIFEST");
  unsetenv("SLRR_GAME_DATA_ROOT");
  unsetenv("SLRR_GAME_DATA_SHA256");
  unsetenv("SLRR_GAME_DATA_STATUS");
  auto bare = RunRecorder::start(options, error);
  CHECK(bare != nullptr);
  if (!bare)
    return;
  CHECK(slurp(fs::path(bare->dir()) / "meta.txt")
            .find("game_data_status: unavailable") != std::string::npos);
  bare->finish(0);
  bare.reset();
  setenv("SLRR_GAME_DATA_MANIFEST", manifest.c_str(), 1);
  setenv("SLRR_GAME_DATA_ROOT", options.gameRoot.c_str(), 1);
  setenv("SLRR_GAME_DATA_SHA256",
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 1);
  setenv("SLRR_GAME_DATA_STATUS", "complete", 1);
  auto recorded = RunRecorder::start(options, error);
  CHECK(recorded != nullptr);
  if (!recorded)
    return;
  const auto dir = fs::path(recorded->dir());
  const auto meta = slurp(dir / "meta.txt");
  CHECK(meta.find("game_data_status: launcher-reported complete") !=
        std::string::npos);
  CHECK(
      meta.find(
          "game_data_manifest_sha256: "
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") !=
      std::string::npos);
  CHECK(slurp(dir / "game-data-manifest.json") == "abc");
  recorded->finish(0);
  recorded.reset();
  std::ofstream(manifest) << "changed";
  auto changed = RunRecorder::start(options, error);
  CHECK(changed != nullptr);
  if (changed) {
    CHECK(slurp(fs::path(changed->dir()) / "meta.txt")
              .find("game_data_status: unavailable") != std::string::npos);
    CHECK(!fs::exists(fs::path(changed->dir()) / "game-data-manifest.json"));
    changed->finish(0);
  }
  unsetenv("SLRR_GAME_DATA_MANIFEST");
  unsetenv("SLRR_GAME_DATA_ROOT");
  unsetenv("SLRR_GAME_DATA_SHA256");
  unsetenv("SLRR_GAME_DATA_STATUS");
}

// A writer interrupted between claim and commit (a signal in the middle of
// formatting a step) must not surface as a half-written or stale line.
static void traceInterruptedWriteIsSkipped() {
  TraceChannel *c = traceChannel("trace-test-interrupt");
  CHECK(c != nullptr);
  tracePush(c, "{\"n\":0}");
  tracePush(c, "{\"n\":1}");
  // Interrupted writer: claimed and half filled, never committed.
  const uint64_t torn = traceClaim(c);
  std::memcpy(c->lines[torn & (kTraceCapacity - 1)], "{\"n\":2,\"par", 12);
  tracePush(c, "{\"n\":3}");
  CHECK(c->written() == 4);
  CHECK(std::string(c->line(0)) == "{\"n\":0}");
  CHECK(std::string(c->line(1)) == "{\"n\":1}");
  CHECK(c->line(torn) == nullptr);
  CHECK(std::string(c->line(3)) == "{\"n\":3}");
  CHECK(c->line(4) == nullptr); // never claimed
  // The slot is reused after a wraparound; a stale commit word must not make
  // the old index readable and the new line must be.
  for (size_t i = 0; i < kTraceCapacity; ++i)
    tracePush(c, "{\"n\":\"later\"}");
  CHECK(c->line(0) == nullptr);
  CHECK(c->line(torn) == nullptr);
  CHECK(std::string(c->line(c->written() - 1)) == "{\"n\":\"later\"}");
  // Committing the torn slot late (the interrupted writer resumes) after it
  // was already overwritten by a later claim must not resurrect index 2.
  traceCommit(c, torn);
  CHECK(c->line(torn) == nullptr);
}

static void traceNonFiniteBecomesNull() {
  char text[kTraceLineSize];
  std::snprintf(
      text, sizeof text,
      "{\"p\":[%.3f,%.3f,%.3f],\"v\":%.1f,\"e\":\"inf,nan\",\"w\":%.2f}",
      std::nan(""), -std::numeric_limits<double>::infinity(), 1.5,
      std::numeric_limits<double>::infinity(), -std::nan(""));
  traceSanitizeJson(text, sizeof text);
  CHECK(std::string(text) ==
        "{\"p\":[null,null,1.500],\"v\":null,\"e\":\"inf,nan\",\"w\":null}");
  char plain[] = "{\"a\":[1,2,3],\"name\":\"nanny\"}";
  traceSanitizeJson(plain, sizeof plain);
  CHECK(std::string(plain) == "{\"a\":[1,2,3],\"name\":\"nanny\"}");
}

int main() {
  const fs::path tmp = fs::temp_directory_path() /
                       ("slrr-run-log-test-" + std::to_string(getpid()));
  fs::remove_all(tmp);
  traceInterruptedWriteIsSkipped();
  traceNonFiniteBecomesNull();
  randomBurstKeepsHistory();
  ringWraparound();
  ringThreadsDoNotLoseSlots();
  truncationAndSanitizing();
  kindTable();
  hooksAreNoOpsWithoutRecorder();
  formatLineShape();
  recorderWritesFilesAndResult(tmp / "runs");
  retentionAndSave(tmp / "retention");
  sourceProvenance(tmp / "source-provenance");
  gameDataProvenance(tmp / "game-data");
  fs::remove_all(tmp);
  if (g_failures) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::puts("run log: ring wraparound, truncation, kinds, line format, "
            "result.txt, retention and save snapshot ok");
  return 0;
}
