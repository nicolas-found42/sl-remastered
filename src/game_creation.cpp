#include "game_creation.hpp"
#include "car_physics.hpp"
#include "car_wheel_runtime.hpp"
#include "car_config.hpp"
#include "part_config.hpp"
#include "part_physics.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

bool readNativeDataFile(PackRegistry& packs, const std::string& rawPath,
                        size_t maximumBytes, std::string& contents,
                        std::string& error) {
  std::string path = rawPath;
  std::replace(path.begin(), path.end(), '\\', '/');
  const std::filesystem::path relative(path);
  if (relative.empty() || relative.is_absolute() ||
      path.find(':') != std::string::npos) {
    error = "unsafe native config path";
    return false;
  }
  for (const auto& component : relative)
    if (component == "." || component == ".." || component.empty()) {
      error = "unsafe native config path";
      return false;
    }
  std::error_code ec;
  const auto root = std::filesystem::canonical(packs.root(), ec);
  if (ec) {
    error = "native config root unavailable";
    return false;
  }
  const auto file = std::filesystem::canonical(root / relative, ec);
  if (ec) {
    error = "native config file unavailable";
    return false;
  }
  auto a = root.begin(), b = file.begin();
  for (; a != root.end() && b != file.end() && *a == *b; ++a, ++b) {
  }
  if (a != root.end() || !std::filesystem::is_regular_file(file, ec) || ec) {
    error = "unsafe native config path";
    return false;
  }
  const auto length = std::filesystem::file_size(file, ec);
  if (ec || length > maximumBytes) {
    error = "native data file size unsupported";
    return false;
  }
  std::ifstream input(file, std::ios::binary);
  if (!input) {
    error = "native config file unavailable";
    return false;
  }
  contents.assign(std::istreambuf_iterator<char>(input), {});
  if (input.bad()) {
    error = "native config read failed";
    return false;
  }
  error.clear();
  return true;
}

bool readNativeConfig(PackRegistry& packs, const std::string& rawPath,
                      std::string& contents, std::string& error) {
  return readNativeDataFile(packs, rawPath, 1024 * 1024, contents, error);
}

bool loadCarScriptPrototype(ResourceWorld& resources, ScriptNode& source,
                            const std::string& configPath, std::string& error,
                            const CarCameraSettings* cameraSettings) {
  if (source.car) {
    if (source.car->configPath == configPath &&
        source.car->stage == CarPrototypeStage::PhysicsGeometryReady) {
      error.clear();
      return true;
    }
    error = "car script prototype already names another config";
    return false;
  }
  std::string contents;
  if (!readNativeConfig(resources.packs(), configPath, contents, error)) {
    error += " (resource " + std::to_string(source.resourceId) + ", config " +
             configPath + ")";
    return false;
  }
  PackView view;
  if (resources.packs().view(static_cast<uint16_t>(source.resourceId >> 16),
                             view, error) != PackViewStatus::Ok)
    return false;
  const ResourceIdMapping mapping{
      view.slot, static_cast<uint32_t>(resources.packs().slotCount()),
      view.dependencies};
  std::unique_ptr<CarPrototype> prototype;
  const bool complete = loadCarPrototype(resources, source, mapping, contents,
                                         prototype, error, true);
  if (prototype &&
      prototype->stage == CarPrototypeStage::PhysicsGeometryReady) {
    if (cameraSettings)
      prototype->cameraSettings = *cameraSettings;
    prototype->configPath = configPath;
    source.car = std::move(prototype);
  }
  if (!complete)
    error += " (resource " + std::to_string(source.resourceId) + ", config " +
             configPath + ")";
  return complete;
}

namespace {
bool equalsIgnoreCase(const std::string& a, const char* b);
}

