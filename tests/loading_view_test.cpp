#include "resource_texture.hpp"
#include "render2d.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>

int main() {
	std::ifstream file("ref/raw/frontend.rpk", std::ios::binary);
	std::vector<uint8_t> pack((std::istreambuf_iterator<char>(file)), {});
	std::string error;
	ResourceTexturePixels pixels;
	if (!resourceTextureLoad(pack, 0xa8, "ref/raw", pixels, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
	using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
	Surface surface(SDL_CreateSurface(1024, 1024, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
	if (!surface) return 1;
	auto renderer = Render2D::forSurface(surface.get(), error);
	if (!renderer) return 1;
	auto texture = renderer->texture(pixels.width, pixels.height, pixels.rgba.data(), pixels.rgba.size(), error);
	if (!texture || !renderer->clear(0, 0, 0, 255, error) || !renderer->draw(*texture, {0, 0, 1024, 1024}, error) || !renderer->present(error)) return 1;
	if (!SDL_LockSurface(surface.get())) return 1;
	const auto* center = static_cast<const uint8_t*>(surface->pixels) + 512 * surface->pitch + 512 * 4;
	const bool ok = center[0] == 84 && center[1] == 82 && center[2] == 76 && center[3] == 255;
	SDL_UnlockSurface(surface.get());
	if (!ok) return 1;
	std::puts("loading picture drawn: ok");
}
