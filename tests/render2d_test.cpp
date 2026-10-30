#include "render2d.hpp"

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

int main() {
	using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
	Surface surface(SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
	if (!surface) return 1;
	std::string error;
	if (Render2D::forSurface(nullptr, error) || Render2D::forWindow(nullptr, error)) return 1;
	auto renderer = Render2D::forSurface(surface.get(), error);
	if (!renderer) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
	int width = 0, height = 0;
	if (!renderer->outputSize(width, height, error) || width != 4 || height != 4) return 1;
	const uint8_t pixels[] = {255, 0, 0, 255, 0, 255, 0, 0};
	if (renderer->texture(2, 1, pixels, sizeof(pixels) - 1, error) ||
		renderer->texture(0, 1, pixels, sizeof(pixels), error) ||
		renderer->texture(2, 1, nullptr, sizeof(pixels), error)) return 1;
	const auto texture = renderer->texture(2, 1, pixels, sizeof(pixels), error);
	if (!texture || renderer->draw(*texture + 1, {0, 0, 1, 1}, error) ||
		renderer->draw(*texture, {0, 0, 0, 1}, error) ||
		renderer->draw(*texture, {std::numeric_limits<float>::infinity(), 0, 1, 1}, error)) return 1;
	if (!renderer->clear(0, 0, 255, 255, error) || !renderer->draw(*texture, {1, 1, 2, 1}, error) ||
		!renderer->present(error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
	if (!SDL_LockSurface(surface.get())) return 1;
	const auto* data = static_cast<const uint8_t*>(surface->pixels);
	const auto match = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
		const auto* p = data + y * surface->pitch + x * 4;
		return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
	};
	const bool ok = match(1, 1, 255, 0, 0, 255) && match(2, 1, 0, 0, 255, 255) && match(0, 0, 0, 0, 255, 255);
	SDL_UnlockSurface(surface.get());
	if (!ok) return 1;
	std::puts("render2d pixels: ok");
	return 0;
}
