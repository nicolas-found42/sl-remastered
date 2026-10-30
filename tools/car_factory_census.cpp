#include "car_runtime.hpp"
#include "car_physics.hpp"
#include "game_creation.hpp"
#include "resource_world.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::string key(std::string s) {
  for (char& c : s) {
    if (c == '\\') c = '/';
    else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}
std::string cell(std::string s) {
  for (char& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return s;
}
struct Source {
  std::string pack;
  uint32_t id;
  std::string alias;
  std::string config;
};
struct Config {
  std::string path;
  uintmax_t size = 0;
  bool parsed = false;
  CarConfig data;
  std::string error;
};
}

int main(int argc, char** argv) {
  bool require = false;
  bool requirePhysics = false;
  std::string root;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--require-prototypes") require = true;
    else if (std::string(argv[i]) == "--require-physics") require = requirePhysics = true;
    else if (root.empty()) root = argv[i];
    else { std::cerr << "usage: car_factory_census [--require-prototypes] GAME_ROOT\n"; return 2; }
  }
  if (root.empty()) { std::cerr << "usage: car_factory_census [--require-prototypes] GAME_ROOT\n"; return 2; }
  std::error_code ec;
  fs::path base = fs::canonical(root, ec);
  if (ec || !fs::is_directory(base)) { std::cerr << "invalid game root\n"; return 2; }
  std::vector<fs::path> paths;
  for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_regular_file()) paths.push_back(it->path());
  if (ec) { std::cerr << "cannot scan game root: " << ec.message() << '\n'; return 2; }
  std::sort(paths.begin(), paths.end());
  PackRegistry packs(base);
  GameWorld world;
  if (!world.addRoot()) { std::cerr << "cannot create world root\n"; return 2; }
  ResourceWorld resources(world, packs);
  std::map<std::string, Config> configs;
  std::vector<Source> sources;
  size_t packErrors = 0;
  for (const auto& path : paths) {
    const std::string relative = fs::relative(path, base).generic_string();
    const std::string name = key(path.filename().string());
    if (key(relative).rfind("cars/", 0) == 0 && name.rfind("_main", 0) == 0 && path.extension() == ".cfg") {
      Config cfg;
      cfg.path = relative;
      cfg.size = fs::file_size(path, ec);
      if (ec) { std::cerr << "cannot stat " << relative << ": " << ec.message() << '\n'; return 2; }
      std::ifstream input(path, std::ios::binary);
      std::string text((std::istreambuf_iterator<char>(input)), {});
      if (!input || input.bad()) cfg.error = "cannot read config";
      else cfg.parsed = parseCarConfig(text, cfg.data, cfg.error);
      if (!configs.emplace(key(relative), std::move(cfg)).second) {
        std::cerr << "case-insensitive config path collision: " << relative << '\n'; return 2;
      }
    }
    if (key(path.extension().string()) != ".rpk") continue;
    uint16_t slot = 0;
    std::string error;
    PackView view;
    if (!packs.registerPackName(relative, slot, error) || packs.view(slot, view, error) != PackViewStatus::Ok) {
      std::cerr << "pack scan gap: " << relative << ": " << error << '\n';
      ++packErrors;
      continue;
    }
    for (const auto& entry : view.pack->entries) {
      if (entry.resType != 8 || (entry.typeId >> 16) != 0 || !(entry.typeId & 0xffff)) continue;
      const size_t offset = entry.payloadOffset, size = entry.payloadSize;
      if (offset > view.bytes->size() || size > view.bytes->size() - offset) continue;
      std::string payload(view.bytes->begin() + offset, view.bytes->begin() + offset + size);
      if (payload.find('\0') != std::string::npos) continue;
      std::istringstream lines(payload);
      std::string line;
      while (std::getline(lines, line)) {
        auto pos = line.find_first_of(" \t");
        if (pos == std::string::npos || key(line.substr(0, pos)) != "native") continue;
        auto directive = parseNativePayloadDirective(line.substr(pos + 1));
        if (directive && key(directive->device) == "car")
          sources.push_back({relative, (static_cast<uint32_t>(slot) << 16) | (entry.typeId & 0xffff), entry.alias,
                             directive->configPath.value_or("")});
      }
    }
  }
  for (const auto& source : sources) {
    if (source.config.empty() || configs.count(key(source.config))) continue;
    Config cfg;
    cfg.path = source.config;
    std::string text;
    if (!readNativeConfig(packs, source.config, text, cfg.error)) continue;
    cfg.size = text.size();
    cfg.parsed = parseCarConfig(text, cfg.data, cfg.error);
    configs.emplace(key(source.config), std::move(cfg));
  }
  size_t parsed = 0, complete = 0, physicsComplete = 0, physicsGaps = 0, gaps = packErrors;
  for (const auto& item : configs) if (item.second.parsed) ++parsed;
  std::cout << "# root\t" << cell(base.string()) << "\n# configs\t" << configs.size()
            << "\n# parsed_configs\t" << parsed << "\n# native_car_sources\t" << sources.size()
            << "\n# pack_scan_gaps\t" << packErrors << '\n';
  std::cout << "config_path\tbytes\tparsed\tpack\tsource_id\talias\tbindings\tgeometry\texpected_geometry\twheels\texpected_wheels\tstage_or_error\tdetached_physics\tphysics_error\n";
  std::map<std::string, size_t> matched;
  for (const auto& source : sources) {
    const auto configKey = key(source.config);
    auto found = configs.find(configKey);
    if (found == configs.end()) {
      ++gaps;
      std::cout << cell(source.config) << "\t\t\t" << cell(source.pack) << '\t' << source.id << '\t' << cell(source.alias)
                << "\t\t\t\t\t\t" << (source.config.empty() ? "native car has no config path" : "config is outside discovered corpus or missing") << "\t0\tnot attempted\n";
      continue;
    }
    ++matched[configKey];
    const Config& cfg = found->second;
    const size_t expectedGeometry = static_cast<size_t>(std::count_if(cfg.data.bodies.begin(), cfg.data.bodies.end(),
        [](const CarBodyRecord& body) { return body.kind == CarBodyRecord::Body; }));
    std::string error = cfg.error;
    std::unique_ptr<CarPrototype> prototype;
    if (cfg.parsed) {
      std::string text;
      if (readNativeConfig(packs, source.config, text, error)) {
        auto resolved = resources.lookup(source.id, 8);
        if (!resolved.node.script) error = "source lookup: " + resolved.detail;
        else {
          PackView view;
          if (packs.view(static_cast<uint16_t>(source.id >> 16), view, error) == PackViewStatus::Ok) {
            const ResourceIdMapping mapping{view.slot, static_cast<uint32_t>(packs.slotCount()), view.dependencies};
            loadCarPrototype(resources, *resolved.node.script, mapping, text, prototype, error);
          }
        }
      }
    }
    const size_t bindings = prototype ? prototype->resources.size() : 0;
    const size_t geometry = prototype ? prototype->geometry.size() : 0;
    const size_t wheels = prototype ? prototype->wheels.size() : 0;
    const bool ready = cfg.parsed && prototype && geometry == expectedGeometry && wheels == cfg.data.wheels.size()
        && prototype->stage == CarPrototypeStage::PhysicsGeometryReady;
    if (ready) ++complete; else ++gaps;
    std::string physicsError;
    auto physics = ready ? buildDetachedCarPhysics(*prototype, "0,0,0,0,0,0,0", 0,
        {0, physics::k::gravityY, 0}, physicsError) : nullptr;
    if (physics) ++physicsComplete; else ++physicsGaps;
    std::cout << cell(cfg.path) << '\t' << cfg.size << '\t' << cfg.parsed << '\t' << cell(source.pack) << '\t'
              << source.id << '\t' << cell(source.alias) << '\t' << bindings << '\t' << geometry << '\t'
              << expectedGeometry << '\t' << wheels << '\t' << cfg.data.wheels.size() << '\t' << cell(error)
              << '\t' << bool(physics) << '\t' << cell(physicsError) << '\n';
  }
  for (const auto& item : configs) if (!matched.count(item.first)) {
    ++gaps;
    const auto& cfg = item.second;
    std::cout << cell(cfg.path) << '\t' << cfg.size << '\t' << cfg.parsed << "\t\t\t\t\t\t\t\t\t"
              << (cfg.parsed ? "no explicit native car source mapping" : cell(cfg.error)) << "\t0\tnot attempted\n";
  }
  std::cerr << "geometry and wheel records constructed: " << complete << "; coverage or loading gaps: " << gaps
            << "; detached physics constructed: " << physicsComplete << "; physics gaps: " << physicsGaps << '\n';
  return require && (gaps || (requirePhysics && physicsGaps) || configs.empty() || sources.empty()) ? 1 : 0;
}
