#include "car_runtime.hpp"

#include "resource_world.hpp"
#include "game_creation.hpp"
#include "car_click_geometry.hpp"
#include <cmath>

CarPrototype::~CarPrototype() { setLinkOwner(source, nullptr); }

CarResourceBinding::~CarResourceBinding() { setLinkOwner(owner, nullptr); }

RuntimeSlotState::~RuntimeSlotState() { setLinkOwner(attachedNode, nullptr); }

CarSeat::~CarSeat() {
  setLinkOwner(player, nullptr);
  setLinkOwner(camera, nullptr);
  setLinkOwner(renderInstance, nullptr);
  setLinkOwner(osd, nullptr);
  for (OwnerLink &link : links)
    setLinkOwner(link, nullptr);
}

std::optional<std::array<float, 16>> calculateCarSteeringAngles(
    const CarRuntimeInstance& car, const CarConfig& chassis) {
  if (!car.carPhysics || car.carPhysics->wheels.size() > car.wheelExtras.size() ||
      !std::isfinite(car.steeringState) || !std::isfinite(chassis.maxSteer[0]) ||
      !std::isfinite(chassis.maxSteer[1])) return std::nullopt;
  auto result = car.wheelSteeringAngles;
  const float requested = chassis.maxSteer[0] * car.steeringState;
  const float base = chassis.maxSteer[1];
  if (!std::isfinite(requested)) return std::nullopt;
  for (size_t i = 0; i < car.carPhysics->wheels.size(); ++i) {
    const auto& wheel = car.carPhysics->wheels[i];
    if (!wheel) continue;
    const float factor = car.wheelExtras[i].steer;
    if (!wheel->suspensionBody || wheel->suspension.wheelBody != wheel->suspensionBody.get() ||
        !std::isfinite(wheel->anchor[0]) || !std::isfinite(factor)) return std::nullopt;
    if (std::fabs(requested) <= 0.0010000000474974513f) {
      result[i] = 0;
    } else if (base == 0) {
      result[i] = requested * factor;
    } else {
      const float radius = base / std::tan(requested);
      result[i] = (radius <= 0 ? -std::atan2(base, -radius - wheel->anchor[0])
                               : std::atan2(base, radius + wheel->anchor[0])) * factor;
    }
  }
  return result;
}

std::optional<CarControlSnapshot> sampleCarControls(GameWorld& world, const CarSeat& seat) {
  auto* player = world.nodeFor(seat.player.owner);
  if (!player || !player->loaded || player->nativeDevice != "player" || !player->player ||
      !player->playerInput || player->player->scriptedControlOverride) return std::nullopt;
  // Commit smoothing/cache updates only when every stock query is supported.
  auto input = *player->playerInput;
  CarControlSnapshot result;
  constexpr std::array<int32_t, 4> continuousAxes{{1, 0x1c, 0x1d, 0x30}};
  constexpr std::array<int32_t, 7> digitalAxes{{0x31, 0x1f, 0x32, 0x36, 7, 0x27, 0x28}};
  for (size_t i = 0; i < continuousAxes.size(); ++i) {
    const auto value = input.sampleAxis(continuousAxes[i]);
    if (!value) return std::nullopt;
    result.continuous[i] = *value;
  }
  for (size_t i = 0; i < digitalAxes.size(); ++i) {
    const auto value = input.sampleAxis(digitalAxes[i]);
    if (!value) return std::nullopt;
    result.digital[i] = static_cast<int32_t>(*value * 127.0f);
  }
  *player->playerInput = std::move(input);
  return result;
}

CarSeat* CarRuntimeInstance::findSeat(uint32_t playerId) const {
  for (const auto& seat : seats)
    if (seat && seat->player.cachedValue == playerId) return seat.get();
  return nullptr;
}

CarSeat* CarRuntimeInstance::selectSeat(uint32_t playerId) const {
  if (auto* existing = findSeat(playerId)) return existing;
  return findSeat(0);
}

CarRuntimeInstance::~CarRuntimeInstance() {
  setLinkOwner(prototypeSource, nullptr);
  setLinkOwner(carLink, nullptr);
  setLinkOwner(physicsRoot, nullptr);
  setLinkOwner(installParentLink, nullptr);
  for (auto& slot : slots) if (slot) setLinkOwner(slot->attachedNode, nullptr);
  for (auto& part : attachedParts) if (part) setLinkOwner(*part, nullptr);
}

