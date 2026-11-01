#define _DARWIN_C_SOURCE
#include "file_state.hpp"
#include "run_log.hpp"
#include "vm.hpp"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                       \
  do {                                                    \
    if (!(cond)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
      ++failures;                                         \
    }                                                     \
  } while (0)

int main() {
  char name[] = "/tmp/sl-file-state-XXXXXX";
  const char* temp = mkdtemp(name);
  CHECK(temp != nullptr);
  if (!temp) return 1;
  const std::filesystem::path root(temp);
  std::filesystem::create_directories(root / "save/game");
  std::filesystem::copy_file("ref/raw/save/game/options", root / "save/game/options");
  const std::vector<uint8_t> shortPayload{'S', 'D', 'A', 'T', 0, 1, 2, 0, 1, 2, 3,
                                          0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0};
  std::ofstream out(root / "save/game/short", std::ios::binary);
  out.write(reinterpret_cast<const char*>(shortPayload.data()), static_cast<std::streamsize>(shortPayload.size()));
  out.close();
  VmObject first, second, unopened;
  std::filesystem::create_directories(root / "assets/Dir");
  std::ofstream(root / "assets/z.txt") << "z";
  std::ofstream(root / "assets/A.TXT") << "a";
  FileState files(root, "ref", root);
  int32_t searchHandle = -1;
  std::optional<std::string> found;
  std::string searchError;
  CHECK(files.findFirst("assets/*.txt", 1, searchHandle, found, searchError));
  CHECK(searchHandle == 0 && found == "A.TXT");
  CHECK(files.findNext(searchHandle, 1) == "z.txt");
  CHECK(!files.findNext(searchHandle, 1));
  files.findClose(searchHandle);
  CHECK(files.findFirst("assets/*.*", 2, searchHandle, found, searchError));
  CHECK(searchHandle == 0 && found == "Dir");
  CHECK(!files.findNext(searchHandle, 2));
  files.findClose(searchHandle);
  CHECK(files.findFirst("assets/*.missing", 0, searchHandle, found, searchError));
  CHECK(searchHandle == -1 && !found);
  CHECK(!files.findFirst("assets/../../*", 0, searchHandle, found, searchError));
  // Stock resolves in-root "." / ".." (FUN_00487cb0 -> FUN_0054cf50);
  // a pattern escaping the root is still refused by the containment check.
  CHECK(files.findFirst("save/../save/game/o*", 0, searchHandle, found, searchError));
  CHECK(searchHandle != -1 && found == "options");
  files.findClose(searchHandle);
  std::string error;
  int32_t value = 0;
  float f = 0;
  CHECK(files.openRead(&first, "save/game/options", error) == FileOpenResult::opened);
  CHECK(files.openRead(&second, "save/game/options", error) == FileOpenResult::opened);
  CHECK(files.handle(&first) && files.handle(&second) && files.handle(&first) != files.handle(&second));
  CHECK(files.readInt(&first, value, error));
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof bits);
  CHECK(bits == 0xfedcba98u);
  CHECK(files.readInt(&first, value, error) && value == 12);
  CHECK(files.readInt(&second, value, error));
  std::memcpy(&bits, &value, sizeof bits);
  CHECK(bits == 0xfedcba98u);
  for (int i = 0; i < 4; ++i) CHECK(files.readInt(&first, value, error));
  CHECK(files.readFloat(&first, f, error));
  std::memcpy(&bits, &f, sizeof bits);
  CHECK(bits == 0x3f806166u);
  CHECK(files.openRead(&first, "save/game/options", error) == FileOpenResult::opened);
  CHECK(files.readInt(&first, value, error));
  std::memcpy(&bits, &value, sizeof bits);
  CHECK(bits == 0xfedcba98u);
  CHECK(files.openRead(&first, "save/game/missing", error) == FileOpenResult::missing);
  CHECK(!files.handle(&first));
  CHECK(!files.readInt(&first, value, error) && error == "file is not open");
  CHECK(files.readInt(&second, value, error) && value == 12);
  CHECK(files.openRead(&first, "save/../save/game/options", error) == FileOpenResult::opened);
  CHECK(files.openRead(&first, "save/game/missing", error) == FileOpenResult::missing);
  CHECK(files.openRead(nullptr, "save/game/options", error) == FileOpenResult::error);
  CHECK(!files.readInt(&unopened, value, error) && error == "file is not open");
  FileState invalidRoot("ref/raw", "ref");
  CHECK(invalidRoot.openRead(&first, "save/game/options", error) == FileOpenResult::error);
  CHECK(files.openRead(&first, "save/game/short", error) == FileOpenResult::opened);
  CHECK(!files.readInt(&first, value, error) && error == "truncated file payload");
  CHECK(!files.readFloat(&first, f, error) && error == "truncated file payload");
  files.close(&first);
  files.close(&first);
  files.close(&second);
  CHECK(!files.readInt(&second, value, error) && error == "file is not open");
  // Writable save paths cannot turn a symlink into a trusted save root.
  const auto isolated = root / "isolated";
  const auto game = root / "installed-game";
  const auto reference = root / "reference";
  std::filesystem::create_directories(isolated);
  std::filesystem::create_directories(game);
  std::filesystem::create_directories(reference);
  for (const auto& protectedRoot : {game, reference}) {
    for (const bool nested : {false, true}) {
      std::filesystem::remove_all(isolated / "save");
      if (nested) std::filesystem::create_directories(isolated / "save");
      std::filesystem::create_directory_symlink(protectedRoot, isolated / (nested ? "save/link" : "save"));
      const std::string prefix = nested ? "save/link/" : "save/";
      std::ofstream(protectedRoot / "victim") << "preserve";
      std::ofstream(isolated / "save/source") << "local";
      FileState guarded(isolated, reference, game);
      int32_t result = 0;
      CHECK(!guarded.deletePath(prefix + "victim", result, error));
      CHECK(!guarded.copyPath(prefix + "victim", "save/copied", result, error));
      CHECK(!guarded.movePath(prefix + "victim", "save/moved", result, error));
      CHECK(!guarded.copyPath("save/source", prefix + "victim", result, error));
      CHECK(!guarded.movePath("save/source", prefix + "victim", result, error));
      CHECK(guarded.openWrite(&first, prefix + "victim", error) == FileOpenResult::error);
      CHECK(!guarded.handle(&first));
      guarded.close(&first);
      std::ifstream preserved(protectedRoot / "victim");
      std::string content;
      preserved >> content;
      CHECK(content == "preserve");
      std::ifstream source(isolated / "save/source");
      source >> content;
      CHECK(content == "local");
      CHECK(!std::filesystem::exists(isolated / "save/copied"));
      CHECK(!std::filesystem::exists(isolated / "save/moved"));
    }
  }
  // Stock write mode resolves in-root "." / ".." like any other path
  // (FUN_00485440 -> FUN_0054caf0); escapes stay refused by containment.
  CHECK(files.openWrite(&first, "save/carrer/../esc-1/main", error) == FileOpenResult::opened);
  files.close(&first);
  CHECK(std::filesystem::exists(root / "save/esc-1/main"));
  CHECK(files.openWrite(&first, "save/../escape-write", error) == FileOpenResult::error);
  CHECK(!std::filesystem::exists(root / "escape-write"));
  // The writer descriptor is held from open to close: swapping the file for a
  // symlink mid-window cannot redirect the deferred write (stock holds its
  // CreateFileA handle the same way).
  {
    std::ofstream(root / "swap-victim.txt", std::ios::binary) << "preserve";
    CHECK(files.openWrite(&first, "save/game/swap", error) == FileOpenResult::opened);
    std::filesystem::remove(root / "save/game/swap");
    std::filesystem::create_symlink(root / "swap-victim.txt", root / "save/game/swap");
    CHECK(files.close(&first).empty());
    std::ifstream swapped(root / "swap-victim.txt");
    std::string content2;
    swapped >> content2;
    CHECK(content2 == "preserve");
  }
  // A leaf symlink whose target exists is resolved by weakly_canonical before
  // containment, so an in-root one is an ordinary save path.
  {
    std::ofstream(root / "save/game/link-target", std::ios::binary) << "x";
    std::filesystem::create_symlink("link-target",
                                    root / "save/game/link-alias");
    CHECK(files.openWrite(&first, "save/game/link-alias", error) ==
          FileOpenResult::opened);
    CHECK(files.close(&first).empty());
    std::ifstream followed(root / "save/game/link-target",
                           std::ios::binary);
    CHECK(followed.peek() == 'S');
  }
  // weakly_canonical only resolves components that exist, so a *dangling*
  // leaf symlink survives as its own name and passes containment. Stock
  // CreateFileA would follow it and create the target, which may sit outside
  // the save root; O_NOFOLLOW refuses that open instead (a deliberate,
  // fail-closed deviation from stock).
  {
    std::filesystem::create_directories(root / "outside-leaf");
    std::filesystem::create_symlink(root / "outside-leaf/created",
                                    root / "save/game/dangling");
    CHECK(files.openWrite(&first, "save/game/dangling", error) ==
          FileOpenResult::missing);
    CHECK(!files.handle(&first));
    CHECK(!std::filesystem::exists(root / "outside-leaf/created"));
  }
  // A failed close-time write is reported instead of silently truncating.
  // Run under RLIMIT_FSIZE=0 in a subprocess so the mandatory SIGXFSZ can
  // never kill the test harness.
  {
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
      struct rlimit zero{0, 0};
      setrlimit(RLIMIT_FSIZE, &zero);
      struct sigaction ignore{};
      ignore.sa_handler = SIG_IGN;
      sigaction(SIGXFSZ, &ignore, nullptr);
      std::string childError;
      FileState childState(root, "ref", root);
      VmObject childOwner;
      const auto opened =
          childState.openWrite(&childOwner, "save/game/full", childError);
      const auto failure = childState.close(&childOwner);
      // The text carries the stock log format: reason, then the save path.
      _exit(opened == FileOpenResult::opened &&
                    failure.find("save/game/full") != std::string::npos
                ? 0
                : 1);
    }
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
  // Reopening an owner that still has a writer retires it through close(), and
  // a failure there must reach the run log rather than be discarded. Verified
  // in a child so the hub stays test-local; the RLIMIT_FSIZE limit is what
  // makes the retired writer's flush fail.
  {
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
      struct rlimit zero{0, 0};
      setrlimit(RLIMIT_FSIZE, &zero);
      struct sigaction ignore{};
      ignore.sa_handler = SIG_IGN;
      sigaction(SIGXFSZ, &ignore, nullptr);
      runlog::Hub hub;
      runlog::g_hub.store(&hub, std::memory_order_release);
      std::string childError;
      FileState childState(root, "ref", root);
      VmObject childOwner;
      const auto first =
          childState.openWrite(&childOwner, "save/game/reopen-old", childError);
      // Reopen under the same owner: close() flushes the first writer and is
      // expected to fail, yet the reopen itself still succeeds.
      const auto second =
          childState.openWrite(&childOwner, "save/game/reopen-new", childError);
      bool logged = false;
      for (size_t i = 0; i < hub.ring.size(); ++i)
        if (hub.ring.at(i).kind ==
                static_cast<uint8_t>(runlog::EventKind::Error) &&
            std::strstr(hub.ring.at(i).text, "save/game/reopen-old") != nullptr)
          logged = true;
      runlog::g_hub.store(nullptr, std::memory_order_release);
      _exit(first == FileOpenResult::opened &&
                    second == FileOpenResult::opened && logged
                ? 0
                : 1);
    }
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
  // File.exists refuses a symlinked escape out of the save root, and FindFile
  // refuses one out of the data root (FindFile is rooted at the data root by
  // design, matching the stock game directory).
  {
    std::filesystem::create_directories(root / "outside-dir");
    std::ofstream(root / "outside-dir/secret", std::ios::binary) << "x";
    std::filesystem::create_directory_symlink(root / "outside-dir", root / "save/escape-link");
    bool existsResult = true;
    CHECK(!files.exists("save/escape-link/secret", existsResult, error));
    CHECK(files.findFirst("save/escape-link/*", 0, searchHandle, found, searchError));
    files.findClose(searchHandle);
    const auto outside = root.parent_path() / (root.filename().string() + "-outside");
    std::filesystem::create_directories(outside);
    std::ofstream(outside / "secret", std::ios::binary) << "x";
    std::filesystem::remove(root / "save/escape-link");
    std::filesystem::create_directory_symlink(outside, root / "save/escape-link");
    CHECK(!files.findFirst("save/escape-link/*", 0, searchHandle, found, searchError));
    std::filesystem::remove_all(outside);
  }
  // The writer holds exactly one descriptor while open and releases it on
  // close and on state destruction, so repeated open/close cannot leak.
  {
    const auto countFds = [] {
      int used = 0;
      for (int fd = 0; fd < 256; ++fd)
        if (fcntl(fd, F_GETFD) != -1)
          ++used;
      return used;
    };
    const int before = countFds();
    VmObject leakOwner;
    for (int i = 0; i < 16; ++i) {
      CHECK(files.openWrite(&leakOwner, "save/game/fds", error) ==
            FileOpenResult::opened);
      CHECK(countFds() == before + 1);
      CHECK(files.close(&leakOwner).empty());
      CHECK(countFds() == before);
    }
    // A writer still open when the state is destroyed must not leak either.
    {
      FileState scoped(root, "ref", root);
      CHECK(scoped.openWrite(&leakOwner, "save/game/scoped", error) ==
            FileOpenResult::opened);
      CHECK(countFds() == before + 1);
    }
    CHECK(countFds() == before);
  }
  // A refused open releases nothing and consumes no handle: a later valid open
  // still receives the next handle instead of a gap from the failed attempt.
  {
    VmObject refusedOwner;
    std::filesystem::create_directories(root / "outside-leaf2");
    std::filesystem::create_symlink(root / "outside-leaf2/made",
                                    root / "save/game/refused");
    CHECK(files.openWrite(&refusedOwner, "save/game/refused", error) ==
          FileOpenResult::missing);
    CHECK(!files.handle(&refusedOwner));
    CHECK(files.openWrite(&refusedOwner, "save/game/after-refusal", error) ==
          FileOpenResult::opened);
    CHECK(files.close(&refusedOwner).empty());
    CHECK(!std::filesystem::exists(root / "outside-leaf2/made"));
  }
  std::filesystem::remove_all(root);
  return failures ? 1 : 0;
}