bool loadButtonScriptPrototype(ResourceWorld& resources, ScriptNode& source,
                               const std::string& configPath,
                               std::string& error) {
  if (source.button) {
    if (source.button->configPath == configPath) {
      error.clear();
      return true;
    }
    error = "button script prototype already names another config";
    return false;
  }
  std::string contents;
  if (configPath.empty() || configPath == "-") {
    error = "button factory needs a config path";
    return false;
  }
  if (!readNativeConfig(resources.packs(), configPath, contents, error)) {
    error += " (resource " + std::to_string(source.resourceId) + ", config " +
             configPath + ")";
    return false;
  }
  std::vector<PartConfigLine> lines;
  if (!parsePartConfig(contents, lines, error))
    return false;
  PackView view;
  if (resources.packs().view(static_cast<uint16_t>(source.resourceId >> 16),
                             view, error) != PackViewStatus::Ok)
    return false;
  const ResourceIdMapping mapping{
      view.slot, static_cast<uint32_t>(resources.packs().slotCount()),
      view.dependencies};
  auto proto = std::make_unique<ButtonPrototype>();
  proto->configPath = configPath;
  for (const auto& line : lines) {
    if (equalsIgnoreCase(line.key, "render")) {
      // FUN_00437250: "%d %f %f %f %f %f %f"; the id is remapped by
      // FUN_00544590 and the trailing floats stay zero when absent.
      const auto parsed = stockScan(line.arguments, "%d %f %f %f %f %f %f");
      proto->renderId = parsed.values.empty()
                            ? 0
                            : remapResourceId(
                                  static_cast<uint32_t>(parsed.values[0].i),
                                  mapping);
    } else if (equalsIgnoreCase(line.key, "body")) {
      // FUN_00437070: "%s %f %f %f"; the name is read again as "%d". Zero
      // names a primitive mesh body (FUN_0049ab60), non-zero a resource.
      const auto parsed = stockScan(line.arguments, "%s %f %f %f");
      proto->hasBody = true;
      if (!parsed.values.empty()) {
        const auto id = stockScan(parsed.values[0].s, "%d");
        if (!id.values.empty() && id.values[0].i != 0)
          proto->bodyId =
              remapResourceId(static_cast<uint32_t>(id.values[0].i), mapping);
        else
          proto->bodyName = parsed.values[0].s;
      }
    }
  }
  source.button = std::move(proto);
  error.clear();
  return true;
}

bool loadPartScriptPrototype(ResourceWorld& resources, ScriptNode& source,
                             const std::string& configPath,
                             std::string& error) {
  if (source.car) {
    if (source.car->configPath == configPath &&
        source.car->stage == CarPrototypeStage::PhysicsGeometryReady) {
      error.clear();
      return true;
    }
    error = "part script prototype already names another config";
    return false;
  }
  std::string contents;
  if (!configPath.empty() && configPath != "-") {
    if (!readNativeConfig(resources.packs(), configPath, contents, error)) {
      error += " (resource " + std::to_string(source.resourceId) + ", config " +
               configPath + ")";
      return false;
    }
  }
  PackView view;
  if (resources.packs().view(static_cast<uint16_t>(source.resourceId >> 16),
                             view, error) != PackViewStatus::Ok)
    return false;
  const ResourceIdMapping mapping{
      view.slot, static_cast<uint32_t>(resources.packs().slotCount()),
      view.dependencies};
  std::unique_ptr<CarPrototype> prototype;
  const bool complete = loadCarPrototype(resources, source, mapping, contents,
                                         prototype, error, false);
  if (prototype &&
      prototype->stage == CarPrototypeStage::PhysicsGeometryReady) {
    prototype->configPath = configPath;
    source.car = std::move(prototype);
  }
  if (!complete)
    error += " (resource " + std::to_string(source.resourceId) + ", config " +
             configPath + ")";
  return complete;
}

std::optional<NativePayloadDirective>
parseNativePayloadDirective(const std::string& text) {
  if (text.find('\0') != std::string::npos)
    return std::nullopt;
  const auto start = text.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return std::nullopt;
  const auto end = text.find_first_of(" \t\r\n", start);
  NativePayloadDirective result{text.substr(start, end - start), std::nullopt};
  if (end != std::string::npos) {
    const auto pathStart = text.find_first_not_of(" \t\r\n", end);
    if (pathStart != std::string::npos) {
      const auto pathEnd = text.find_first_of(" \t\r\n", pathStart);
      result.configPath = text.substr(pathStart, pathEnd - pathStart);
    }
  }
  return result;
}

std::optional<NativePayloadDirective>
readNativeOnlyGameType(PackRegistry& registry, uint32_t scriptResourceId,
                       std::string& error) {
  PackResource resource;
  if (!registry.lookup(scriptResourceId, resource, error))
    return std::nullopt;
  if (resource.entry.resType != 8 || !resource.packBytes ||
      resource.entry.payloadSize > 65536 ||
      resource.entry.payloadOffset > resource.packBytes->size() ||
      resource.entry.payloadSize >
          resource.packBytes->size() - resource.entry.payloadOffset) {
    error = "invalid native-only type-8 payload";
    return std::nullopt;
  }
  const auto begin = resource.packBytes->begin() + resource.entry.payloadOffset;
  std::string payload(begin, begin + resource.entry.payloadSize);
  if (!payload.empty() && payload.back() == '\n')
    payload.pop_back();
  if (!payload.empty() && payload.back() == '\r')
    payload.pop_back();
  if (payload.find_first_of("\r\n") != std::string::npos ||
      payload.compare(0, 7, "native ") != 0) {
    error = "type-8 payload is not a single native directive";
    return std::nullopt;
  }
  auto result = parseNativePayloadDirective(payload.substr(7));
  if (!result)
    error = "invalid native directive";
  else
    error.clear();
  return result;
}

