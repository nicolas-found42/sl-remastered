#include "ground_config.hpp"
#include "car_config.hpp"

#include <algorithm>
#include <cctype>

namespace {
GroundResource resource(uint32_t id) { return {(id & 0xffff0000u) == 0xffff0000u ? id & 0xffffu : id,
          (id & 0xffff0000u) == 0xffff0000u ? GroundResourceArchive::Particles : GroundResourceArchive::GroundPack}; }
}

bool parseGroundConfig(std::string_view text, float globalFriction, GroundConfig& out, std::string& error) {
  std::vector<CarConfigLine> lines;
  if (!tokenizeCarConfig(text, lines, error)) return false;
  GroundConfig result;
  std::vector<bool> selected;
  for (const auto& line : lines) {
    const auto fail = [&]() {
      error = "ground config line " + std::to_string(line.number) + ": invalid " + line.key;
      return false;
    };
    if (line.key == "materials") {
      auto scan = stockScan(line.args, "%d");
      if (scan.values.empty() || scan.values[0].i < 0 || scan.values[0].i > 100000) return fail();
      const auto count = static_cast<size_t>(scan.values[0].i);
      GroundMaterial material{"", globalFriction, 0.001f, 0.2f,
                              {2, GroundResourceArchive::Particles}, {10, GroundResourceArchive::Particles}, 0x80000000u, {6, GroundResourceArchive::Sound}, 0.6f};
      result.materials.assign(count, material);
      selected.assign(count, false);
    } else if (line.key == "material") {
      auto scan = stockScan(line.args, "%d %31s");
      if (scan.values.size() != 2 || scan.values[1].s.empty()) return fail();
      std::fill(selected.begin(), selected.end(), false);
      const auto index = scan.values[0].i;
      if (index >= 0 && static_cast<size_t>(index) < result.materials.size()) {
        result.materials[static_cast<size_t>(index)].name = scan.values[1].s;
        selected[static_cast<size_t>(index)] = true;
      }
    } else if (line.key == "params") {
      auto scan = stockScan(line.args, "%31s");
      if (scan.values.empty() || scan.values[0].s.empty()) return fail();
      for (size_t i = 0; i < result.materials.size(); ++i)
        selected[i] = result.materials[i].name.size() == scan.values[0].s.size() &&
          std::equal(result.materials[i].name.begin(), result.materials[i].name.end(), scan.values[0].s.begin(),
            [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
    } else if (line.key == "friction" || line.key == "rollres" || line.key == "collcoeff") {
      auto scan = stockScan(line.args, "%f");
      if (scan.values.empty()) return fail();
      for (size_t i = 0; i < result.materials.size(); ++i) if (selected[i]) {
        if (line.key == "friction") result.materials[i].friction = scan.values[0].f * globalFriction;
        else if (line.key == "rollres") result.materials[i].rollingResistance = scan.values[0].f;
        else result.materials[i].collisionCoefficient = scan.values[0].f;
      }
    } else if (line.key == "skid_ps" || line.key == "skid_sm" || line.key == "skid_smcolor" || line.key == "skid_sfx") {
      auto scan = stockScan(line.args, line.key == "skid_sfx" ? "%x %f" : "%x");
      if (scan.values.empty()) return fail();
      uint32_t id = static_cast<uint32_t>(scan.values[0].i);
      for (size_t i = 0; i < result.materials.size(); ++i) if (selected[i]) {
        auto& m = result.materials[i];
        if (line.key == "skid_ps") m.skidParticles = resource(id);
        else if (line.key == "skid_sm") m.skidMark = resource(id);
        else if (line.key == "skid_smcolor") m.skidMarkColor = id;
        else { m.skidSound = resource(id); m.skidSoundScale = scan.values.size() > 1 ? scan.values[1].f : 1.0f; }
      }
    }
  }
  out = std::move(result);
  error.clear();
  return true;
}
