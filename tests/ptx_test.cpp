#include "ptx.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main() {
	struct Case { const char* path; uint32_t width; uint32_t height; std::array<uint8_t, 4> first; std::array<uint8_t, 4> middle; };
	const Case cases[] = {
		{"ref/raw/frontend/textures/GPS_FrameRaceSetup.ptx", 1024, 1024, {14, 14, 14, 196}, {14, 14, 14, 255}},
		{"ref/raw/frontend/textures/empty.ptx", 32, 32, {150, 0, 25, 0}, {150, 0, 25, 0}},
		{"ref/raw/frontend/textures/buttons/Info_holder.ptx", 32, 32, {20, 20, 20, 153}, {27, 16, 22, 153}},
	};
	for (const auto& test : cases) {
		std::ifstream file(test.path, std::ios::binary);
		if (!file) return 1;
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
		PtxImage image{};
		PtxPixels pixels{};
		std::string error;
		if (!ptxParse(bytes.data(), bytes.size(), image, error) || image.width != test.width ||
			image.height != test.height || image.levels.size() != 1 || !image.levels[0].alphaSize ||
			!ptxDecode(bytes.data(), bytes.size(), image.levels[0], pixels, error) ||
			pixels.width != test.width || pixels.height != test.height ||
			pixels.rgba.size() != size_t(test.width) * test.height * 4) {
			std::fprintf(stderr, "%s: %s\n", test.path, error.c_str());
			return 1;
		}
		const size_t middle = (size_t(test.width) * test.height / 2) * 4;
		for (size_t i = 0; i < 4; ++i)
			if (pixels.rgba[i] != test.first[i] || pixels.rgba[middle + i] != test.middle[i]) return 1;
		std::printf("%s first=%u,%u,%u,%u middle=%u,%u,%u,%u\n", test.path,
			pixels.rgba[0], pixels.rgba[1], pixels.rgba[2], pixels.rgba[3],
			pixels.rgba[middle], pixels.rgba[middle + 1], pixels.rgba[middle + 2], pixels.rgba[middle + 3]);
		const PtxLevel level = image.levels[0];
		bytes[level.colorOffset] = 0;
		if (ptxDecode(bytes.data(), bytes.size(), level, pixels, error)) return 1;
		bytes[level.colorOffset] = 0xff;
		bytes[level.alphaOffset] = 0;
		if (ptxDecode(bytes.data(), bytes.size(), level, pixels, error)) return 1;
		bytes[level.alphaOffset] = 0xff;
		if (ptxParse(bytes.data(), bytes.size() - 1, image, error)) return 1;
		bytes[16] = 0xff;
		bytes[17] = 0xff;
		bytes[18] = 0xff;
		bytes[19] = 0xff;
		if (ptxParse(bytes.data(), bytes.size(), image, error)) return 1;
	}
	PtxImage image{};
	PtxPixels pixels{};
	std::string error;
	const uint8_t shortFile[] = {0, 0, 0, 0};
	if (ptxParse(shortFile, sizeof(shortFile), image, error)) return 1;
	PtxLevel bad{0, 4, 4, 1};
	if (ptxDecode(shortFile, sizeof(shortFile), bad, pixels, error)) return 1;
	PtxLevel noAlpha{0, 4, 4, 0};
	if (ptxDecode(shortFile, sizeof(shortFile), noAlpha, pixels, error)) return 1;
	std::ifstream plain("ref/raw/Data/Textures/sedo.ptx", std::ios::binary);
	if (!plain) return 1;
	std::vector<uint8_t> plainBytes((std::istreambuf_iterator<char>(plain)), {});
	if (!ptxParse(plainBytes.data(), plainBytes.size(), image, error) || image.levels.size() != 5 ||
	    image.levels.back().alphaSize != 0 ||
	    !ptxDecode(plainBytes.data(), plainBytes.size(), image.levels.back(), pixels, error) ||
	    pixels.width != 64 || pixels.height != 64 || pixels.rgba[0] != 1 || pixels.rgba[3] != 255 ||
	    pixels.rgba[64 * 32 * 4] != 3 || pixels.rgba[64 * 32 * 4 + 3] != 255) return 1;
	std::puts("PTX test: ok");
	return 0;
}