GameInstanceDescriptor
prepareGameInstanceDescriptor(const GameCreationRequest& request) {
  const auto nul = request.name.find('\0');
  return {1, 0x21, request.name.substr(0, nul < 30 ? nul : 30)};
}

namespace {
// Stock's script-root table (strings at 0060ab78 through 0060ac3c) pairs each
// script directory with the Java package of the classes inside it.
// FUN_00410430 loads the class file from the root and requires the pair to
// match the class's own package, so root suffix and package agree.
struct ScriptRoot {
  const char* directory;
  const char* packageName;
};
const ScriptRoot kScriptRoots[] = {
    {"system\\scripts\\lang", "java.lang"},
    {"system\\scripts\\io", "java.io"},
    {"system\\scripts\\util", "java.util"},
    {"system\\scripts\\render", "java.render"},
    {"system\\scripts\\sound", "java.sound"},
    {"sl\\scripts\\game", "java.game"},
};

bool equalsIgnoreCase(const std::string& a, const char* b) {
  size_t length = std::strlen(b);
  if (a.size() != length)
    return false;
  for (size_t i = 0; i < length; ++i) {
    char left = a[i];
    if (left >= 'A' && left <= 'Z')
      left = static_cast<char>(left - 'A' + 'a');
    if (left != b[i])
      return false;
  }
  return true;
}

// FUN_005461a0's script directive: a payload path ending ".class" maps
// through the root table (the leaf after the last separator becomes the class
// name); anything else is already a class name and FUN_00410890 loads it
// directly. An unknown root has no table mapping; stock instead reads the
// class file's own header name (FUN_00410430), which the port does not
// implement.
std::optional<std::string> classNameFromScriptPath(const std::string& raw) {
  const std::string suffix = ".class";
  if (raw.size() < suffix.size() ||
      raw.compare(raw.size() - suffix.size(), suffix.size(), suffix) != 0)
    return raw;
  const size_t slash = raw.find_last_of('\\');
  if (slash == std::string::npos || slash + 1 >= raw.size() - suffix.size())
    return {};
  const std::string root = raw.substr(0, slash);
  const std::string leaf =
      raw.substr(slash + 1, raw.size() - slash - 1 - suffix.size());
  for (const auto& entry : kScriptRoots)
    if (equalsIgnoreCase(root, entry.directory))
      return std::string(entry.packageName) + "." + leaf;
  return {};
}
} // namespace

