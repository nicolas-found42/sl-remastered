#include "game_creation.hpp"
#include "ground_runtime.hpp"
#include "natives/registry.hpp"
#include "natives/game_type.hpp"
#include "pack_registry.hpp"
#include "resource_world.hpp"
#include "world_model.hpp"
#include <fstream>
#include <iterator>
#include <cassert>
#include <cmath>
#include <cstring>
#include <memory>

namespace {
const VmObject* vector(VmArena& arena, float x, float y, float z) {
  auto value = std::make_unique<VmObject>();
  value->fields = {{"x", x, "F"}, {"y", y, "F"}, {"z", z, "F"}};
  const auto* result = value.get();
  arena.objects.push_back(std::move(value));
  return result;
}
float component(const VmObject* value, const char* name) {
  for (const auto& field : value->fields)
    if (field.name == name) return std::get<float>(field.value);
  assert(false);
  return 0;
}
} // namespace
int main() {
  PackRegistry packs("ref/raw");
  GameWorld world;
  assert(world.addRoot());
  ResourceWorld resources(world, packs);
  uint16_t slot = 0;
  std::string error;
  assert(packs.registerPackName("maps\\city.rpk", slot, error));
  auto city = resources.lookup((uint32_t(slot) << 16) | 1, 1);
  assert(city.node.packNode);
  auto factory = std::make_shared<GroundFactoryResult>();
  factory->preparation = std::make_shared<GroundPreparation>();
  {
    std::ifstream trc("ref/raw/maps/city/ai/city.trc", std::ios::binary);
    assert(trc);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(trc)), {});
    RoadNetwork network;
    assert(readRoadNetwork(bytes, network, error));
    factory->preparation->files.roads = std::move(network);
  }
  city.node.packNode->groundFactory = factory;
  VmArena arena;
  VmGameState game;
  game.world = &world;
  game.resources = &resources;
  arena.gameState = &game;
  auto receiver = std::make_unique<VmObject>();
  receiver->fields.push_back(VmField{"ptr", int32_t{0}, "I"});
  const auto* ground = receiver.get();
  arena.objects.push_back(std::move(receiver));
  arena.resourceStates.push_back({ground, {}});
  const int32_t handle = static_cast<int32_t>(arena.resourceStates.size());
  ground->fields[0].value = handle;
  arena.resourceStates.back().words[2] = city.node.packNode->value;
  game.bindings[handle] = std::make_unique<ReceiverBinding>();
  game.bindings[handle]->link.cachedValue = city.node.packNode->value;
  setLinkOwner(game.bindings[handle]->link, city.node.packNode);
  TufaClass vectorClass;
  vectorClass.name = "java.lang.Vector3";
  std::vector<const TufaClass*> classes{&vectorClass};
  const auto call = [&](const char* method, const char* descriptor, std::vector<VmValue> args) {
    return invokeNative("java.util.resource.GroundRef", method, descriptor, args, ground, &arena, &classes);
  };
  // Null vector argument returns null.
  const auto nullResult = call("alignToRoad", "(Ljava.lang.Vector3;)[Ljava.lang.Vector3;",
                               {VmValue{static_cast<const VmObject*>(nullptr)}});
  assert(std::holds_alternative<VmValue>(nullResult) &&
         std::get<const VmObject*>(std::get<VmValue>(nullResult)) == nullptr);
  // A spawn-area position aligns to a real road spline: two Vector3 elements.
  const auto* origin = vector(arena, -278.5f, 9.8f, 1033.0f);
  const auto aligned = call("alignToRoad", "(Ljava.lang.Vector3;)[Ljava.lang.Vector3;",
                            {VmValue{origin}});
  assert(std::holds_alternative<VmValue>(aligned));
  const auto* array = std::get_if<const VmObject*>(&std::get<VmValue>(aligned));
  assert(array && *array && (*array)->arrayElements.size() == 2);
  const auto* position = std::get_if<const VmObject*>(&(*array)->arrayElements[0]);
  const auto* direction = std::get_if<const VmObject*>(&(*array)->arrayElements[1]);
  assert(position && *position && direction && *direction);
  double length = 0;
  for (const char* name : {"x", "y", "z"}) {
    const double v = component(*direction, name);
    length += v * v;
  }
  assert(std::fabs(length - 1.0) < 1e-3);
  assert(std::isfinite(component(*position, "x")));
  // Start direction between two road positions is a unit vector.
  const auto* finish = vector(arena, -278.5f, 9.8f, 1000.0f);
  const auto startDir = call("getStartDirection",
                             "(Ljava.lang.Vector3;Ljava.lang.Vector3;)Ljava.lang.Vector3;",
                             {VmValue{origin}, VmValue{finish}});
  assert(std::holds_alternative<VmValue>(startDir));
  const auto* dir = std::get_if<const VmObject*>(&std::get<VmValue>(startDir));
  assert(dir && *dir);
  double dlen = 0;
  for (const char* name : {"x", "y", "z"}) {
    const double v = component(*dir, name);
    dlen += v * v;
  }
  assert(std::fabs(dlen - 1.0) < 1e-3);
  // haltTrafficPath initializes the other-record flags without error.
  factory->preparation->otherInitial.assign(factory->preparation->files.roads->others.size(), {});
  const auto halted = call("haltTrafficPath",
                           "(Ljava.lang.Vector3;Ljava.lang.Vector3;)V",
                           {VmValue{origin}, VmValue{finish}});
  assert(std::holds_alternative<std::monostate>(halted));
  size_t marked = 0;
  for (const auto& record : factory->preparation->otherInitial) marked += record.fieldC4;
  assert(marked >= 1);
  return 0;
}
