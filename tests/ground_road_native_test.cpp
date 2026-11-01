#include "game_creation.hpp"
#include "ground_runtime.hpp"
#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "pack_registry.hpp"
#include "resource_world.hpp"
#include "world_model.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <memory>

namespace {
uint32_t bits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, 4);
  return result;
}
const VmObject* vector(VmArena& arena, float x, float y, float z) {
  auto value = std::make_unique<VmObject>();
  value->fields = {{"x", x, "F"}, {"y", y, "F"}, {"z", z, "F"}};
  const auto* result = value.get();
  arena.objects.push_back(std::move(value));
  return result;
}
const VmObject* object(const NativeResult& result) {
  assert(std::holds_alternative<VmValue>(result));
  const auto* value = std::get_if<const VmObject*>(&std::get<VmValue>(result));
  assert(value && *value);
  return *value;
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
  city.node.packNode->groundFactory = factory;
  VmArena arena;
  VmGameState game;
  game.world = &world;
  game.resources = &resources;
  arena.gameState = &game;
  auto receiver = std::make_unique<VmObject>();
  receiver->fields = {{"ptr", int32_t{1}, "I"}};
  const auto* ground = receiver.get();
  arena.objects.push_back(std::move(receiver));
  arena.resourceStates.push_back({ground, {}});
  game.bindings[1] = std::make_unique<ReceiverBinding>();
  setLinkOwner(game.bindings[1]->link, city.node.packNode);
  TufaClass vectorClass;
  vectorClass.name = "java.lang.Vector3";
  std::vector<const TufaClass*> classes{&vectorClass};
  const auto call = [&](const char* method, const char* descriptor, std::vector<VmValue> args) {
    return invokeNative("java.util.resource.GroundRef", method, descriptor, args, ground, &arena, &classes);
  };
  const auto* origin = vector(arena, 0, 0, 0);
  const auto missing = call("alignToRoad", "(Ljava.lang.Vector3;)[Ljava.lang.Vector3;", {VmValue{origin}});
  assert(std::holds_alternative<VmValue>(missing) &&
         std::get<const VmObject*>(std::get<VmValue>(missing)) == nullptr);
}
