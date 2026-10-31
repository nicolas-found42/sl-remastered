#include "ptx.hpp"
#include "resource_material.hpp"
#include "resource_texture.hpp"
#include "scx.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

static int failures = 0;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::printf("FAIL line %d: %s\n", __LINE__, #c);                         \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static ResourceMaterialResult fixture(const std::string &text,
                                      uint8_t type = 14) {
  std::vector<uint8_t> bytes(text.begin(), text.end());
  PackResource r{};
  r.packBytes = &bytes;
  r.entry.resType = type;
  r.entry.payloadSize = static_cast<uint32_t>(bytes.size());
  return resourceMaterialParse(r);
}
int main() {
  PackRegistry registry("ref/raw");
  std::string error;
  uint32_t id = 0;
  PackResource material{};
  CHECK(registry.resolveLiteral("frontend.rpk", 0xd7, id, error));
  CHECK(registry.lookup(id, material, error));
  const auto parsed = resourceMaterialParse(material);
  CHECK(parsed.ok && parsed.mesh == 0xd9 &&
        parsed.textures == std::vector<uint32_t>{0xaf} && parsed.flags == 4096);
  uint32_t meshId = 0, textureId = 0;
  PackResource mesh{}, texture{};
  CHECK(registry.resolveLiteral("frontend.rpk", parsed.mesh, meshId, error));
  CHECK(registry.resolveLiteral("frontend.rpk", parsed.textures[0], textureId,
                                error));
  CHECK(registry.lookup(meshId, mesh, error) && mesh.entry.resType == 5 &&
        mesh.entry.alias == "etalon_alpha");
  if (mesh.packBytes) {
    const std::string payload(
        mesh.packBytes->begin() + mesh.entry.payloadOffset,
        mesh.packBytes->begin() + mesh.entry.payloadOffset +
            mesh.entry.payloadSize);
    CHECK(payload ==
          "sourcefile frontend\\meshes\\etalon_negyzet_alpha.SCX\r\n");
    const auto model =
        scxLoadFile("ref/raw/frontend/meshes/etalon_negyzet_alpha.SCX");
    CHECK(model.ok && !model.model.meshes.empty());
  }
  CHECK(registry.lookup(textureId, texture, error) &&
        texture.entry.resType == 7);
  if (texture.packBytes) {
    const auto file =
        resourceTextureLookup(*texture.packBytes, 0xaf, "ref/raw");
    CHECK(file.ok && file.path.filename() == "empty.ptx");
    if (file.ok) {
      std::ifstream f(file.path, std::ios::binary);
      std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
      PtxImage image{};
      CHECK(ptxParse(data.data(), data.size(), image, error));
    }
  }
  const std::string valid = "mesh 0xD9\nflags 4096.000\ntexture 0xAF\n";
  CHECK(fixture(valid).ok);
  // particles.rpk materials name a particle script config.
  const auto particle =
      fixture("mesh 0x3\nflags 256.000\nparticle "
              "particles\\scripts\\particle_clouds\\spark.cfg\ntexture 0x4\n");
  CHECK(particle.ok && particle.mesh == 3 && particle.flags == 256 &&
        particle.textures == std::vector<uint32_t>{4} &&
        particle.particle == "particles\\scripts\\particle_clouds\\spark.cfg");
  CHECK(!fixture("mesh 0x3\nparticle a\nparticle b\n").ok);
  const auto skidmark =
      fixture("mesh 0x3\nflags 256.000\nskidmark "
              "particles\\scripts\\skids\\sliding\\asphalt.cfg\ntexture 0x9\n");
  CHECK(skidmark.ok &&
        skidmark.skidmark ==
            "particles\\scripts\\skids\\sliding\\asphalt.cfg" &&
        skidmark.textures == std::vector<uint32_t>{9});
  CHECK(!fixture("mesh 0x3\nskidmark a\nskidmark b\n").ok);
  const auto noFlags = fixture("mesh 0x53\ntexture 0x51\n");
  CHECK(noFlags.ok && noFlags.mesh == 0x53 &&
        noFlags.textures == std::vector<uint32_t>{0x51} && noFlags.flags == 0);
  PackResource carMaterial{};
  CHECK(registry.resolveLiteral("cars.rpk", 0x55, id, error));
  CHECK(registry.lookup(id, carMaterial, error));
  const auto car = resourceMaterialParse(carMaterial);
  CHECK(car.ok && car.mesh == 0x53 &&
        car.textures == std::vector<uint32_t>{0x51} && car.flags == 0);
  PackResource cityMaterial{};
  CHECK(registry.resolveLiteral("maps\\city.rpk", 0x10, id, error));
  CHECK(registry.lookup(id, cityMaterial, error));
  const auto city = resourceMaterialParse(cityMaterial);
  CHECK(city.ok && city.mesh == 0xeae && city.flags == 2048 &&
        city.textures.size() == 95);
  if (city.textures.size() == 95) {
    CHECK(city.textures.front() == 0x40069 && city.textures[42] == 0x22f &&
          city.textures.back() == 0x4006d);
  }
  const std::string multi = "mesh 0x1\ntexture 0x2\ntexture 0x3\n";
  CHECK(fixture(multi + std::string(1, '\0')).textures ==
        (std::vector<uint32_t>{2, 3}));
  CHECK(!fixture(multi + std::string(1, '\0') + "texture 0x4\n").ok);
  const auto nullSlot = fixture(multi + "texture 0\n");
  CHECK(nullSlot.ok && nullSlot.textures == (std::vector<uint32_t>{2, 3, 0}));
  CHECK(!fixture(valid, 7).ok);
  const auto untextured = fixture("mesh 0xD9\nflags 4096.000\n");
  CHECK(untextured.ok && untextured.mesh == 0xd9 && untextured.flags == 4096 &&
        untextured.textures.empty());
  CHECK(!fixture(valid + "mesh 0xD9\n").ok);
  CHECK(!fixture("mesh 0x100000000\nflags 0\ntexture 0xAF\n").ok);
  const auto fractionalFlags =
      fixture("mesh 0xD9\nflags 4096.1\ntexture 0xAF\n");
  CHECK(fractionalFlags.ok && fractionalFlags.flags == 4096);
  const auto hexFlags = fixture("mesh 0xD9\nflags 0x1000\n");
  CHECK(hexFlags.ok && hexFlags.flags == 4096);
  const auto negativeFlags = fixture("mesh 0xD9\nflags -1\n");
  CHECK(negativeFlags.ok && negativeFlags.flags == UINT32_MAX);
  CHECK(!fixture("mesh 0xD9\nflags .5\n").ok);
  const auto lodMaterial = fixture(valid + "lod_bias 3\nlod_amp 5.000\n");
  CHECK(lodMaterial.ok && lodMaterial.lodBias == 3 &&
        lodMaterial.lodAmplitude == 5);
  CHECK(!fixture(valid + "lod_amp nan\n").ok);
  CHECK(!fixture(valid + "lod_bias 3\nlod_bias 4\n").ok);
  PackResource bad = material;
  bad.entry.payloadOffset = UINT32_MAX;
  CHECK(!resourceMaterialParse(bad).ok);
  return failures ? 1 : 0;
}
