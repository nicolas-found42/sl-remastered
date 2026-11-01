#include "natives/ground_road.hpp"

#include "ground_runtime.hpp"
#include "natives/game_type.hpp"
#include "resource_world.hpp"
#include "road_alignment.hpp"
#include "road_network.hpp"
#include "road_route.hpp"
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
bool readPosition(const VmValue& value, std::array<float, 3>& position) {
  const auto* object = std::get_if<const VmObject*>(&value);
  if (!object || !*object) return false;
  const char* names[] = {"x", "y", "z"};
  for (size_t i = 0; i < 3; ++i) {
    bool found = false;
    for (const auto& field : (*object)->fields) {
      if (field.name == names[i] && field.descriptor == "F" &&
          std::holds_alternative<float>(field.value)) {
        position[i] = std::get<float>(field.value);
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}
const RoadNetwork* roads(GroundFactoryResult* factory) {
  return factory && factory->preparation && factory->preparation->files.roads
             ? &*factory->preparation->files.roads
             : nullptr;
}
} // namespace

// FUN_00483e60: return an array of snapped position and direction; absent
// ground returns null. FUN_00583a00 selects the road spline.
NativeResult groundAlignToRoad(const std::vector<VmValue>& args,
                               const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes) {
  if (!arena || args.size() != 1)
    return std::string{"GroundRef.alignToRoad: invalid arguments"};
  if (std::holds_alternative<const VmObject*>(args[0]) &&
      !std::get<const VmObject*>(args[0]))
    return VmValue{static_cast<const VmObject*>(nullptr)};
  std::array<float, 3> position;
  if (!readPosition(args[0], position))
    return std::string{"GroundRef.alignToRoad: invalid vector"};
  const auto* network = roads(roadFactory(receiver, arena));
  if (!network) return VmValue{static_cast<const VmObject*>(nullptr)};
  const auto aligned = roadAlignToRoad(*network, position);
  if (!aligned)
    return std::string{"GroundRef.alignToRoad: road spline cannot be evaluated"};
  if (arena->objects.size() > arena->limit || arena->limit - arena->objects.size() < 3)
    return std::string{"GroundRef.alignToRoad: arena full"};
  const auto p = allocateVector3(arena, classes, aligned->position[0],
                                 aligned->position[1], aligned->position[2]);
  if (const auto* error = std::get_if<std::string>(&p)) return *error;
  const auto d = allocateVector3(arena, classes, aligned->direction[0],
                                 aligned->direction[1], aligned->direction[2]);
  if (const auto* error = std::get_if<std::string>(&d)) return *error;
  auto result = std::make_unique<VmObject>();
  result->arrayDescriptor = "[Ljava.lang.Vector3;";
  result->arrayElements = {std::get<VmValue>(p), std::get<VmValue>(d)};
  const auto* raw = result.get();
  arena->objects.push_back(std::move(result));
  return VmValue{raw};
}

// FUN_00483400: return the negated route spline tangent, or null when the
// route solver returns a zero vector. Route construction is FUN_005826f0.
NativeResult groundGetStartDirection(const std::vector<VmValue>& args,
                                     const VmObject* receiver, VmArena* arena,
                                     const std::vector<const TufaClass*>* classes) {
  if (!arena || args.size() != 2)
    return std::string{"GroundRef.getStartDirection: invalid arguments"};
  std::array<float, 3> start, finish;
  if (!readPosition(args[0], start) || !readPosition(args[1], finish))
    return std::string{"GroundRef.getStartDirection: invalid vectors"};
  const auto* network = roads(roadFactory(receiver, arena));
  if (!network) return VmValue{static_cast<const VmObject*>(nullptr)};
  const auto direction = roadStartDirection(*network, start, finish);
  if (!direction) return VmValue{static_cast<const VmObject*>(nullptr)};
  return allocateVector3(arena, classes, (*direction)[0], (*direction)[1],
                         (*direction)[2]);
}

// FUN_004835e0 returns without work if either position argument is null.
// UNCORROBORATED APPROXIMATION — not stock-matched: FUN_00583fd0's full
// flag-propagation semantics are unread in triage; this implementation marks
// only the route's "other" records and reports an error when there is no
// road network or no route, instead of silent success.
NativeResult groundHaltTrafficPath(const std::vector<VmValue>& args,
                                   const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>*) {
  if (!arena || args.size() != 2)
    return std::string{"GroundRef.haltTrafficPath: invalid arguments"};
  for (const auto& arg : args)
    if (std::holds_alternative<const VmObject*>(arg) &&
        !std::get<const VmObject*>(arg))
      return std::monostate{};
  std::array<float, 3> start, finish;
  if (!readPosition(args[0], start) || !readPosition(args[1], finish))
    return std::string{"GroundRef.haltTrafficPath: invalid vectors"};
  auto* factory = roadFactory(receiver, arena);
  const auto* network = roads(factory);
  if (!network)
    return std::string{"GroundRef.haltTrafficPath: no road network"};
  const auto halted = roadHaltTrafficPath(*network, start, finish);
  if (!halted)
    return std::string{"GroundRef.haltTrafficPath: road route cannot be evaluated"};
  auto& records = factory->preparation->otherInitial;
  if (records.size() != halted->size())
    return std::string{"GroundRef.haltTrafficPath: road records are not initialized"};
  for (size_t i = 0; i < records.size(); ++i) records[i].fieldC4 = (*halted)[i];
  return std::monostate{};
}
