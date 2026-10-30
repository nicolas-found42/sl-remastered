#include "dds.hpp"

#include <algorithm>

namespace {
uint32_t word(const uint8_t* p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}

bool ddsParseCompressed(const uint8_t* data, size_t size, DdsImage& image, std::string& error) {
	image = {};
	const auto fail = [&](const char* message) { image = {}; error = message; return false; };
	if (!data || size < 128) return fail("truncated DDS header");
	if (word(data) != 0x20534444 || word(data + 4) != 124 || word(data + 76) != 32 ||
	    !(word(data + 80) & 4)) return fail("not a compressed DDS");
	uint32_t blockSize = 16;
	switch (word(data + 84)) {
	case 0x31545844: image.format = DdsImage::Format::Dxt1; blockSize = 8; break;
	case 0x33545844: image.format = DdsImage::Format::Dxt3; break;
	case 0x35545844: image.format = DdsImage::Format::Dxt5; break;
	default: return fail("unsupported DDS compression");
	}
	if ((word(data + 112) & 0x0020fe00) || (word(data + 8) & 0x800000))
		return fail("DDS cube maps and volumes are unsupported");
	uint32_t width = word(data + 16), height = word(data + 12);
	uint32_t count = std::max(word(data + 28), 1u);
	if (!width || !height) return fail("invalid DDS dimensions");
	uint32_t maxLevels = 1;
	for (uint32_t n = std::max(width, height); n > 1; n >>= 1) ++maxLevels;
	if (count > maxLevels) return fail("invalid DDS mip count");
	size_t offset = 128;
	for (uint32_t i = 0; i < count; ++i) {
		const uint64_t rowBytes = ((static_cast<uint64_t>(width) + 3) / 4) * blockSize;
		const uint64_t rows = (static_cast<uint64_t>(height) + 3) / 4;
		if (rows > (size - offset) / rowBytes) return fail("truncated DDS blocks");
		const uint64_t bytes = rowBytes * rows;
		image.levels.push_back({width, height, offset, static_cast<size_t>(bytes)});
		offset += static_cast<size_t>(bytes);
		width = std::max(width >> 1, 1u);
		height = std::max(height >> 1, 1u);
	}
	error.clear();
	return true;
}

bool ddsParseDxt1(const uint8_t* data, size_t size, DdsImage& image, std::string& error) {
	if (!ddsParseCompressed(data, size, image, error)) return false;
	if (image.format != DdsImage::Format::Dxt1) {
		image = {};
		error = "not a DXT1 DDS";
		return false;
	}
	return true;
}

bool ddsDecodeDxt1(const uint8_t* data, size_t size, const DdsLevel& level,
                   DdsPixels& pixels, std::string& error) {
	pixels = {};
	const auto fail = [&](const char* message) { error = message; return false; };
	if (!data || !level.width || !level.height || level.width > 16384 || level.height > 16384)
		return fail("invalid DXT1 dimensions");
	const uint64_t blocksWide = (static_cast<uint64_t>(level.width) + 3) / 4;
	const uint64_t blocksHigh = (static_cast<uint64_t>(level.height) + 3) / 4;
	const uint64_t bytes = blocksWide * blocksHigh * 8;
	if (level.offset > size || bytes != level.size || bytes > size - level.offset)
		return fail("invalid DXT1 block bounds");
	pixels.width = level.width;
	pixels.height = level.height;
	pixels.rgba.resize(static_cast<size_t>(level.width) * level.height * 4);
	for (uint32_t by = 0; by < blocksHigh; ++by) {
		for (uint32_t bx = 0; bx < blocksWide; ++bx) {
			const auto* block = data + level.offset + (static_cast<size_t>(by) * blocksWide + bx) * 8;
			const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
			const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
			uint8_t palette[4][4]{};
			const auto unpack = [&](unsigned index, uint16_t color) {
				const unsigned r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
				palette[index][0] = static_cast<uint8_t>((r << 3) | (r >> 2));
				palette[index][1] = static_cast<uint8_t>((g << 2) | (g >> 4));
				palette[index][2] = static_cast<uint8_t>((b << 3) | (b >> 2));
				palette[index][3] = 255;
			};
			unpack(0, c0); unpack(1, c1);
			for (unsigned channel = 0; channel < 3; ++channel) {
				if (c0 > c1) {
					palette[2][channel] = static_cast<uint8_t>((2 * palette[0][channel] + palette[1][channel]) / 3);
					palette[3][channel] = static_cast<uint8_t>((palette[0][channel] + 2 * palette[1][channel]) / 3);
				} else {
					palette[2][channel] = static_cast<uint8_t>((palette[0][channel] + palette[1][channel]) / 2);
				}
			}
			palette[2][3] = 255;
			if (c0 > c1) palette[3][3] = 255;
			const uint32_t indices = word(block + 4);
			for (uint32_t y = 0; y < 4 && by * 4 + y < level.height; ++y) {
				for (uint32_t x = 0; x < 4 && bx * 4 + x < level.width; ++x) {
					const unsigned index = (indices >> (2 * (y * 4 + x))) & 3;
					const size_t out = (static_cast<size_t>(by * 4 + y) * level.width + bx * 4 + x) * 4;
					std::copy_n(palette[index], 4, pixels.rgba.begin() + static_cast<std::ptrdiff_t>(out));
				}
			}
		}
	}
	error.clear();
	return true;
}
