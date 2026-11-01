#include "camera_device_factory.hpp"
#include "game_creation.hpp"
#include "ground_runtime.hpp"
#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "pack_registry.hpp"
#include "resource_world.hpp"
#include "vm.hpp"
#include "world_model.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <variant>

namespace {
const VmObject *makeVector3(VmArena &arena, float x, float y, float z) {
  auto object = std::make_unique<VmObject>();
  object->fields.push_back(VmField{"x", x, "F"});
  object->fields.push_back(VmField{"y", y, "F"});
  object->fields.push_back(VmField{"z", z, "F"});
  const VmObject *raw = object.get();
  arena.objects.push_back(std::move(object));
  return raw;
}

const VmObject *bindRef(VmArena &arena, VmGameState &game, uint32_t id) {
  auto object = std::make_unique<VmObject>();
  object->fields.push_back(VmField{"ptr", int32_t{0}, "I"});
  const VmObject *raw = object.get();
  arena.objects.push_back(std::move(object));
  arena.resourceStates.push_back({raw, {}});
  const int32_t handle = static_cast<int32_t>(arena.resourceStates.size());
  raw->fields[0].value = handle;
  arena.resourceStates.back().words[2] = id;
  game.bindings[handle] = std::make_unique<ReceiverBinding>();
  game.bindings[handle]->link.cachedValue = id;
  return raw;
}

int32_t returned(const NativeResult &result) {
  const auto *value = std::get_if<VmValue>(&result);
  assert(value && std::holds_alternative<int32_t>(*value));
  return std::get<int32_t>(*value);
}
} // namespace

