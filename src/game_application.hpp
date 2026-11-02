#pragma once
#include <filesystem>
#include <string>

// Immutable bundle content and mutable per-user state remain separate. Paths
// are owned values; the application retains no caller-owned storage.
struct GameApplicationPaths {
  std::filesystem::path gameRoot;
  std::filesystem::path dataRoot;
  std::filesystem::path logRoot;
  std::filesystem::path cursorResources;
};

// Resolve bundled content and prepare only the external writable directories.
// Existing saves are never copied, replaced or initialized from bundled saves.
bool prepareGameApplicationPaths(const std::filesystem::path& resources,
                                 const std::filesystem::path& userData,
                                 GameApplicationPaths& out, std::string& error);

// The diagnostic adapter and player application execute this same lifecycle.
// A player launch supplies prepared bundle paths and accepts no probe switches.
int runGameApplication(int argc, char** argv,
                       const GameApplicationPaths* playerPaths = nullptr);
