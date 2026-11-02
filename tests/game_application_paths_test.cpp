#include "game_application.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

int main() {
  namespace fs = std::filesystem;
  std::string temporary = (fs::temp_directory_path() / "sl-game-paths-XXXXXX").string();
  assert(mkdtemp(temporary.data()));
  const fs::path root(temporary), resources = root / "Example.app/Contents/Resources";
  const fs::path data = root / "Application Support/Game";
  fs::create_directories(resources / "Game");
  std::ofstream(resources / "Game/system.rpk") << "fixture content";
  std::ofstream(resources / "StockCursorResources.exe") << "fixture resource carrier";
  fs::create_directories(data / "save/game");
  std::ofstream(data / "save/game/career") << "existing progress";
  GameApplicationPaths paths;
  std::string error;
  assert(prepareGameApplicationPaths(resources, data, paths, error));
  assert(error.empty());
  assert(fs::is_directory(paths.logRoot));
  std::ifstream saved(paths.dataRoot / "save/game/career");
  assert(std::string(std::istreambuf_iterator<char>(saved), {}) == "existing progress");
  assert(fs::is_regular_file(paths.gameRoot / "system.rpk"));
  assert(fs::is_regular_file(paths.cursorResources));
  // Replacing the application leaves progress in the same external location.
  fs::remove_all(resources.parent_path().parent_path());
  fs::create_directories(resources / "Game");
  std::ofstream(resources / "Game/system.rpk") << "replacement content";
  std::ofstream(resources / "StockCursorResources.exe") << "replacement cursor carrier";
  assert(prepareGameApplicationPaths(resources, data, paths, error));
  std::ifstream replaced(paths.dataRoot / "save/game/career");
  assert(std::string(std::istreambuf_iterator<char>(replaced), {}) == "existing progress");

  assert(!prepareGameApplicationPaths(resources, resources / "user-data", paths, error));
  assert(!fs::exists(resources / "user-data"));
  assert(!prepareGameApplicationPaths(resources, root, paths, error));
  assert(!prepareGameApplicationPaths(resources, "relative-data", paths, error));

  const auto escaped = root / "escaped-save";
  fs::create_directories(escaped);
  fs::create_directories(root / "bad-user");
  fs::create_directory_symlink(escaped, root / "bad-user/save");
  assert(!prepareGameApplicationPaths(resources, root / "bad-user", paths, error));
  assert(!fs::exists(escaped / "game"));
  fs::create_directories(root / "bad-logs");
  fs::create_directory_symlink(escaped, root / "bad-logs/logs");
  assert(!prepareGameApplicationPaths(resources, root / "bad-logs", paths, error));
  assert(!fs::exists(root / "bad-logs/save"));

  fs::remove(resources / "StockCursorResources.exe");
  assert(!prepareGameApplicationPaths(resources, root / "missing-cursor-user", paths, error));
  assert(!fs::exists(root / "missing-cursor-user"));
  std::ofstream(resources / "StockCursorResources.exe") << "fixture resource carrier";
  fs::remove_all(resources / "Game");
  fs::create_directories(root / "outside-game");
  std::ofstream(root / "outside-game/system.rpk") << "outside content";
  fs::create_directory_symlink(root / "outside-game", resources / "Game");
  assert(!prepareGameApplicationPaths(resources, root / "outside-game-user", paths, error));
  assert(!fs::exists(root / "outside-game-user"));
  fs::remove_all(root);
}
