#include "game_application.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <limits.h>

int main(int argc, char** argv) {
  const auto fail = [](const std::string& error) {
    std::fprintf(stderr, "Street Legal Racing: %s\n", error.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Street Legal Racing", error.c_str(), nullptr);
    return 1;
  };
  const CFBundleRef bundle = CFBundleGetMainBundle();
  if (!bundle)
    return fail("Cannot locate the application bundle.");
  const CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(bundle);
  if (!resourcesURL)
    return fail("Cannot locate bundled game resources.");
  UInt8 resourcePath[PATH_MAX];
  const bool havePath = CFURLGetFileSystemRepresentation(resourcesURL, true, resourcePath, sizeof resourcePath);
  CFRelease(resourcesURL);
  if (!havePath)
    return fail("Cannot resolve bundled game resources.");
  char* preferencePath = SDL_GetPrefPath("Found42", "Street Legal Racing Redline");
  if (!preferencePath)
    return fail(std::string("Cannot locate per-user game data: ") + SDL_GetError());
  const std::filesystem::path userData(preferencePath);
  SDL_free(preferencePath);
  GameApplicationPaths paths;
  std::string error;
  if (!prepareGameApplicationPaths(reinterpret_cast<const char*>(resourcePath), userData, paths, error))
    return fail(error);
  return runGameApplication(argc, argv, &paths);
}
