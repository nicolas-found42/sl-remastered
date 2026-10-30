#include "platform.hpp"
#include "render2d.hpp"
#include "resource_texture.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
	if (argc != 2 && argc != 4) {
		std::fputs("usage: loading_view <game-root> [--timeout-ms <milliseconds>]\n", stderr); return 2;
	}
	unsigned long timeout = 0;
	if (argc == 4) {
		char* end = nullptr;
		timeout = std::strtoul(argv[3], &end, 10);
		if (std::string(argv[2]) != "--timeout-ms" || argv[3][0] == '-' || end == argv[3] || *end || !timeout || timeout > 60000) {
			std::fputs("invalid timeout\n", stderr); return 2;
		}
	}
	const std::filesystem::path root = argv[1];
	std::ifstream file(root / "frontend.rpk", std::ios::binary | std::ios::ate);
	if (!file) { std::fputs("cannot open frontend.rpk\n", stderr); return 1; }
	const auto length = file.tellg();
	if (length <= 0 || length > 256 * 1024 * 1024) { std::fputs("invalid frontend.rpk size\n", stderr); return 1; }
	std::vector<uint8_t> pack(static_cast<size_t>(length));
	file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(pack.data()), length)) { std::fputs("cannot read frontend.rpk\n", stderr); return 1; }
	std::string error;
	ResourceTexturePixels pixels;
	if (!resourceTextureLoad(pack, 0xa8, root, pixels, error)) { std::fprintf(stderr, "texture: %s\n", error.c_str()); return 1; }
	auto platform = Platform::create("Loading picture diagnostic", 800, 700, error);
	if (!platform) { std::fprintf(stderr, "window: %s\n", error.c_str()); return 1; }
	auto renderer = Render2D::forWindow(platform->window(), error);
	if (!renderer) { std::fprintf(stderr, "renderer: %s\n", error.c_str()); return 1; }
	const auto texture = renderer->texture(pixels.width, pixels.height, pixels.rgba.data(), pixels.rgba.size(), error);
	if (!texture) { std::fprintf(stderr, "texture: %s\n", error.c_str()); return 1; }
	const Uint64 start = SDL_GetTicks();
	bool running = true;
	while (running && (!timeout || SDL_GetTicks() - start < timeout)) {
		for (const auto& event : platform->poll()) {
			if (std::holds_alternative<PlatformQuit>(event)) running = false;
			if (const auto* key = std::get_if<PlatformKey>(&event))
				if (key->down && key->scancode == SDL_SCANCODE_ESCAPE) running = false;
		}
		if (!running) break;
		int width = 0, height = 0;
		if (!renderer->outputSize(width, height, error)) { std::fprintf(stderr, "output: %s\n", error.c_str()); return 1; }
		if (width > 0 && height > 0) {
			const float scale = std::min(static_cast<float>(width) / pixels.width, static_cast<float>(height) / pixels.height);
			const float w = pixels.width * scale, h = pixels.height * scale;
			if (!renderer->clear(0, 0, 0, 255, error) ||
			    !renderer->draw(*texture, {(width - w) / 2, (height - h) / 2, w, h}, error) || !renderer->present(error)) {
				std::fprintf(stderr, "draw: %s\n", error.c_str()); return 1;
			}
		}
		SDL_Delay(16);
	}
	return 0;
}