bool loadCarPrototype(ResourceWorld& world, ScriptNode& source, const ResourceIdMapping& mapping,
                      std::string_view text, std::unique_ptr<CarPrototype>& result,
                      std::string& error, bool isCar) {
  result = std::make_unique<CarPrototype>();
  result->isCar = isCar;
  setLinkOwner(result->source, &source);
  struct DefaultRequest { const char* pack; uint16_t local; int type; };
  static const DefaultRequest carDefaults[] = {
    {"decals.rpk", 0x0b, 7}, {"frontend.rpk", 0x23, 14}, {"cars.rpk", 0x55, 14}, {"cars.rpk", 0x57, 14},
    {"cars.rpk", 0x56, 14}, {"cars.rpk", 0x58, 14}, {"cars.rpk", 0x58, 14},
    {"cars.rpk", 0x5c, 14}, {"cars.rpk", 0x5c, 14}, {"cars.rpk", 0x58, 14},
    {"cars.rpk", 0x61, 14}, {"cars.rpk", 0x62, 14}, {"cars.rpk", 0x63, 14},
    {"cars.rpk", 0x64, 14}, {"sound.rpk", 0x0d, 6}, {"sound.rpk", 0x0d, 6},
    {"sound.rpk", 0x0d, 6}, {"sound.rpk", 0x0d, 6}, {"sound.rpk", 0x19, 6},
    {"sound.rpk", 0x1a, 6}, {"sound.rpk", 7, 6}, {"sound.rpk", 9, 6},
    {"sound.rpk", 10, 6}, {"sound.rpk", 0x17, 6}, {"sound.rpk", 0x18, 6},
    {"sound.rpk", 0x15, 6}, {"sound.rpk", 8, 6}, {"sound.rpk", 6, 6},
    {"sound.rpk", 0x25, 6}, {"particles.rpk", 2, 14}, {"particles.rpk", 2, 14}
  };
  static const DefaultRequest partDefaults[] = {
    {"decals.rpk", 0x0b, 7}, {"sound.rpk", 0x0b, 6}
  };
  auto load = [&](const CarResourceRequest& request, uint32_t id) {
    const auto fail = [&](const std::string& why) {
      error = "car " + request.directive + " line " + std::to_string(request.line) +
              " resource " + std::to_string(id) + ": " + why;
      return false;
    };
    if (!id) {
      auto binding = std::make_unique<CarResourceBinding>();
      binding->request = request;
      result->resources.push_back(std::move(binding));
      return true;
    }
    auto found = world.lookup(id, static_cast<uint32_t>(request.loadType), request.fallbackType != 0);
    if (!found.node && request.fallbackType)
      found = world.lookup(id, static_cast<uint32_t>(request.fallbackType));
    if (found.status != LookupStatus::Found || !found.node.owner) return fail("resource unavailable");
    auto loaded = std::make_unique<CarResourceBinding>();
    loaded->request = request;
    loaded->id = id;
    setLinkOwner(loaded->owner, found.node.owner);
    result->resources.push_back(std::move(loaded));
    return true;
  };
  const DefaultRequest* defaults = isCar ? carDefaults : partDefaults;
  const size_t defaultCount = isCar ? std::size(carDefaults) : std::size(partDefaults);
  for (size_t i = 0; i < defaultCount; ++i) {
    const auto& item = defaults[i];
    uint32_t id = 0;
    if (!world.packs().resolveLiteral(item.pack, item.local, id, error)) return false;
    if (!load({"default", item.local, item.type, 0, true, {}, 0}, id)) return false;
  }
  if (!text.empty() && !parseCarConfig(text, result->config, error)) return false;
  for (auto& slot : result->config.slots) {
    for (auto& attach : slot.attachments)
      attach.resourceId = remapResourceId(attach.resourceId, mapping);
    for (auto& comp : slot.compatible)
      comp.resource = remapResourceId(comp.resource, mapping);
    for (auto& bolt : slot.bolts)
      if (bolt.resource) bolt.resource = remapResourceId(bolt.resource, mapping);
    if (slot.hasFlapSounds) {
      for (auto& sfx : slot.flapSounds)
        if (sfx) sfx = remapResourceId(sfx, mapping);
    }
  }
  for (auto& stockPart : result->config.stockParts)
    stockPart = remapResourceId(stockPart, mapping);
  size_t up = 0, down = 0, exhaust = 0;
  for (const auto& request : result->config.resources) {
    uint32_t id = request.runtimeId ? request.resource : remapResourceId(request.resource, mapping);
    if (!request.literalPack.empty() &&
        !world.packs().resolveLiteral(request.literalPack, request.resource, id, error)) return false;
    if (!load(request, id)) return false;
    CarSfxTable* table = nullptr;
    const CarEngineSound* sound = nullptr;
    if (request.directive == "sfx_engine_up") { table = &result->engineUp; sound = &result->config.engineUp[up++]; }
    else if (request.directive == "sfx_engine_down") { table = &result->engineDown; sound = &result->config.engineDown[down++]; }
    else if (request.directive == "sfx_exhaust") { table = &result->exhaust; sound = &result->config.exhaust[exhaust++]; }
    if (table && !table->addItem(result->resources.back()->owner.owner, sound->rpmScale,
                                 sound->values[0], sound->values[1], sound->values[2], sound->values[3])) {
      error = "car sound table capacity exceeded";
      return false;
    }
  }
  if (isCar) {
    for (const auto& fallback : {std::pair<CarSfxTable*, uint16_t>{&result->engineUp, 3},
                                 {&result->engineDown, 2}}) {
      if (fallback.first->size()) continue;
      uint32_t id = 0;
      if (!world.packs().resolveLiteral("sound.rpk", fallback.second, id, error)) return false;
      if (!load({"default sound", fallback.second, 6, 0, true, {}, 0}, id)) return false;
      fallback.first->addItem(result->resources.back()->owner.owner,
                              fallback.second == 3 ? 5000.0f : 2500.0f,
                              1.0f, 1000000.0f, 1.0f, 1.0f);
    }
  }
  const auto collisionLoader = [&](const std::string& path, ScxModel& model, std::string& detail) {
    std::string bytes;
    if (!readNativeDataFile(world.packs(), path, INT32_MAX, bytes, detail)) return false;
    auto mesh = scxParse(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    if (!mesh.ok) { detail = mesh.error; return false; }
    model = std::move(mesh.model);
    return true;
  };
  const auto geometryResourceLoader = [&](uint32_t serialized, float mass,
      CarGeometryPrototype& out, std::string& detail) {
    const auto id = remapResourceId(serialized, mapping);
    const auto found = world.lookup(id, 9);
    PackResource resource;
    if (!found.node || !world.packs().lookup(id, resource, detail) || !resource.packBytes) {
      if (detail.empty()) detail = "physics geometry resource unavailable";
      return false;
    }
    const auto& bytes = *resource.packBytes;
    const size_t offset = resource.entry.payloadOffset, size = resource.entry.payloadSize;
    if (offset > bytes.size() || size > bytes.size() - offset) {
      detail = "invalid physics geometry resource payload bounds";
      return false;
    }
    CarGeometryPrototype prototype;
    if (!loadCarGeometryResource(std::string_view(reinterpret_cast<const char*>(bytes.data() + offset), size),
                                 collisionLoader, prototype, detail)) return false;
    out = cloneGeometryResource(prototype, mass);
    return true;
  };
  if (!buildCarGeometry(result->config, collisionLoader, result->geometry, error,
                        geometryResourceLoader)) return false;
  if (isCar && !buildCarWheelGeometry(result->config, result->config.brakePower, result->wheels, error)) return false;
  if (result->config.click) {
    CarGeometryPrototype click;
    const auto& definition = *result->config.click;
    if (definition.resource) {
      if (!geometryResourceLoader(*definition.resource, 0, click, error)) return false;
      click.body->flags = (click.body->flags | 0x20u) & ~physics::kProtoCreated;
    } else if (!loadCarClickMesh(definition.meshPath, collisionLoader, click, error)) return false;
    PartSlotDirective pose{};
    for (size_t i = 0; i < 3; ++i) {
      pose.position[i] = definition.position[i];
      pose.orientation[i] = definition.orientation[i];
    }
    PartSlotState transform;
    if (!makePartSlotState(pose, transform)) { error = "invalid click geometry transform"; return false; }
    click.transform = {definition.position, definition.orientation, transform.rotation};
    result->clickGeometry = std::move(click);
  }
  result->stage = CarPrototypeStage::PhysicsGeometryReady;
  error.clear();
  return true;
}
