#include "natives/ground_road.hpp"

#include "ground_runtime.hpp"
#include "natives/game_type.hpp"
#include "resource_world.hpp"
#include "road_network.hpp"
#include "run_log.hpp"
#include "world_model.hpp"

namespace {
// The ground factory linked from the receiver's resource state, the same
// message-0x39 lookup FUN_00483d00 uses.
GroundFactoryResult* roadFactory(const VmObject* receiver, VmArena* arena) {
  if (!arena || !arena->gameState || !arena->gameState->resources) return nullptr;
  const auto* state = nativeResourceState(receiver, arena);
  if (!state) return nullptr;
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto it = arena->gameState->bindings.find(handle);
  if (it == arena->gameState->bindings.end() || !it->second ||
      !it->second->link.owner)
    return nullptr;
  const auto resolved =
      arena->gameState->resources->resolve(it->second->link.owner);
  return resolved.packNode && resolved.packNode->groundFactory
             ? resolved.packNode->groundFactory.get()
             : nullptr;
}
void gap(const char* method, const char* stock) {
  static std::string first;
  static bool reported = false;
  if (reported) return;
  reported = true;
  runlog::event(runlog::EventKind::Note,
                "gap: GroundRef.%s delegates to the stock road layer %s, which "
                "is not ported; the argument boundary is ported and the call "
                "leaves the road state untouched",
                method, stock);
}
} // namespace

// FUN_00483e60: null Vector3 returns null; the road spline query (FUN_00583a00)
// returns the snapped position and direction.
NativeResult groundAlignToRoad(const std::vector<VmValue>& args,
                               const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
  if (!arena || args.size() != 1)
    return std::string{"GroundRef.alignToRoad: invalid arguments"};
  const auto* object = std::get_if<const VmObject*>(&args[0]);
  if (!object)
    return std::string{"GroundRef.alignToRoad: invalid vector"};
  if (!*object) return VmValue{static_cast<const VmObject*>(nullptr)};
  if (!roadFactory(receiver, arena))
    return VmValue{static_cast<const VmObject*>(nullptr)};
  gap("alignToRoad", "FUN_00583a00");
  return VmValue{static_cast<const VmObject*>(nullptr)};
}

// FUN_00483400: null arguments or a zero-length direction return null.
// The route construction and tangent (FUN_005826f0, FUN_00583840) are the
// unported layer.
NativeResult groundGetStartDirection(const std::vector<VmValue>& args,
                                     const VmObject* receiver, VmArena* arena,
                                     const std::vector<const TufaClass*>*) {
  if (!arena || args.size() != 2)
    return std::string{"GroundRef.getStartDirection: invalid arguments"};
  for (const auto& arg : args) {
    const auto* object = std::get_if<const VmObject*>(&arg);
    if (!object) return std::string{"GroundRef.getStartDirection: invalid vector"};
    if (!*object) return VmValue{static_cast<const VmObject*>(nullptr)};
  }
  if (!roadFactory(receiver, arena))
    return VmValue{static_cast<const VmObject*>(nullptr)};
  gap("getStartDirection", "FUN_00583840");
  return VmValue{static_cast<const VmObject*>(nullptr)};
}

// FUN_004835e0 returns without work when either position argument is null.
// FUN_00583fd0's record clearing and traffic propagation is the unported layer.
NativeResult groundHaltTrafficPath(const std::vector<VmValue>& args,
                                   const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>*) {
  if (!arena || args.size() != 2)
    return std::string{"GroundRef.haltTrafficPath: invalid arguments"};
  for (const auto& arg : args) {
    const auto* object = std::get_if<const VmObject*>(&arg);
    if (!object) return std::string{"GroundRef.haltTrafficPath: invalid vector"};
    if (!*object) return std::monostate{};
  }
  if (!roadFactory(receiver, arena)) return std::monostate{};
  gap("haltTrafficPath", "FUN_00583fd0");
  return std::monostate{};
}
