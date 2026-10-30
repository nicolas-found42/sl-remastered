#include "resource_texture.hpp"

#include <SDL3/SDL.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv) {
	if (argc != 2 || (std::string(argv[1]) != "--software" && std::string(argv[1]) != "--window")) {
		std::fputs("usage: geometry_backend_probe --software|--window\n", stderr); return 2;
	}
	std::ifstream file("ref/raw/frontend.rpk", std::ios::binary);
	std::vector<uint8_t> pack((std::istreambuf_iterator<char>(file)), {});
	ResourceTexturePixels pixels;
	std::string error;
	if (!resourceTextureLoad(pack, 0xa8, "ref/raw", pixels, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
	if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
	using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
	using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
	using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
	using Texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
	Surface surface(nullptr, SDL_DestroySurface);
	Window window(nullptr, SDL_DestroyWindow);
	const bool software = std::string(argv[1]) == "--software";
	if (software) surface.reset(SDL_CreateSurface(1024, 1024, SDL_PIXELFORMAT_RGBA32));
	else window.reset(SDL_CreateWindow("Geometry backend probe", 1024, 1024, 0));
	if ((!surface && software) || (!window && !software)) { std::fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
	Renderer renderer(software ? SDL_CreateSoftwareRenderer(surface.get()) : SDL_CreateRenderer(window.get(), nullptr), SDL_DestroyRenderer);
	if (!renderer) { std::fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
	Texture texture(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 1024, 1024), SDL_DestroyTexture);
	if (!texture || !SDL_UpdateTexture(texture.get(), nullptr, pixels.rgba.data(), 4096) ||
	    !SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND)) { std::fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
	const SDL_Vertex vertices[] = {
		{{0, 0}, {1, 1, 1, 1}, {0, 0}}, {{0, 1024}, {1, 1, 1, 1}, {0, 1}},
		{{1024, 0}, {1, 1, 1, 1}, {1, 0}}, {{1024, 1024}, {1, 1, 1, 1}, {1, 1}},
	};
	const int indices[] = {0, 1, 2, 3, 2, 1};
	SDL_ClearError();
	if (!SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255) || !SDL_RenderClear(renderer.get()) ||
	    !SDL_RenderGeometry(renderer.get(), texture.get(), vertices, 4, indices, 6)) {
		std::fprintf(stderr, "queue: %s\n", SDL_GetError()); return 1;
	}
	Surface readback(SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
	if (!readback) { std::fprintf(stderr, "readback: %s\n", SDL_GetError()); return 1; }
	const std::string renderError = SDL_GetError();
	uint8_t r, g, b, a;
	if (!SDL_ReadSurfacePixel(readback.get(), 512, 512, &r, &g, &b, &a)) return 1;
	std::printf("backend %s, center %u %u %u %u, SDL error: %s\n", SDL_GetRendererName(renderer.get()), r, g, b, a, renderError.c_str());
	return r == 84 && g == 82 && b == 76 && a == 255 && renderError.empty() ? 0 : 1;
}
