#include "game_application.hpp"
#include <system_error>

namespace {
bool contains(const std::filesystem::path& parent,
              const std::filesystem::path& child) {
  auto p = parent.begin(), c = child.begin();
  for (; p != parent.end(); ++p, ++c)
    if (c == child.end() || *p != *c)
      return false;
  return true;
}
} // namespace

bool prepareGameApplicationPaths(const std::filesystem::path& resources,
                                 const std::filesystem::path& userData,
                                 GameApplicationPaths& out, std::string& error) {
  namespace fs = std::filesystem;
  out = {};
  error.clear();
  std::error_code ec;
  const auto bundle = fs::canonical(resources, ec);
  if (ec || !fs::is_directory(bundle, ec) || ec) {
    error = "application resources are missing";
    return false;
  }
  const auto game = fs::canonical(bundle / "Game", ec);
  if (ec || !contains(bundle, game) || !fs::is_regular_file(game / "system.rpk", ec) || ec) {
    error = "bundled game content is missing or escapes application resources";
    return false;
  }
  const auto cursors = fs::canonical(bundle / "StockCursorResources.exe", ec);
  if (ec || !contains(bundle, cursors) || !fs::is_regular_file(cursors, ec) || ec) {
    error = "bundled cursor resources are missing or escape application resources";
    return false;
  }
  if (userData.empty() || !userData.is_absolute()) {
    error = "an absolute per-user data directory is required";
    return false;
  }
  const auto data = fs::weakly_canonical(userData, ec);
  // Protect the entire .app, including Contents/MacOS, rather than only Game.
  const auto application = bundle.parent_path().parent_path();
  if (ec || contains(application, data) || contains(data, application)) {
    error = "per-user data must not overlap the application";
    return false;
  }
  const auto save = fs::weakly_canonical(data / "save/game", ec);
  if (ec || !contains(data, save)) {
    error = "save directory escapes per-user data";
    return false;
  }
  const auto logs = fs::weakly_canonical(data / "logs", ec);
  if (ec || !contains(data, logs)) {
    error = "log directory escapes per-user data";
    return false;
  }
  fs::create_directories(save, ec);
  if (!ec)
    fs::create_directories(logs, ec);
  if (ec) {
    error = "cannot prepare per-user state: " + ec.message();
    return false;
  }
  out = {game, data, logs, cursors};
  return true;
}