int main() {
  PackRegistry packs("ref/raw");
  GameWorld world;
  assert(world.addRoot());
  ResourceWorld resources(world, packs);
  uint16_t citySlot = 0;
  std::string error;
  assert(packs.registerPackName("maps\\city.rpk", citySlot, error));
  const auto city = resources.lookup((uint32_t(citySlot) << 16) | 1, 1);
  assert(city.node.packNode);
  auto &node = *city.node.packNode;

  auto preparation = std::make_shared<GroundPreparation>();
  RoadNetwork network;
  network.paths.resize(1);
  preparation->files.roads = std::move(network);
  auto factory = std::make_shared<GroundFactoryResult>();
  factory->preparation = preparation;
  factory->roadWorld = &world;
  node.groundFactory = factory;

  VmArena arena;
  VmGameState game;
  game.world = &world;
  game.resources = &resources;
  arena.gameState = &game;
  const VmObject *ground = bindRef(arena, game, node.value);
  const int32_t groundHandle = std::get<int32_t>(ground->fields[0].value);
  setLinkOwner(game.bindings[groundHandle]->link, &node);
  // The nearest crossing returns the road node, not the query point.
  auto &roads = *preparation->files.roads;
  roads.nodes[0].resize(2);
  const float crossingA[3] = {10.f, 2.f, 3.f};
  const float crossingB[3] = {30.f, 4.f, 5.f};
  std::memcpy(roads.nodes[0][0].words.data(), crossingA, sizeof(crossingA));
  std::memcpy(roads.nodes[0][1].words.data(), crossingB, sizeof(crossingB));
  TufaClass vectorClass;
  vectorClass.name = "java.lang.Vector3";
  const std::vector<const TufaClass *> classes{&vectorClass};
  const auto nearest = invokeNative("java.util.resource.GroundRef", "getNearestCross",
      "(Ljava.lang.Vector3;F)Ljava.lang.Vector3;",
      {VmValue{makeVector3(arena, 9.f, 2.f, 3.f)}, VmValue{0.f}}, ground, &arena, &classes);
  const auto *nearestValue = std::get_if<VmValue>(&nearest);
  assert(nearestValue && std::holds_alternative<const VmObject *>(*nearestValue));
  const auto *crossing = std::get<const VmObject *>(*nearestValue);
  assert(crossing && std::get<float>(crossing->fields[0].value) == 10.f &&
         std::get<float>(crossing->fields[1].value) == 2.f &&
         std::get<float>(crossing->fields[2].value) == 3.f);
  roads.nodes[0].clear();

  const uint32_t taxi = 0x00350006;
  const VmObject *car = bindRef(arena, game, taxi);

  const auto call = [&](int32_t count) {
    return invokeNative("java.util.resource.GroundRef", "addTrafficN",
                        "(Ljava.util.resource.GameRef;IFFF)I",
                        {VmValue{car}, VmValue{count}, VmValue{2.f},
                         VmValue{5.f}, VmValue{2.f}},
                        ground, &arena, nullptr);
  };

  assert(returned(call(80)) == 80);
  assert(factory->traffic.size() == 80);
  for (const auto &placed : factory->traffic) {
    assert(placed.templateId == taxi);
    assert(placed.along == 2.f && placed.across == 5.f && placed.gap == 2.f);
  }
  assert(returned(call(0)) == 0);
  assert(factory->traffic.size() == 80);
  assert(returned(call(-3)) == 0);
  assert(factory->traffic.size() == 80);
  assert(returned(call(2)) == 2);
  assert(factory->traffic.size() == 82);
  const auto beforeDensity = resources.groundSnapshot();
  assert(beforeDensity.cities == 1 && beforeDensity.roads == 1 &&
         beforeDensity.traffic == 82 && beforeDensity.densitySet == 0);

  const auto setDensity = [&](float density) {
    return invokeNative("java.util.resource.GroundRef", "setPedestrianDensityN",
                        "(F)V", {VmValue{density}}, ground, &arena, nullptr);
  };
  assert(std::holds_alternative<std::monostate>(setDensity(0.003f)));
  assert(factory->pedestrianDensity == 0.003f);
  assert(factory->pedestrianDensityHi == 0.003f * 1.1f);
  assert(std::holds_alternative<std::monostate>(setDensity(0.0005f)));
  assert(factory->pedestrianDensity == 0.0005f);
  assert(factory->pedestrianDensityHi == 0.0005f * 1.1f);
  assert(factory->pedestrianDensitySet);
  const auto afterDensity = resources.groundSnapshot();
  assert(afterDensity.cities == 1 && afterDensity.roads == 1 &&
         afterDensity.traffic == 82 && afterDensity.densitySet == 1 &&
         afterDensity.density == 0.0005f);

  const uint32_t pedestrian = 0x00360001;
  const VmObject *walker = bindRef(arena, game, pedestrian);
  const auto addPedType = [&](const VmObject *groundRef,
                              const VmObject *typeRef) {
    return invokeNative("java.util.resource.GroundRef", "addPedestrianType",
                        "(Ljava.util.resource.GameRef;)V", {VmValue{typeRef}},
                        groundRef, &arena, nullptr);
  };
  assert(factory->pedestrianTypes.empty());
  assert(std::holds_alternative<std::monostate>(addPedType(ground, walker)));
  assert(factory->pedestrianTypes.size() == 1);
  assert(factory->pedestrianTypes[0] == pedestrian);
  assert(std::holds_alternative<std::monostate>(addPedType(ground, walker)));
  assert(factory->pedestrianTypes.size() == 1);
  assert(std::holds_alternative<std::monostate>(addPedType(ground, car)));
  assert(factory->pedestrianTypes.size() == 2);
  assert(factory->pedestrianTypes[0] == pedestrian);
  assert(factory->pedestrianTypes[1] == taxi);
  const auto afterPedTypes = resources.groundSnapshot();
  assert(afterPedTypes.pedestrianTypes == 2 && afterPedTypes.waterActive == 0);

  assert(!factory->waterActive);
  const VmObject *posA = makeVector3(arena, 10.f, -12.f, 20.f);
  const VmObject *dirA = makeVector3(arena, 0.f, 1.f, 0.f);
  const auto setWater = [&](const VmObject *groundRef, const VmObject *pos,
                            const VmObject *dir, float extentA, float extentB) {
    return invokeNative(
        "java.util.resource.GroundRef", "setWater",
        "(Ljava.lang.Vector3;Ljava.lang.Vector3;FF)V",
        {VmValue{pos}, VmValue{dir}, VmValue{extentA}, VmValue{extentB}},
        groundRef, &arena, nullptr);
  };
  assert(std::holds_alternative<std::monostate>(
      setWater(ground, posA, dirA, 300.f, 550.f)));
  assert(factory->waterActive);
  assert(factory->waterExtentA == 300.f && factory->waterExtentB == 550.f);
  assert(factory->waterPosX == 10.f && factory->waterPosY == -12.f &&
         factory->waterPosZ == 20.f);
  assert(factory->waterDirX == 0.f && factory->waterDirY == 1.f &&
         factory->waterDirZ == 0.f);

  const VmObject *posB = makeVector3(arena, 1.f, 2.f, 3.f);
  const VmObject *dirB = makeVector3(arena, 3.f, 0.f, 4.f);
  assert(std::holds_alternative<std::monostate>(
      setWater(ground, posB, dirB, 40.f, 50.f)));
  assert(factory->waterActive);
  assert(factory->waterExtentA == 40.f && factory->waterExtentB == 50.f);
  assert(factory->waterPosX == 1.f && factory->waterPosY == 2.f &&
         factory->waterPosZ == 3.f);
  assert(factory->waterDirX == 0.6f && factory->waterDirY == 0.f &&
         factory->waterDirZ == 0.8f);
  const auto afterWater = resources.groundSnapshot();
  assert(afterWater.waterActive == 1);
  const auto waterNote = resources.groundWaterNote();
  assert(waterNote.active && waterNote.posX == 1.f &&
         waterNote.extentA == 40.f);

  const VmObject *unlinkedGround = bindRef(arena, game, node.value);
  assert(std::holds_alternative<std::monostate>(
      setWater(unlinkedGround, posB, dirB, 9.f, 9.f)));
  assert(factory->waterExtentA == 40.f && factory->waterExtentB == 50.f);

  const VmObject *limA = makeVector3(arena, 0.f, 0.f, -500.f);
  const VmObject *limB = makeVector3(arena, 0.f, 0.f, 1.f);
  const auto addLimit = [&](const VmObject *groundRef, const VmObject *a,
                            const VmObject *b) {
    return invokeNative("java.util.resource.GroundRef", "addWaterLimit",
                        "(Ljava.lang.Vector3;Ljava.lang.Vector3;)V",
                        {VmValue{a}, VmValue{b}}, groundRef, &arena, nullptr);
  };
  assert(factory->waterLimits.empty());
  assert(std::holds_alternative<std::monostate>(addLimit(ground, limA, limB)));
  assert(factory->waterLimits.size() == 1);
  assert(factory->waterLimits[0].az == -500.f &&
         factory->waterLimits[0].bz == 1.f);
  assert(std::holds_alternative<std::monostate>(
      addLimit(unlinkedGround, limA, limB)));
  assert(factory->waterLimits.size() == 1);
  const auto afterLimits = resources.groundSnapshot();
  assert(afterLimits.waterLimits == 1);

  const auto setFog = [&](const VmObject *groundRef, int32_t color,
                          float nearDist, float farDist) {
    return invokeNative("java.util.resource.GroundRef", "setFog", "(IFF)V",
                        {VmValue{color}, VmValue{nearDist}, VmValue{farDist}},
                        groundRef, &arena, nullptr);
  };
  assert(std::holds_alternative<std::monostate>(
      setFog(ground, 0x7121e, 20.f, 150.f)));
  assert(factory->fogSetting);
  assert(factory->fogSetting->color == 0x7121e);
  assert(factory->fogSetting->nearDist == 200.f &&
         factory->fogSetting->farDist == 1500.f);
  const auto afterFog = resources.groundSnapshot();
  assert(afterFog.fogSettings == 1);
  assert(std::holds_alternative<std::monostate>(
      setFog(ground, 11582652, 70.f, 400.f)));
  assert(factory->fogSetting && factory->fogSetting->color == 11582652 &&
         factory->fogSetting->nearDist == 700.f &&
         factory->fogSetting->farDist == 4000.f);
  assert(resources.groundSnapshot().fogSettings == 1);
  auto camera = makeCameraResource(1, 55, 0.2f, 200, 0, 1, 1, 0);
  applyCameraSectorEnvironment(camera, *factory, 300.f);
  assert(camera.farDistance == 3000.f && camera.fog &&
         camera.fog->color == 11582652 && camera.fog->nearDistance == 700.f &&
         camera.fog->farDistance == 4000.f);
  GroundFactoryResult noFog;
  applyCameraSectorEnvironment(camera, noFog, 400.f);
  assert(camera.farDistance == 4000.f && camera.fog &&
         camera.fog->color == 11582652);

  node.stateWord |= 1;
  node.eventMask = 0x14;
  auto *observed = world.addChild(*world.defaultParent(), "ground observer", 0);
  assert(observed);
  int32_t observedId;
  std::memcpy(&observedId, &observed->instanceId, sizeof observedId);
  const auto event = [&](int32_t kind, const std::string &text) {
    auto message = std::make_unique<VmObject>();
    const VmObject *raw = message.get();
    arena.objects.push_back(std::move(message));
    arena.strings.emplace_back(text, raw);
    return gameRefQueueEvent({VmValue{static_cast<const VmObject *>(nullptr)},
                              VmValue{kind}, VmValue{raw}},
                             ground, &arena, nullptr);
  };
  const auto add = "obs_add " + std::to_string(observedId);
  const auto remove = "obs_rem " + std::to_string(observedId);
  assert(std::holds_alternative<std::monostate>(event(0x10, add)));
  assert(std::holds_alternative<std::monostate>(event(0x10, add)));
  assert(factory->observers.size() == 2 &&
         factory->observers[0]->owner == observed &&
         factory->observers[1]->owner == observed);
  assert(std::holds_alternative<std::monostate>(event(0x10, remove)));
  assert(factory->observers.size() == 1 &&
         factory->observers[0]->owner == observed);
  assert(std::holds_alternative<std::monostate>(
      invokeNative("java.lang.GameType", "clearEventMask", "(I)V",
                   {int32_t{0x10}}, ground, &arena, nullptr)));
  assert(std::holds_alternative<std::monostate>(event(0x10, add)) &&
         factory->observers.size() == 1);
  assert(std::holds_alternative<std::monostate>(
      invokeNative("java.lang.GameType", "setEventMask", "(I)V",
                   {int32_t{0x10}}, ground, &arena, nullptr)));
  assert(std::holds_alternative<std::string>(event(0x10, "unknown")));
  assert(std::holds_alternative<std::string>(event(0x04, "contact")));
  factory->roadWorld = nullptr;
  assert(std::holds_alternative<std::monostate>(event(0x10, add)) &&
         factory->observers.size() == 1);
  factory->roadWorld = &world;
  assert(world.destroy(*observed));
  assert(!factory->observers[0]->owner &&
         factory->observers[0]->cachedValue == 0);
  assert(std::holds_alternative<std::monostate>(event(0x10, "obs_rem 0")) &&
         factory->observers.empty());
  assert(std::holds_alternative<std::monostate>(event(0x10, "obs_add 0")) &&
         factory->observers.empty());

  factory->roadWorld = nullptr;
  assert(returned(call(4)) == 0);
  assert(factory->traffic.size() == 82);
  factory->roadWorld = &world;
  factory->preparation->files.roads->paths.clear();
  assert(returned(call(4)) == 0);
  assert(factory->traffic.size() == 82);

  const VmObject *unlinked = bindRef(arena, game, 1);
  const auto missing =
      invokeNative("java.util.resource.GroundRef", "addTrafficN",
                   "(Ljava.util.resource.GameRef;IFFF)I",
                   {VmValue{car}, VmValue{int32_t{4}}, VmValue{2.f},
                    VmValue{5.f}, VmValue{2.f}},
                   unlinked, &arena, nullptr);
  assert(returned(missing) == 0);
  assert(factory->traffic.size() == 82);

  factory->roadWorld = &world;
  preparation->files.roads->paths.resize(1);
  assert(std::holds_alternative<std::monostate>(setDensity(1.f)));
  assert(factory->pedestrianDensity == 1.f);
  assert(factory->pedestrianDensityHi == 1.1f);

  factory->roadWorld = nullptr;
  assert(std::holds_alternative<std::monostate>(setDensity(0.25f)));
  assert(factory->pedestrianDensity == 1.f);
  assert(factory->pedestrianDensityHi == 1.1f);
  assert(factory->pedestrianDensitySet);
  const auto rejected = resources.groundSnapshot();
  assert(rejected.roads == 0 && rejected.densitySet == 1 &&
         rejected.density == 1.f);

  const size_t typesBeforeMissingGround = factory->pedestrianTypes.size();
  assert(std::holds_alternative<std::monostate>(addPedType(ground, walker)));
  assert(factory->pedestrianTypes.size() == typesBeforeMissingGround);

  factory->roadWorld = &world;
  preparation->files.roads->paths.resize(1);
  assert(std::holds_alternative<std::monostate>(addPedType(unlinked, walker)));
  assert(factory->pedestrianTypes.size() == typesBeforeMissingGround);

  auto *player = world.addChild(*world.defaultParent(), "player", 0);
  assert(player);
  player->carInstance = std::make_unique<CarRuntimeInstance>();
  player->carInstance->isCar = true;
  player->carInstance->position = {1.5f, 2.5f, 3.5f};
  player->carInstance->linearVelocity = {3.f, 4.f, 0.f};
  player->carInstance->currentGear = 2;
  player->carInstance->engineRpm = 3200.f;
  const auto noted = world.playerCarNote();
  assert(noted.count == 1 && noted.instanceId == player->instanceId &&
         noted.x == 1.5f && noted.y == 2.5f && noted.z == 3.5f &&
         noted.speed == 5.f && noted.gear == 2 && noted.rpm == 3200.f);
  player->carInstance->carPhysics = std::make_shared<CarPhysics>();
  auto &carBody = player->carInstance->carPhysics->root;
  carBody = std::make_unique<physics::Body>();
  carBody->current = 1;
  carBody->state[1].pos = {8.f, 9.f, 10.f};
  carBody->state[1].vel = {0.f, 0.f, 12.f};
  const auto moved = world.playerCarNote();
  assert(moved.x == 8.f && moved.y == 9.f && moved.z == 10.f &&
         moved.speed == 12.f);
}
