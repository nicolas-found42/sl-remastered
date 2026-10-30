#include "dds.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

using Bytes = std::vector<uint8_t>;
static void put(Bytes& b, size_t offset, uint32_t value) {
	for (size_t i = 0; i < 4; ++i) b[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
	std::ifstream f("ref/raw/frontend/textures/Loading.dds", std::ios::binary);
	Bytes bytes((std::istreambuf_iterator<char>(f)), {});
	DdsImage image; std::string error;
	CHECK(ddsParseDxt1(bytes.data(), bytes.size(), image, error));
	CHECK(image.levels.size() == 1 && image.levels[0].width == 1024 && image.levels[0].height == 1024);
	CHECK(image.levels[0].offset == 128 && image.levels[0].size == 524288);
	CHECK(!ddsParseDxt1(bytes.data(), bytes.size() - 1, image, error) && image.levels.empty());
	Bytes mip(128 + 32 + 8 + 8, 0);
	put(mip, 0, 0x20534444); put(mip, 4, 124); put(mip, 12, 5); put(mip, 16, 7);
	put(mip, 28, 3); put(mip, 76, 32); put(mip, 80, 4); put(mip, 84, 0x31545844);
	CHECK(ddsParseDxt1(mip.data(), mip.size(), image, error));
	CHECK(image.levels.size() == 3 && image.levels[0].size == 32 && image.levels[1].size == 8 &&
	      image.levels[2].size == 8 && image.levels[2].offset == 168);
	put(mip, 28, 4); CHECK(!ddsParseDxt1(mip.data(), mip.size(), image, error));
	put(mip, 28, 1); put(mip, 112, 0x200); CHECK(!ddsParseDxt1(mip.data(), mip.size(), image, error));
	put(mip, 112, 0); put(mip, 84, 0x33545844); CHECK(!ddsParseDxt1(mip.data(), mip.size(), image, error));
	Bytes huge = mip;
	put(huge, 12, 0xffffffffu); put(huge, 16, 0xffffffffu);
	CHECK(!ddsParseCompressed(huge.data(), huge.size(), image, error) && image.levels.empty());
	DdsPixels pixels;
	Bytes block{0x00, 0xf8, 0x1f, 0x00, 0xe4, 0xe4, 0xe4, 0xe4};
	CHECK(ddsDecodeDxt1(block.data(), block.size(), {4, 4, 0, 8}, pixels, error));
	const Bytes opaque{255, 0, 0, 255, 0, 0, 255, 255, 170, 0, 85, 255, 85, 0, 170, 255};
	CHECK(std::equal(opaque.begin(), opaque.end(), pixels.rgba.begin()));
	block = {0x1f, 0x00, 0x00, 0xf8, 0xe4, 0xe4, 0xe4, 0xe4};
	CHECK(ddsDecodeDxt1(block.data(), block.size(), {3, 2, 0, 8}, pixels, error));
	const Bytes alpha{0, 0, 255, 255, 255, 0, 0, 255, 127, 0, 127, 255};
	CHECK(pixels.rgba.size() == 24 && std::equal(alpha.begin(), alpha.end(), pixels.rgba.begin()));
	CHECK(ddsDecodeDxt1(block.data(), block.size(), {4, 4, 0, 8}, pixels, error));
	CHECK(pixels.rgba[15] == 0 && pixels.rgba[12] == 0 && pixels.rgba[13] == 0 && pixels.rgba[14] == 0);
	block = {0x1f, 0x00, 0x1f, 0x00, 0xff, 0xff, 0xff, 0xff};
	CHECK(ddsDecodeDxt1(block.data(), block.size(), {4, 4, 0, 8}, pixels, error));
	CHECK(pixels.rgba[3] == 0);
	CHECK(!ddsDecodeDxt1(block.data(), 7, {4, 4, 0, 8}, pixels, error) && pixels.rgba.empty());
	CHECK(ddsParseDxt1(bytes.data(), bytes.size(), image, error));
	CHECK(ddsDecodeDxt1(bytes.data(), bytes.size(), image.levels[0], pixels, error));
	CHECK(pixels.rgba.size() == 1024u * 1024u * 4u);
	std::puts("DDS blocks and pixels: ok");
}