GameCreationResult
prepareGameCreation(PackRegistry& registry, uint32_t scriptResourceId,
                    std::optional<uint32_t> parentResourceId,
                    std::optional<std::string> constructorArgument,
                    std::optional<std::string> name,
                    const std::function<std::optional<std::string>(
                        const std::string&, std::string&)>& pathResolver) {
  if (!scriptResourceId)
    return {{}, "zero script resource needs unsupported class fallback"};
  if (!name)
    return {{}, "null name needs unsupported class fallback"};
  if (name->find('\0') != std::string::npos ||
      (constructorArgument &&
       constructorArgument->find('\0') != std::string::npos))
    return {{}, "embedded NUL is unsupported"};
  PackResource resource;
  std::string error;
  if (!registry.lookup(scriptResourceId, resource, error))
    return {{},
            error + " (script resource " + std::to_string(scriptResourceId) +
                ")"};
  if (resource.entry.resType != 8 || !resource.packBytes)
    return {{}, "expected type-8 script resource"};
  const auto& bytes = *resource.packBytes;
  const size_t offset = resource.entry.payloadOffset,
               size = resource.entry.payloadSize;
  if (offset > bytes.size() || size > bytes.size() - offset || size > 65536)
    return {{}, "invalid script payload bounds"};
  const auto payloadBegin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
  std::string payload(payloadBegin, payloadBegin + static_cast<std::ptrdiff_t>(size));
  if (payload.find('\0') != std::string::npos)
    return {{}, "invalid script payload"};
  // FUN_005461a0 walks the payload as text lines: `script <path>` names the
  // class, `native <device>` builds the node's native device, and lod and
  // description lines store metadata. Unknown lines are skipped. The chunked
  // forms (ICAL/ICFG/XCAL/XCFG/LODA magics) are a separate branch the port
  // has not read; such a payload fails below without a recognized directive.
  bool sawScript = false;
  std::string scriptClass, nativeDevice;
  std::optional<std::string> nativeConfigPath;
  size_t position = 0;
  while (position < payload.size()) {
    size_t end = payload.find('\n', position);
    if (end == std::string::npos)
      end = payload.size();
    std::string line = payload.substr(position, end - position);
    position = end + 1;
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const size_t space = line.find(' ');
    const std::string directive =
        space == std::string::npos ? line : line.substr(0, space);
    const std::string rest =
        space == std::string::npos ? "" : line.substr(space + 1);
    if (equalsIgnoreCase(directive, "script")) {
      if (sawScript)
        return {{}, "duplicate script directive"};
      auto mapped = classNameFromScriptPath(rest);
      if (!mapped && pathResolver)
        mapped = pathResolver(rest, error);
      if (!mapped)
        return {{}, error.empty() ? "unsupported script path root" : error};
      if (mapped->empty() || mapped->find_first_not_of(
                                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQR"
                                 "STUVWXYZ0123456789._$") != std::string::npos)
        return {{}, "invalid script class name"};
      scriptClass = std::move(*mapped);
      sawScript = true;
    } else if (equalsIgnoreCase(directive, "native")) {
      const auto parsed = parseNativePayloadDirective(rest);
      if (!parsed)
        return {{}, "invalid native device directive"};
      nativeDevice = parsed->device;
      nativeConfigPath = parsed->configPath;
    }
    // lod and description lines (FUN_005461a0 stores them at +0x18/+0x14)
    // and unrecognized lines are skipped, as stock skips them.
  }
  if (!sawScript && nativeDevice.empty())
    return {{}, "unsupported script payload"};
  if (parentResourceId && !*parentResourceId)
    parentResourceId.reset();
  GameCreationRequest request{
      scriptResourceId, std::move(scriptClass),
      parentResourceId, std::move(constructorArgument),
      std::move(*name), std::move(nativeDevice),
      std::move(nativeConfigPath)};
  return {std::move(request), {}};
}

namespace {
CreationResult fail(CreationStatus status, CreationIssue issue,
                    const char* detail) {
  CreationResult result;
  result.status = status;
  result.issue = issue;
  result.detail = detail;
  return result;
}
} // namespace

