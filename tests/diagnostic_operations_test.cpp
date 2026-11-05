#include "file_state.hpp"
#include "run_log.hpp"
#include "vm.hpp"

#include <cassert>
#include <csignal>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string read(const fs::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), {});
}

static int record(const fs::path& root, const std::string& mode) {
  fs::create_directories(root / "reference");
  fs::create_directories(root / "fixture/save/game");
  runlog::RunOptions options;
  options.baseDir = (root / "runs").string();
  options.referenceRoot = (root / "reference").string();
  options.captureDeveloperSource = false;
  options.captureStreams = mode == "stream" || mode == "capture-fault";
  options.installHandlers = false;
  options.maxLogBytes = mode == "cap" ? 512 : 16 * 1024 * 1024;
  std::string error;
  const auto descriptorCount = [] {
    int count = 0;
    for (int fd = 0; fd < 256; ++fd)
      if (fcntl(fd, F_GETFD) >= 0) ++count;
    return count;
  };
  std::vector<int> fillerDescriptors;
  rlimit originalDescriptors{};
  if (mode == "capture-fault") {
    assert(getrlimit(RLIMIT_NOFILE, &originalDescriptors) == 0);
    int highest = 2;
    for (int fd = 0; fd < 256; ++fd)
      if (fcntl(fd, F_GETFD) >= 0) highest = fd;
    // Close holes in the inherited descriptor set so the failure occurs after
    // the first capture pipe/dup, independently of the guardian's FD numbers.
    for (;;) {
      const int fd = ::open("/dev/null", O_RDONLY);
      assert(fd >= 0);
      fillerDescriptors.push_back(fd);
      if (fd > highest) {
        highest = fd;
        break;
      }
    }
    rlimit restricted = originalDescriptors;
    restricted.rlim_cur = static_cast<rlim_t>(highest + 6);
    assert(setrlimit(RLIMIT_NOFILE, &restricted) == 0);
  }
  const int beforeDescriptors = descriptorCount();
  auto recorder = runlog::RunRecorder::start(options, error);
  if (mode == "capture-fault") {
    assert(setrlimit(RLIMIT_NOFILE, &originalDescriptors) == 0);
    assert(recorder && !error.empty());
    assert(descriptorCount() == beforeDescriptors + 2); // run.log and input.txt
    for (int fd : fillerDescriptors) ::close(fd);
  } else {
    assert(recorder && error.empty());
  }
  const fs::path directory(recorder->dir());
  std::cout << directory.string() << std::endl;

  if (mode == "io") {
    // Real disk-write refusal, confined to this process; application work below
    // must still succeed after the temporary diagnostic-store failure.
    rlimit before{};
    assert(getrlimit(RLIMIT_FSIZE, &before) == 0);
    const auto handler = std::signal(SIGXFSZ, SIG_IGN);
    rlimit blocked = before;
    blocked.rlim_cur = 0;
    assert(setrlimit(RLIMIT_FSIZE, &blocked) == 0);
    runlog::event(runlog::EventKind::Note, "diagnostic write must fail");
    assert(setrlimit(RLIMIT_FSIZE, &before) == 0);
    std::signal(SIGXFSZ, handler);
  }

  FileState files(root / "fixture", root / "reference");
  VmObject owner;
  assert(files.openRead(&owner, "save/game/private-player-token", error) == FileOpenResult::missing);
  assert(files.openWrite(&owner, "save/game/private-player-token", error) == FileOpenResult::opened);
  const int32_t expected = 77;
  assert(files.writeBytes(&owner, &expected, sizeof expected, error));
  assert(files.close(&owner).empty());
  assert(files.openRead(&owner, "save/game/private-player-token", error) == FileOpenResult::opened);
  int32_t observed = 0;
  assert(files.readInt(&owner, observed, error) && observed == expected);
  assert(files.close(&owner).empty());

  if (mode == "hold") {
    runlog::Operation interrupted(runlog::OperationKind::FileWrite);
    auto* hub = runlog::g_hub.load();
    hub->checkpoint(hub->context);
    std::cout << "ready-for-kill" << std::endl;
    for (;;) pause();
  }
  if (mode == "cap")
    for (int i = 0; i < 1000; ++i)
      runlog::event(runlog::EventKind::Note, "bounded diagnostic flood %d", i);
  if (mode == "stream") {
    std::fprintf(stderr, "startup probe: synthetic failure context\n");
    for (int i = 0; i < 1000; ++i) std::fprintf(stderr, "  context %d\n", i);
  }
  auto* hub = runlog::g_hub.load();
  hub->checkpoint(hub->context);
  assert(recorder->finish(0) == 0);
  const auto log = read(directory / "run.log");
  const auto health = read(directory / "telemetry.json");
  assert(log.find("private-player-token") == std::string::npos);
  assert(health.find("\"phase\":\"finished\"") != std::string::npos);
  if (mode == "cap") {
    assert(fs::file_size(directory / "run.log") <= 512);
    assert(health.find("\"dropped_lines\":0") == std::string::npos);
  } else {
    assert(log.find("op1 ") != std::string::npos);
    assert(log.find("\"r\":2") != std::string::npos);
    assert(log.find("\"r\":1") != std::string::npos);
  }
  if (mode == "io") assert(health.find("\"io_failures\":0") == std::string::npos);
  if (mode == "stream") {
    assert(health.find("\"dropped_context_lines\":0") == std::string::npos);
    std::cout << directory.string() << std::endl;
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--record")
    return record(fs::canonical(argv[2]), argv[3]);
  const auto root = fs::temp_directory_path() / ("sl-diagnostic-operations-" + std::to_string(getpid()));
  fs::create_directory(root);
  const int result = record(fs::canonical(root), "normal");
  fs::remove_all(root);
  return result;
}
