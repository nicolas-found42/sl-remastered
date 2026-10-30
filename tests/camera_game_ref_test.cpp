#include "camera_device_factory.hpp"
#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "resource_world.hpp"
#include "vm_test_harness.hpp"
#include <cstdio>

namespace {
const VmObject *parentObject, *sourceObject, *receiverObject;
NativeResult creationResult;
NativeResult createCamera(const std::vector<VmValue> &, const VmObject *,
                          VmArena *arena, const std::vector<const TufaClass *> *classes) {
  const VmObject *null = nullptr;
  creationResult = gameRefCreate({parentObject, sourceObject, null, null},
                                 receiverObject, arena, classes);
  return std::monostate{};
}
}

int main() {
  GameWorld world;
  auto *root = world.addRoot();
  if (!root) return 1;
  auto *parent = world.addChild(*root, "parent", 0x21);
  if (!parent) return 1;
  world.markLoaded(*parent);
  PackRegistry packs(".scratch/2005-extract/Program Files/Street Legal Racing - Redline");
  ResourceWorld resources(world, packs);
  std::string error;
  uint32_t cameraId = 0;
  if (!packs.resolveLiteral("system.rpk", 0x33, cameraId, error) ||
      !resources.lookup(cameraId, 8).node) return 2;
  VmGameState game;
  game.world = &world; game.packs = &packs; game.resources = &resources;
  VmArena arena;
  arena.gameState = &game;
  const auto object = [&](uint32_t id) -> const VmObject * {
    auto instance = std::make_unique<VmObject>();
    const int32_t handle = static_cast<int32_t>(arena.resourceStates.size() + 1);
    instance->fields.push_back({"ptr", handle, "I"});
    const auto *raw = instance.get();
    arena.objects.push_back(std::move(instance));
    arena.resourceStates.push_back({raw, {}});
    arena.resourceStates.back().words[2] = id;
    return raw;
  };
  parentObject = object(parent->instanceId);
  sourceObject = object(cameraId);
  receiverObject = object(0);
  vm_test::Cb caller("test.CameraCaller");
  caller.method("run", "()V", 0x48, {});
  const auto ref = caller.ref("test.CameraCaller", "run", "()V");
  TufaClass config; config.name = "java.util.Config";
  const std::vector<const TufaClass *> classes{&caller.c, &config};
  std::vector<VmStaticFields> fields{{&config,
      {{"camera_AOV", 55.0f, "F"}, {"camera_torque", 0.02f, "F"},
       {"camera_force", 2.0f, "F"}, {"camera_damping", 1.0f, "F"},
       {"camera_perpdamping", 0.7f, "F"}, {"camera_ext_clipplane", 0.2f, "F"},
       {"camera_ext_viewrange", 200.0f, "F"}, {"camera_max_force", 20.0f, "F"},
       {"camera_nocollision", int32_t{0}, "I"}}}};
  const std::vector<NativeEntry> natives{{"test.CameraCaller", "run", "()V", "test", createCamera}};
  const auto call = vmExecute(caller.c,
      {vm_test::literal(0), vm_test::node(0x24), vm_test::node(0x1a, true, ref), vm_test::retVoid},
      {}, &natives, &classes, &fields, &arena);
  const auto *returned = std::get_if<VmValue>(&creationResult);
  if (!call.ok || !returned || !std::get_if<const VmObject *>(returned) ||
      std::get<const VmObject *>(*returned)) return 3;
  auto *state = nativeResourceState(receiverObject, &arena);
  auto *node = state ? world.findInstance(state->words[2]) : nullptr;
  auto *script = world.findScript(cameraId);
  if (!node || !node->cameraDevice || node->scriptReceiver || !script ||
      !script->cameraPrototype || node->scriptLink.owner != script ||
      game.bindings.at(3)->link.owner != node || world.dynamicBodyCount() != 1)
    return 4;
  const auto prototype = script->cameraPrototype;
  receiverObject = object(0);
  const auto second = vmExecute(caller.c,
      {vm_test::literal(0), vm_test::node(0x24), vm_test::node(0x1a, true, ref), vm_test::retVoid},
      {}, &natives, &classes, &fields, &arena);
  auto *secondState = nativeResourceState(receiverObject, &arena);
  auto *secondNode = secondState ? world.findInstance(secondState->words[2]) : nullptr;
  if (!second.ok || !secondNode || !secondNode->cameraDevice ||
      script->cameraPrototype != prototype || world.dynamicBodyCount() != 2) return 5;
  if (!world.destroy(*node) || world.dynamicBodyCount() != 1 ||
      game.bindings.at(3)->link.owner || !secondNode->cameraDevice) return 6;
  if (!world.destroy(*secondNode) || world.dynamicBodyCount() != 0 ||
      game.bindings.at(4)->link.owner) return 7;
  std::puts("camera_game_ref_test: ok");
}