CreationResult createNativeInstance(GameWorld& world,
                                    const GameCreationRequest& request,
                                    ReceiverBinding* receiver,
                                    ResourceWorld* resources,
                                    const PlayerFactorySetup* player,
                                    const CarCameraSettings* cameraSettings) {
  // FUN_0042ab50 on the receiver's field; a null record reaches the "Mighty
  // ERROR" branch.
  if (!receiver)
    return fail(CreationStatus::StockError, CreationIssue::NoReceiverLink,
                "receiver has no native link record");
  if (!request.scriptResourceId)
    return fail(
        CreationStatus::Unsupported, CreationIssue::ZeroScriptId,
        "zero script id needs the per-class gametype node (FUN_0053a1a0)");

  // FUN_00546070 resolves the id as a type-8 node; loading it parses the text
  // directives (`script <path>`, `native <device>`) in FUN_005461a0 and looks
  // the class up (FUN_00410890, or FUN_00410430 for a ".class" path).
  ScriptNode* script = world.findScript(request.scriptResourceId);
  // A script node that a pack mount made has no class yet: stock reads "script
  // <class>" only when it loads the node.
  if (script && !script->className.empty() &&
      script->className != request.scriptClass)
    return fail(CreationStatus::StockError, CreationIssue::ScriptClassMismatch,
                "script resource already holds another class");
  if (!script || !script->loaded) {
    if (!world.classResolver())
      return fail(CreationStatus::Unsupported,
                  CreationIssue::ClassResolverMissing,
                  "script load needs a class resolver");
    if (!world.classResolver()(request.scriptClass))
      return fail(CreationStatus::StockError, CreationIssue::ScriptClassMissing,
                  "JVM.getClass: class not found");
  }

  // FUN_00536a10: a null/empty parent record means the world default parent,
  // otherwise the parent id is looked up as a type-1 node (FUN_00537000 with
  // type 1).
  WorldNode* parent = request.parentResourceId
                          ? world.findInstance(*request.parentResourceId)
                          : world.defaultParent();
  ResourceNode* packParent = nullptr;
  if (!parent && request.parentResourceId && resources) {
    const auto lookup = resources->lookup(*request.parentResourceId, 1);
    if (lookup.status == LookupStatus::MountRefused)
      return fail(CreationStatus::Unsupported, CreationIssue::ParentNotLoaded,
                  "parent pack mount is unsupported");
    if (lookup.status == LookupStatus::StockCrash)
      return fail(CreationStatus::StockError, CreationIssue::ParentNotFound,
                  "parent pack mount crashes");
    if (lookup.status == LookupStatus::Found)
      packParent = lookup.node.packNode;
  }
  if (!parent && !packParent)
    return fail(CreationStatus::StockError, CreationIssue::ParentNotFound,
                "Unregistrable node: parent not found");
  // FUN_00536a10 links the new node under the parent record found by
  // FUN_00537000 without testing its load state; a pack parent that the
  // loader has not reached yet simply gains a child.
  if (parent && !parent->loaded)
    return fail(CreationStatus::Unsupported, CreationIssue::ParentNotLoaded,
                "unloaded world parent needs the resource load path");
  std::unique_ptr<CarPhysics> builtCarPhysics;
  std::unique_ptr<PartPhysics> builtPartPhysics;
  if (request.nativeDevice == "car") {
    runlog::event(runlog::EventKind::Note,
                  "car creation requested: resource=0x%08x class=%s config=%s",
                  request.scriptResourceId, request.scriptClass.c_str(),
                  request.nativeConfigPath ? request.nativeConfigPath->c_str()
                                           : "<missing>");
    const char* trace = std::getenv("SLRR_TRACE_CAR_CREATION");
    // The event ring has a fixed text limit; the captured stderr line retains
    // the complete configuration path for later diagnosis. Print it only when
    // explicitly asked (every play.sh run has runlog active, and a mirrored
    // stderr line reads as a second err entry in the run log).
    if (trace && std::strcmp(trace, "1") == 0)
      std::fprintf(
          stderr,
          "car creation requested: resource=0x%08x class=%s config=%s\n",
          request.scriptResourceId, request.scriptClass.c_str(),
          request.nativeConfigPath ? request.nativeConfigPath->c_str()
                                   : "<missing>");
    if (!resources || !request.nativeConfigPath)
      return fail(CreationStatus::Unsupported,
                  CreationIssue::NativeDeviceUnsupported,
                  "car factory needs its resource world and config path");
    if (!script)
      script = world.addScript(request.scriptResourceId, request.scriptClass);
    if (!script)
      return fail(CreationStatus::Unsupported,
                  CreationIssue::NativeDeviceUnsupported,
                  "car script prototype allocation failed");
    std::string error;
    if (!loadCarScriptPrototype(*resources, *script, *request.nativeConfigPath,
                                error, cameraSettings) &&
        (!script->car ||
         script->car->stage != CarPrototypeStage::PhysicsGeometryReady ||
         script->car->configPath != *request.nativeConfigPath))
      return fail(CreationStatus::Unsupported,
                  CreationIssue::NativeDeviceUnsupported, error.c_str());
    auto& car = *script->car;
    builtCarPhysics = buildDetachedCarPhysics(
        car, request.constructorArgument.value_or(""), world.physicsTime(),
        {0, physics::k::gravityY, 0}, error);
    if (!builtCarPhysics)
      return fail(CreationStatus::Unsupported,
                  CreationIssue::NativeDeviceUnsupported, error.c_str());
  } else if (request.nativeDevice == "part") {
    runlog::event(runlog::EventKind::Note,
                  "part creation requested: resource=0x%08x class=%s config=%s",
                  request.scriptResourceId, request.scriptClass.c_str(),
                  request.nativeConfigPath ? request.nativeConfigPath->c_str()
                                           : "<missing>");
    if (!resources || !request.nativeConfigPath)
      return fail(CreationStatus::Unsupported,
                  CreationIssue::NativeDeviceUnsupported,
                  "part factory needs its resource world and config path");
    if (!script)
      script = world.addScript(request.scriptResourceId, request.scriptClass);
    if (!script)
      return fail(CreationStatus::Unsupported,
                  CreationIssue::NativeDeviceUnsupported,
                  "part script prototype allocation failed");
    std::string error;
    if (!loadPartScriptPrototype(*resources, *script, *request.nativeConfigPath,
                                 error))
      return fail(CreationStatus::Unsupported,
                  CreationIssue::NativeDeviceUnsupported, error.c_str());
    auto& partProto = *script->car;
    if (partProto.config.rootBody >= 0 || partProto.clickGeometry) {
      builtPartPhysics = buildPartPhysics(
          partProto.config, partProto.geometry,
          request.constructorArgument.value_or(""), error,
          partProto.clickGeometry ? &*partProto.clickGeometry : nullptr,
          world.physicsTime());
      if (!builtPartPhysics)
        return fail(CreationStatus::Unsupported,
                    CreationIssue::NativeDeviceUnsupported, error.c_str());
    }
  } else if (request.nativeDevice == "ground") {
    runlog::event(
        runlog::EventKind::Note,
        "ground creation requested: resource=0x%08x class=%s config=%s",
        request.scriptResourceId, request.scriptClass.c_str(),
        request.nativeConfigPath ? request.nativeConfigPath->c_str()
                                 : "<missing>");
    return fail(
        CreationStatus::Unsupported, CreationIssue::NativeDeviceUnsupported,
        "native device factory, initialization, and hooks are not modeled");
  } else if (!request.nativeDevice.empty() &&
             (request.nativeDevice != "player" || !player ||
              !player->soundResource || !player->fontResource))
    return fail(
        CreationStatus::Unsupported, CreationIssue::NativeDeviceUnsupported,
        "native device factory, initialization, and hooks are not modeled");
  int32_t playerArgs[3]{};
  if (player && request.nativeDevice == "player" &&
      request.constructorArgument) {
    const char* cursor = request.constructorArgument->c_str();
    for (auto& value : playerArgs) {
      char* end = nullptr;
      errno = 0;
      const long parsed = std::strtol(cursor, &end, 10);
      if (end == cursor)
        break;
      if (errno == ERANGE || parsed < std::numeric_limits<int32_t>::min() ||
          parsed > std::numeric_limits<int32_t>::max())
        return fail(CreationStatus::Unsupported,
                    CreationIssue::NativeDeviceUnsupported,
                    "player constructor argument is out of range");
      value = static_cast<int32_t>(parsed);
      cursor = end;
    }
    if (playerArgs[1] != 0)
      return fail(CreationStatus::Unsupported,
                  CreationIssue::NativeDeviceUnsupported,
                  "player constructor resource link is not modeled");
  }
  if (request.nativeDevice.empty() && !receiver->getInfo)
    return fail(CreationStatus::Unsupported,
                CreationIssue::GetInfoUpcallMissing,
                "FUN_00427210 needs receiver script upcalls");

  // From here on the node tree and ids are rolled back on an unsupported
  // result. The loaded script node stays, as it does in stock.
  if (!script)
    script = world.addScript(request.scriptResourceId, request.scriptClass);
  if (!script)
    return fail(CreationStatus::Unsupported,
                CreationIssue::ClassResolverMissing, "script table error");
  script->className = request.scriptClass;
  script->loaded = true;
  if (resources)
    resources->noteScriptLoaded(script->resourceId);

  const auto descriptor = prepareGameInstanceDescriptor(request);
  WorldNode* node =
      packParent ? resources->addGameChild(*packParent, descriptor.name,
                                           descriptor.flags)
                 : world.addChild(*parent, descriptor.name, descriptor.flags);
  if (!node)
    return fail(CreationStatus::Unsupported,
                CreationIssue::InstanceIdsExhausted, "instance ids exhausted");
  world.noteInstanceCreated(*node);
  struct NodeGuard {
    OwnerLink link;
    ~NodeGuard() { setLinkOwner(link, nullptr); }
  } guard;
  setLinkOwner(guard.link, node);
  node->flags |= 0x200;
  world.markLoaded(*node);
  node->loadPriority = 1.0f;
  // FUN_0053dd80 constructs the GameType's subscription list empty. No
  // supported native in the host adds subscriptions yet.
  node->eventSubscriptionsKnownEmpty = true;
  setLinkOwner(node->scriptLink, script);
  node->hasReceiver = true;

  if (request.nativeDevice == "player") {
    world.initPlayerFactory(*player->soundResource);
    node->nativeDevice = "player";
    node->playerInput = std::make_unique<PlayerInputState>();
    world.configurePlayerInput(*node->playerInput);
    node->player = std::make_unique<PlayerInstance>();
    node->player->config = player->config;
    node->player->firstArgument = playerArgs[0];
    node->player->playerNumber = playerArgs[2];
    node->player->name = "Player " + std::to_string(playerArgs[2]);
    setLinkOwner(node->player->font, player->fontResource);
    world.attachPlayerInput(*node);
    node->eventMask |= 0x10;
    world.registerHook(GameHook::Kind8, *node);
    world.registerHook(GameHook::Kind1c, *node);
  } else if (request.nativeDevice == "car" || request.nativeDevice == "part") {
    const bool isCar = request.nativeDevice == "car";
    node->nativeDevice = request.nativeDevice;
    node->carInstance = std::make_unique<CarRuntimeInstance>();
    auto& inst = *node->carInstance;
    inst.isCar = isCar;
    setLinkOwner(inst.prototypeSource, script);
    if (isCar) {
      setLinkOwner(inst.carLink, node);
      inst.stateMode = 2;
    } else {
      inst.stateMode = 3;
      if (parent && parent != world.defaultParent())
        setLinkOwner(inst.installParentLink, parent);
      else if (packParent)
        setLinkOwner(inst.installParentLink, &linkOwner(*packParent));
    }
    PartSlotDirective args{};
    const auto parsed = stockScan(request.constructorArgument.value_or(""),
                                  "%f,%f,%f,%f,%f,%f,%d");
    for (size_t i = 0; i < parsed.values.size(); ++i) {
      if (i < 3)
        args.position[i] = parsed.values[i].f;
      else if (i < 6)
        args.orientation[i - 3] = parsed.values[i].f;
      else
        args.id = parsed.values[i].i;
    }
    PartSlotState initial{};
    if (makePartSlotState(args, initial)) {
      inst.position = initial.position;
      inst.ypr = {args.orientation[0], args.orientation[1],
                  args.orientation[2]};
      inst.rotation = initial.rotation;
    }
    if (script->car) {
      const auto& proto = *script->car;
      if (isCar)
        for (size_t i = 0; i < proto.config.drivers.size(); ++i)
          inst.seats.push_back(std::make_unique<CarSeat>());
      inst.slots.reserve(proto.config.slots.size());
      for (size_t i = 0; i < proto.config.slots.size(); ++i) {
        const auto& srcSlot = proto.config.slots[i];
        auto dstSlot = std::make_unique<RuntimeSlotState>();
        dstSlot->id = srcSlot.state.id;
        dstSlot->transform = srcSlot.state;
        dstSlot->orientation = srcSlot.orientation;
        dstSlot->slotType = srcSlot.slotType;
        dstSlot->damageMode = srcSlot.damageMode;
        dstSlot->deform = srcSlot.deform;
        inst.slots.push_back(std::move(dstSlot));
      }
      for (const auto& res : proto.resources) {
        if (res->request.directive == "mesh" && !inst.meshId)
          inst.meshId = res->id;
        if (res->request.directive == "texture" && !inst.textureId)
          inst.textureId = res->id;
        if (res->request.directive == "texture" && !inst.defaultTextureId)
          inst.defaultTextureId = res->id;
      }
      for (const auto& body : proto.config.bodies) {
        if (body.kind == CarBodyRecord::Render && body.renderResource) {
          PackView view;
          std::string mapErr;
          if (resources && resources->packs().view(
                               static_cast<uint16_t>(script->resourceId >> 16),
                               view, mapErr) == PackViewStatus::Ok) {
            const ResourceIdMapping mapping{
                view.slot,
                static_cast<uint32_t>(resources->packs().slotCount()),
                view.dependencies};
            inst.renderType = remapResourceId(body.renderResource, mapping);
            break;
          }
        }
      }
      if (isCar) {
        for (size_t i = 0;
             i < proto.config.wheels.size() && i < inst.wheelStates.size(); ++i)
          inst.wheelStates[i] = proto.config.wheels[i];
        for (size_t i = 0; i < proto.engineUp.size(); ++i)
          if (const auto* item = proto.engineUp.item(i))
            inst.sfxEngineUp.addItem(
                item->sound.owner,
                item->inversePitch > 0.0f ? 1.0f / item->inversePitch : 0.0f,
                item->pitchMin, item->pitchMax, item->volumeMin,
                item->volumeMax);
        for (size_t i = 0; i < proto.engineDown.size(); ++i)
          if (const auto* item = proto.engineDown.item(i))
            inst.sfxEngineDown.addItem(
                item->sound.owner,
                item->inversePitch > 0.0f ? 1.0f / item->inversePitch : 0.0f,
                item->pitchMin, item->pitchMax, item->volumeMin,
                item->volumeMax);
        for (size_t i = 0; i < proto.exhaust.size(); ++i)
          if (const auto* item = proto.exhaust.item(i))
            inst.sfxExhaust.addItem(
                item->sound.owner,
                item->inversePitch > 0.0f ? 1.0f / item->inversePitch : 0.0f,
                item->pitchMin, item->pitchMax, item->volumeMin,
                item->volumeMax);
      }
    }
    inst.carPhysics = std::move(builtCarPhysics);
    if (isCar)
      inst.initializeWheelDrivetrain();
    inst.partPhysics = std::move(builtPartPhysics);
    if (isCar) {
      // FUN_0044a250 (all.c:58972-58979) sleeps the completed compound
      // before returning it to Scripts for assembly and placement.
      suspendCar(*inst.carPhysics);
      std::string registrationError;
      auto* root =
          world.registerCarPhysics(*node, inst.carPhysics, registrationError);
      if (!root) {
        world.destroy(*node);
        return fail(CreationStatus::Unsupported,
                    CreationIssue::NativeDeviceUnsupported,
                    registrationError.c_str());
      }
      setLinkOwner(inst.physicsRoot, root);
    } else {
      std::string registrationError;
      std::vector<physics::Body*> tree;
      // 0046eae0 sleeps its root before 0049b230 constructs children.
      // 0049b0b0/0049a840 do not inherit that sleep flag: preserve the
      // resulting root-only sleep instead of sleeping the completed tree.
      if (!inst.partPhysics ||
          !partPhysicsTree(*inst.partPhysics, tree, registrationError)) {
        world.destroy(*node);
        return fail(CreationStatus::Unsupported,
                    CreationIssue::NativeDeviceUnsupported,
                    registrationError.empty() ? "part sleep failed" : registrationError.c_str());
      }
      physics::sleepBodyState(*inst.partPhysics->root);
      auto* root = world.registerPartPhysics(*node, inst.partPhysics, registrationError);
      if (!root) {
        world.destroy(*node);
        return fail(CreationStatus::Unsupported,
                    CreationIssue::NativeDeviceUnsupported,
                    registrationError.c_str());
      }
      setLinkOwner(inst.physicsRoot, root);
    }
    node->eventMask |= 0x10;
    if (isCar) {
      world.registerHook(GameHook::Kind8, *node);
      world.registerHook(GameHook::Kind9, *node);
      world.registerHook(GameHook::Kind1c, *node);
    } else {
      world.registerHook(GameHook::Kind9, *node);
    }
  }

  const auto initializeReceiver = receiver->initializeReceiver;
  const auto getInfo = receiver->getInfo;
  if (initializeReceiver) {
    const auto error = initializeReceiver(*node);
    if (error) {
      if (guard.link.owner)
        world.destroy(*node);
      return fail(CreationStatus::Unsupported, CreationIssue::GetInfoFailed,
                  error->c_str());
    }
    if (!guard.link.owner)
      return fail(CreationStatus::Unsupported, CreationIssue::GetInfoFailed,
                  "script constructor destroyed its node");
  }

  // FUN_00427210 -> three FUN_00426470 calls. The new node has no native
  // instance, so each runs the receiver's getInfo. A nonzero answer would
  // register entries in the global lists at DAT_00636338.
  for (const int kind : {8, 9, 0x1c}) {
    if (request.nativeDevice == "player" || request.nativeDevice == "car" ||
        request.nativeDevice == "part")
      continue;
    const auto answer = getInfo(kind);
    if (!guard.link.owner)
      return fail(CreationStatus::Unsupported, CreationIssue::GetInfoFailed,
                  "getInfo destroyed its node");
    if (!answer || *answer != 0) {
      world.destroy(*node);
      if (!answer)
        return fail(CreationStatus::Unsupported, CreationIssue::GetInfoFailed,
                    "getInfo script call failed in the VM");
      return fail(CreationStatus::Unsupported,
                  CreationIssue::GetInfoRegistration,
                  "getInfo registered a hook; global lists are not modelled");
    }
  }

  // The sector link was set when the node was created (GameWorld::addChild runs
  // FUN_00544fe0): under the type-0 root the node is its own sector root. Then
  // FUN_004988a0 finds no children and FUN_00498b10 returns 0 at once (bounds
  // kind +0x80 is 2). FUN_0048ae40 does run on the node's sector target, but it
  // returns non-zero only for a target whose native instance (+0x124) is set,
  // and a script node without a `native` factory never has one, so FUN_004b3ee0
  // is not reached. Every world node here is such a node. Receiver attachment
  // (handler tail): FUN_004290f0 moves the receiver link to the new node.
  setLinkOwner(receiver->link, node);

  CreationResult result;
  result.status = CreationStatus::Created;
  result.node = node;
  result.instanceId = node->instanceId;
  return result;
}
