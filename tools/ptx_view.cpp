#include "platform.hpp"
#include "ptx.hpp"
#include "resource_texture.hpp"
#include "render2d.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

static bool readFile(const std::string& path, std::vector<uint8_t>& bytes) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
	const auto length = file.tellg();
	if (length < 0 || length > 256 * 1024 * 1024) { std::fprintf(stderr, "invalid file size: %s\n", path.c_str()); return false; }
	bytes.resize(static_cast<size_t>(length));
	file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return false; }
	return true;
}

int main(int argc, char** argv) {
	const bool resource = argc > 1 && std::string(argv[1]) == "--resource";
	const int required = resource ? 5 : 2;
	if (argc != required && argc != required + 2) {
		std::fprintf(stderr, "usage: ptx_view <file.ptx> [--timeout-ms <milliseconds>]\n"
			"       ptx_view --resource <file.rpk> <local-id> <root> [--timeout-ms <milliseconds>]\n");
		return 2;
	}
	unsigned long timeout = 0;
	if (argc == required + 2) {
		char* end = nullptr;
		errno = 0;
		timeout = std::strtoul(argv[required + 1], &end, 10);
		if (std::string(argv[required]) != "--timeout-ms" || end == argv[required + 1] || *end || errno || timeout == 0 || timeout > 60000) {
			std::fputs("invalid timeout\n", stderr);
			return 2;
		}
	}
	std::string path = argv[1];
	if (resource) {
		char* end = nullptr;
		errno = 0;
		const unsigned long id = std::strtoul(argv[3], &end, 0);
		if (end == argv[3] || *end || errno || id > 0xffff || argv[3][0] == '-') {
			std::fputs("invalid local resource ID\n", stderr); return 2;
		}
		std::vector<uint8_t> pack;
		if (!readFile(argv[2], pack)) return 1;
		const auto result = resourceTextureLookup(pack, static_cast<uint16_t>(id), argv[4]);
		if (!result.ok) { std::fprintf(stderr, "resource: %s\n", result.error.c_str()); return 1; }
		path = result.path.string();
		std::printf("texture: %s\n", path.c_str());
	}
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes)) return 1;
	std::string error;
	PtxImage image{};
	PtxPixels pixels{};
	if (!ptxParse(bytes.data(), bytes.size(), image, error) ||
		!ptxDecode(bytes.data(), bytes.size(), image.levels.back(), pixels, error)) {
		std::fprintf(stderr, "PTX: %s\n", error.c_str());
		return 1;
	}
	auto platform = Platform::create("PTX diagnostic viewer", 800, 700, error);
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
			if (const auto* key = std::get_if<PlatformKey>(&event)) {
				if (key->down && key->scancode == SDL_SCANCODE_ESCAPE) running = false;
			}
		}
		if (!running) break;
		int width = 0, height = 0;
		if (!renderer->outputSize(width, height, error)) { std::fprintf(stderr, "output: %s\n", error.c_str()); return 1; }
		if (width > 0 && height > 0) {
			if (!renderer->clear(48, 48, 48, 255, error)) { std::fprintf(stderr, "clear: %s\n", error.c_str()); return 1; }
			const float scale = std::min(static_cast<float>(width) / pixels.width, static_cast<float>(height) / pixels.height);
			const float w = pixels.width * scale, h = pixels.height * scale;
			const RenderRect destination{(width - w) / 2, (height - h) / 2, w, h};
			if (!renderer->draw(*texture, destination, error) || !renderer->present(error)) {
				std::fprintf(stderr, "draw: %s\n", error.c_str()); return 1;
			}
		}
		SDL_Delay(16);
	}
	return 0;
}
