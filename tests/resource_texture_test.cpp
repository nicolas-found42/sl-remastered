#include "dds.hpp"
#include "ptx.hpp"
#include "resource_texture.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);                      \
      ++failures;                                                              \
    }                                                                          \
  } while (0)
using Bytes = std::vector<uint8_t>;

static void put32(Bytes &bytes, uint32_t n) {
  for (int i = 0; i < 4; ++i)
    bytes.push_back(static_cast<uint8_t>(n >> (i * 8)));
}

static Bytes fixture(const std::string &payload, unsigned count = 1,
                     uint8_t type = 7, uint32_t sizeExtra = 0) {
  Bytes bytes{'R', 'P', 'A', 'K'};
  put32(bytes, 0x200);
  put32(bytes, 0);
  put32(bytes, 0);
  put32(bytes, count * 23);
  put32(bytes, count);
  put32(bytes, 0);
  put32(bytes, 0);
  for (unsigned i = 0; i < count; ++i) {
    put32(bytes, 3);
    put32(bytes, 0x28);
    bytes.push_back(type);
    bytes.push_back(0);
    put32(bytes, 0);
    put32(bytes, 32 + count * 23);
    put32(bytes, static_cast<uint32_t>(payload.size()) + sizeExtra);
    bytes.push_back(0);
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
      CHECK(
          ptxDecode(data.data(), data.size(), image.levels[0], pixels, error));
      CHECK(pixels.width > 0 && pixels.height > 0 && !pixels.rgba.empty());
    }
  }
  ResourceTexturePixels loading{};
  std::string loadError;
  const auto loadingFile = resourceTextureLookup(pack, 0xa8, "ref/raw");
  CHECK(loadingFile.ok && loadingFile.path.filename() == "Loading.dds");
  CHECK(resourceTextureLoad(pack, 0xa8, "ref/raw", loading, loadError));
  CHECK(loading.authoredMipChain && loading.width == 1024 && loading.height == 1024 &&
        loading.rgba.size() == 1024u * 1024u * 4u);
  if (loading.rgba.size() == 1024u * 1024u * 4u) {
    const size_t center = (512u * 1024u + 512u) * 4u;
    CHECK(loading.rgba[center] == 84 && loading.rgba[center + 1] == 82 &&
          loading.rgba[center + 2] == 76 && loading.rgba[center + 3] == 255);
  }
  ResourceTexturePixels font{};
  CHECK(resourceTextureLoad(pack, 0x136, "ref/raw", font, loadError));
  CHECK(font.width == 256 && font.height == 128 &&
        font.rgba.size() == 256u * 128u * 4u);
  if (font.rgba.size() == 256u * 128u * 4u) {
    const size_t glyph = (37u * 256u + 84u) * 4u;
    CHECK(font.rgba[glyph] == 85 && font.rgba[glyph + 1] == 85 &&
          font.rgba[glyph + 2] == 85 && font.rgba[glyph + 3] == 223);
  }
  ResourceTexturePixels ptx{};
  CHECK(resourceTextureLoad(pack, 0x28, "ref/raw", ptx, loadError));
  CHECK(ptx.authoredMipChain && ptx.width > 0 && ptx.height > 0 && !ptx.rgba.empty() && ptx.lodBias &&
        *ptx.lodBias == 3.0f);
  if (result.ok) {
    std::ifstream asset(result.path, std::ios::binary);
    Bytes data((std::istreambuf_iterator<char>(asset)), {});
    PtxImage image{};
    CHECK(ptxParse(data.data(), data.size(), image, loadError));
    CHECK(!image.levels.empty() &&
          ptx.mipLevels.size() + 1 == image.levels.size());
    for (size_t i = 1; i < image.levels.size() && i <= ptx.mipLevels.size();
         ++i) {
      PtxPixels expected{};
      CHECK(ptxDecode(data.data(), data.size(), image.levels[i], expected,
                      loadError));
      CHECK(ptx.mipLevels[i - 1].width == expected.width &&
            ptx.mipLevels[i - 1].height == expected.height &&
            ptx.mipLevels[i - 1].rgba == expected.rgba);
    }
  }
  if (loadingFile.ok) {
    std::ifstream asset(loadingFile.path, std::ios::binary);
    Bytes data((std::istreambuf_iterator<char>(asset)), {});
    DdsImage image;
    CHECK(ddsParseDxt1(data.data(), data.size(), image, loadError));
    CHECK(!image.levels.empty() &&
          loading.mipLevels.size() + 1 == image.levels.size());
    for (size_t i = 1; i < image.levels.size() && i <= loading.mipLevels.size();
         ++i) {
      DdsPixels expected;
      CHECK(ddsDecodeDxt1(data.data(), data.size(), image.levels[i], expected,
                          loadError));
      CHECK(loading.mipLevels[i - 1].width == expected.width &&
            loading.mipLevels[i - 1].height == expected.height &&
            loading.mipLevels[i - 1].rgba == expected.rgba);
    }
  }
  CHECK(!resourceTextureLoad(pack, 0xffff, "ref/raw", loading, loadError) &&
        loading.rgba.empty());
  CHECK(!resourceTextureLookup(pack, 0xffff, "ref/raw").ok);
  CHECK(!resourceTextureLoad(
      fixture("sourcefile system/Scripts/render/Frontend.class\n"), 0x28,
      "ref/raw", loading, loadError));
  CHECK(loadError == "unsupported texture format");
  const std::string source =
      "sourcefile frontend\\textures\\buttons\\Info_holder.ptx\r\n";
  CHECK(resourceTextureLookup(fixture(source), 0x28, "ref/raw").ok);
  CHECK(!resourceTextureLookup(fixture(source, 2), 0x28, "ref/raw").ok);
  CHECK(!resourceTextureLookup(fixture(source, 1, 6), 0x28, "ref/raw").ok);
  CHECK(!resourceTextureLookup(fixture(source, 1, 7, 1), 0x28, "ref/raw").ok);
  CHECK(!resourceTextureLookup(fixture("lod_bias 3\n"), 0x28, "ref/raw").ok);
  CHECK(!resourceTextureLookup(fixture(source + "lod_bias garbage\n"), 0x28,
                               "ref/raw")
             .ok);
  const auto lod = resourceTextureLookup(
      fixture(source + "lod_bias 0.25\nlod_amp 0.5\nflags 0\n"), 0x28,
      "ref/raw");
  CHECK(lod.ok && lod.lodBias && *lod.lodBias == 0.25f && lod.lodAmplitude &&
        *lod.lodAmplitude == 0.5f);
  CHECK(
      resourceTextureLookup(fixture(source + "flags 1\n"), 0x28, "ref/raw").ok);
  CHECK(
      resourceTextureLookup(fixture(source + "flags 1.000\n"), 0x28, "ref/raw")
          .ok);
  CHECK(
      !resourceTextureLookup(fixture(source + "flags nope\n"), 0x28, "ref/raw")
           .ok);
  CHECK(!resourceTextureLookup(fixture(source + source), 0x28, "ref/raw").ok);
  CHECK(
      !resourceTextureLookup(
           fixture("sourcefile ../frontend/textures/buttons/Info_holder.ptx\n"),
           0x28, "ref/raw")
           .ok);
  CHECK(!resourceTextureLookup(fixture("sourcefile C:\\Info_holder.ptx\n"),
                               0x28, "ref/raw")
             .ok);
  CHECK(!resourceTextureLookup(fixture("sourcefile /etc/passwd\n"), 0x28,
                               "ref/raw")
             .ok);
  // Stock TGA textures: particles.rpk 0x15 is asphalt.tga (type 2, 32 bpp,
  // 30x30); 0x01 names Smoke.tga, which the install does not ship.
  std::ifstream particlesFile("ref/raw/particles.rpk", std::ios::binary);
  Bytes particlesPack((std::istreambuf_iterator<char>(particlesFile)), {});
  CHECK(!particlesPack.empty());
  const auto asphalt = resourceTextureLookup(particlesPack, 0x15, "ref/raw");
  CHECK(asphalt.ok && asphalt.path.filename() == "asphalt.tga");
  ResourceTexturePixels tga{};
  CHECK(resourceTextureLoad(particlesPack, 0x15, "ref/raw", tga, loadError));
  CHECK(!tga.authoredMipChain && tga.width == 30 && tga.height == 30 &&
        tga.rgba.size() == 30u * 30u * 4u);
  // Exercise origin conversion and packet bounds through the public loader.
  std::filesystem::path root;
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  for (int attempt = 0; attempt < 100 && root.empty(); ++attempt) {
    const auto candidate = std::filesystem::temp_directory_path() /
                           ("texture-test-" + std::to_string(stamp) + "-" +
                            std::to_string(attempt));
    std::error_code ec;
    if (std::filesystem::create_directory(candidate, ec))
      root = candidate;
  }
  CHECK(!root.empty());
  if (!root.empty()) {
    const auto syntheticPack = fixture("sourcefile image.tga\n");
    auto loadTga = [&](const Bytes &bytes) {
      std::ofstream out(root / "image.tga", std::ios::binary);
      out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
      out.close();
      return resourceTextureLoad(syntheticPack, 0x28, root, tga, loadError);
    };
    Bytes image(18, 0);
    image[2] = 2;
    image[12] = 2;
    image[14] = 2;
    image[16] = 24;
    image[17] = 0x10;
    image.insert(image.end(), {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    CHECK(loadTga(image));
    CHECK(tga.width == 2 && tga.height == 2);
    CHECK(tga.rgba == Bytes({255, 255, 255, 255, 255, 0, 0, 255, 0, 255, 0, 255,
                             0, 0, 255, 255}));
    image.resize(18);
    image[2] = 10;
    image[17] = 0x20;
    image.insert(image.end(), {0x81, 0, 0, 255, 0x01, 0, 255, 0, 255, 0, 0});
    CHECK(loadTga(image));
    CHECK(tga.rgba == Bytes({255, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0,
                             0, 255, 255}));
    image.resize(18);
    image.insert(image.end(), {0x84, 0, 0, 255});
    CHECK(!loadTga(image) && tga.rgba.empty());
    image[17] = 0x40;
    CHECK(!loadTga(image) && tga.rgba.empty());
    std::filesystem::remove_all(root);
  }
  CHECK(!resourceTextureLoad(particlesPack, 0x01, "ref/raw", tga, loadError) &&
        tga.rgba.empty());
  return failures ? 1 : 0;
}
