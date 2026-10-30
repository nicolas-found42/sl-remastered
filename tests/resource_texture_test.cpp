#include "resource_texture.hpp"
#include "ptx.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)
using Bytes = std::vector<uint8_t>;

static void put32(Bytes& bytes, uint32_t n) {
	for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(n >> (i * 8)));
}

static Bytes fixture(const std::string& payload, unsigned count = 1, uint8_t type = 7, uint32_t sizeExtra = 0) {
	Bytes bytes{'R', 'P', 'A', 'K'};
	put32(bytes, 0x200); put32(bytes, 0); put32(bytes, 0);
	put32(bytes, count * 23); put32(bytes, count); put32(bytes, 0); put32(bytes, 0);
	for (unsigned i = 0; i < count; ++i) {
		put32(bytes, 3); put32(bytes, 0x28); bytes.push_back(type); bytes.push_back(0);
		put32(bytes, 0); put32(bytes, 32 + count * 23);
		put32(bytes, static_cast<uint32_t>(payload.size()) + sizeExtra); bytes.push_back(0);
	}
	bytes.insert(bytes.end(), payload.begin(), payload.end());
	return bytes;
}

int main() {
	std::ifstream file("ref/raw/frontend.rpk", std::ios::binary);
	Bytes pack((std::istreambuf_iterator<char>(file)), {});
	CHECK(!pack.empty());
	const auto result = resourceTextureLookup(pack, 0x28, "ref/raw");
	CHECK(result.ok);
	if (result.ok) {
		CHECK(result.path.filename() == "Info_holder.ptx");
		CHECK(result.lodBias && *result.lodBias == 3.0f);
		std::ifstream asset(result.path, std::ios::binary);
		Bytes data((std::istreambuf_iterator<char>(asset)), {});
		PtxImage image{};
		std::string error;
		CHECK(ptxParse(data.data(), data.size(), image, error));
		if (!image.levels.empty()) {
			PtxPixels pixels{};
			CHECK(ptxDecode(data.data(), data.size(), image.levels[0], pixels, error));
			CHECK(pixels.width > 0 && pixels.height > 0 && !pixels.rgba.empty());
		}
	}
	ResourceTexturePixels loading{};
	std::string loadError;
	const auto loadingFile = resourceTextureLookup(pack, 0xa8, "ref/raw");
	CHECK(loadingFile.ok && loadingFile.path.filename() == "Loading.dds");
	CHECK(resourceTextureLoad(pack, 0xa8, "ref/raw", loading, loadError));
	CHECK(loading.width == 1024 && loading.height == 1024 && loading.rgba.size() == 1024u * 1024u * 4u);
	if (loading.rgba.size() == 1024u * 1024u * 4u) {
		const size_t center = (512u * 1024u + 512u) * 4u;
		CHECK(loading.rgba[center] == 84 && loading.rgba[center + 1] == 82 &&
		      loading.rgba[center + 2] == 76 && loading.rgba[center + 3] == 255);
	}
	ResourceTexturePixels font{};
	CHECK(resourceTextureLoad(pack, 0x136, "ref/raw", font, loadError));
	CHECK(font.width == 256 && font.height == 128 && font.rgba.size() == 256u * 128u * 4u);
	if (font.rgba.size() == 256u * 128u * 4u) {
		const size_t glyph = (37u * 256u + 84u) * 4u;
		CHECK(font.rgba[glyph] == 85 && font.rgba[glyph + 1] == 85 && font.rgba[glyph + 2] == 85 && font.rgba[glyph + 3] == 223);
	}
	ResourceTexturePixels ptx{};
	CHECK(resourceTextureLoad(pack, 0x28, "ref/raw", ptx, loadError));
	CHECK(ptx.width > 0 && ptx.height > 0 && !ptx.rgba.empty() && ptx.lodBias && *ptx.lodBias == 3.0f);
	CHECK(!resourceTextureLoad(pack, 0xffff, "ref/raw", loading, loadError) && loading.rgba.empty());
	CHECK(!resourceTextureLookup(pack, 0xffff, "ref/raw").ok);
	CHECK(!resourceTextureLoad(fixture("sourcefile system/Scripts/render/Frontend.class\n"),
	                           0x28, "ref/raw", loading, loadError));
	CHECK(loadError == "unsupported texture format");
	const std::string source = "sourcefile frontend\\textures\\buttons\\Info_holder.ptx\r\n";
	CHECK(resourceTextureLookup(fixture(source), 0x28, "ref/raw").ok);
	CHECK(!resourceTextureLookup(fixture(source, 2), 0x28, "ref/raw").ok);
	CHECK(!resourceTextureLookup(fixture(source, 1, 6), 0x28, "ref/raw").ok);
	CHECK(!resourceTextureLookup(fixture(source, 1, 7, 1), 0x28, "ref/raw").ok);
	CHECK(!resourceTextureLookup(fixture("lod_bias 3\n"), 0x28, "ref/raw").ok);
	CHECK(!resourceTextureLookup(fixture(source + "lod_bias garbage\n"), 0x28, "ref/raw").ok);
	const auto lod = resourceTextureLookup(fixture(source + "lod_bias 0.25\nlod_amp 0.5\nflags 0\n"), 0x28, "ref/raw");
	CHECK(lod.ok && lod.lodBias && *lod.lodBias == 0.25f && lod.lodAmplitude && *lod.lodAmplitude == 0.5f);
	CHECK(!resourceTextureLookup(fixture(source + "flags 1\n"), 0x28, "ref/raw").ok);
	CHECK(!resourceTextureLookup(fixture(source + source), 0x28, "ref/raw").ok);
	CHECK(!resourceTextureLookup(fixture("sourcefile ../frontend/textures/buttons/Info_holder.ptx\n"), 0x28, "ref/raw").ok);
	CHECK(!resourceTextureLookup(fixture("sourcefile C:\\Info_holder.ptx\n"), 0x28, "ref/raw").ok);
	CHECK(!resourceTextureLookup(fixture("sourcefile /etc/passwd\n"), 0x28, "ref/raw").ok);
	return failures ? 1 : 0;
}
