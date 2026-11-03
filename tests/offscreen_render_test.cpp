#include "render2d.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <unistd.h>

int main() {
  if (!SDL_SetHintWithPriority(SDL_HINT_MAC_BACKGROUND_APP, "1", SDL_HINT_OVERRIDE) ||
      !SDL_SetHintWithPriority(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0", SDL_HINT_OVERRIDE) ||
      !SDL_SetHintWithPriority(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0", SDL_HINT_OVERRIDE) ||
      !SDL_InitSubSystem(SDL_INIT_VIDEO)) return 1;
  struct Quit {
    ~Quit() { SDL_QuitSubSystem(SDL_INIT_VIDEO); }
  } quit;
  std::string directory = (std::filesystem::temp_directory_path() / "sl-offscreen-XXXXXX").string();
  if (!mkdtemp(directory.data())) return 1;
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::filesystem::remove_all(path); }
  } cleanup{directory};
  std::string error;
  auto renderer = Render2D::forOffscreen(64, 48, nullptr, error);
  if (!renderer) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  const uint8_t pixel[] = {255, 0, 0, 255};
  auto texture = renderer->texture(1, 1, pixel, sizeof(pixel), error);
  if (!texture) return 1;
  for (int frame = 0; frame < 3; ++frame) {
    const int width = 64 + frame * 16, height = 48 + frame * 8;
    int actualWidth = 0, actualHeight = 0;
    if (!renderer->presentation(width, height, error) ||
        !renderer->outputSize(actualWidth, actualHeight, error) ||
        actualWidth != width || actualHeight != height ||
        !renderer->clear(static_cast<uint8_t>(32 + frame * 32), 64, 128, 255, error) ||
        !renderer->draw(*texture, {2, 2, 8, 8}, error) || !renderer->present(error)) return 1;
    const auto shot = (std::filesystem::path(directory) / "frame.bmp").string();
    if (!renderer->saveFrame(shot, error)) return 1;
    auto* surface = SDL_LoadBMP(shot.c_str());
    if (!surface || surface->w != width || surface->h != height) return 1;
    Uint8 r = 0, g = 0, b = 0, a = 0;
    const bool background = SDL_ReadSurfacePixel(surface, 30, 30, &r, &g, &b, &a) &&
                            r == 32 + frame * 32 && g == 64 && b == 128 && a == 255;
    const bool foreground = SDL_ReadSurfacePixel(surface, 5, 5, &r, &g, &b, &a) &&
                            r == 255 && g == 0 && b == 0 && a == 255;
    SDL_DestroySurface(surface);
    if (!background || !foreground) return 1;
    float x = 0, y = 0, backX = 0, backY = 0;
    if (!renderer->windowToRender(11, 13, x, y) ||
        !renderer->renderToWindow(x, y, backX, backY) || backX != 11 || backY != 13) return 1;
  }
  int count = 0;
  auto** windows = SDL_GetWindows(&count);
  SDL_free(windows);
  if (count != 0) return 1;
  std::puts("offscreen GPU: repeated resized frames, texture preservation, pixel capture and no windows: ok");
  return 0;
}
