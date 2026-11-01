#include "natives/registry.hpp"
#include "ground_runtime.hpp"
#include "natives/car_part.hpp"
#include "natives/file.hpp"
#include "natives/game_type.hpp"
#include "natives/ground_road.hpp"
#include "natives/lang_object.hpp"
#include "natives/lang_string.hpp"
#include "natives/lang_system.hpp"
#include "natives/math.hpp"
#include "natives/measure.hpp"
#include "natives/navigator.hpp"
#include "natives/physics.hpp"
#include "natives/random.hpp"
#include "natives/resource_duplicate.hpp"
#include "natives/resource_ref.hpp"
#include "natives/sfx.hpp"
#include "natives/sound.hpp"
#include "natives/text.hpp"
#include "natives/vector3.hpp"
#include "natives/video.hpp"
#include "natives/viewport.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"
#include "resource_texture_cache.hpp"
#include "world_model.hpp"

#include <cstring>

namespace {

bool readVector3Components(const VmObject* object, float& x, float& y, float& z) {
  if (!object)
    return false;
  const VmField* fx = nullptr;
  const VmField* fy = nullptr;
  const VmField* fz = nullptr;
  for (const auto& field : object->fields) {
    if (field.descriptor != "F" || !std::holds_alternative<float>(field.value))
      continue;
    if (field.name == "x")
      fx = &field;
    else if (field.name == "y")
      fy = &field;
    else if (field.name == "z")
      fz = &field;
  }
  if (!fx || !fy || !fz)
    return false;
  x = std::get<float>(fx->value);
  y = std::get<float>(fy->value);
  z = std::get<float>(fz->value);
  return true;
}
NativeResult buildNumber(const std::vector<VmValue>& args, const VmObject*,
                         VmArena*, const std::vector<const TufaClass*>*) {
  if (!args.empty())
    return std::string{"buildNumber expects no arguments"};
  return VmValue{int32_t{606}};
}

NativeResult enableDebugDump(const std::vector<VmValue>& args, const VmObject*,
                             VmArena*,
                             const std::vector<const TufaClass*>*) {
  if (args.size() != 1 || !std::holds_alternative<const VmObject*>(args[0]))
    return std::string{"enableDebugDump expects one reference argument"};
  return VmValue{int32_t{0}};
}

NativeResult resourceNewNative(const std::vector<VmValue>& args,
                               const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes) {
  if (!args.empty() || !receiver || !arena || !classes)
    return std::string{"invalid ResourceRef receiver"};
  bool owned = false;
  for (const auto& object : arena->objects)
    if (object.get() == receiver)
      owned = true;
  if (!owned)
    return std::string{"invalid ResourceRef receiver"};
  const TufaClass* current = receiver->runtimeClass;
  std::vector<const TufaClass*> visited;
  while (current && visited.size() < 128) {
    bool loaded = false;
    for (const auto* candidate : *classes)
      if (candidate == current)
        loaded = true;
    if (!loaded || !current->hasClassHeader)
      return std::string{"invalid ResourceRef class"};
    for (const auto* prior : visited)
      if (prior == current)
        return std::string{"cyclic ResourceRef class"};
    visited.push_back(current);
    if (current->name == "java.util.resource.ResourceRef")
      break;
    const auto parent = current->classHeader[3];
    if (parent >= current->constants.size())
      return std::string{"invalid ResourceRef class"};
    const auto& ref = current->constants[parent];
    if (ref.kind != 4 || ref.a >= current->constants.size() ||
        current->constants[ref.a].kind != 0)
      return std::string{"invalid ResourceRef class"};
    const auto& name = current->constants[ref.a].text;
    current = nullptr;
    for (const auto* candidate : *classes)
      if (candidate && candidate->name == name)
        current = candidate;
  }
  if (!current || current->name != "java.util.resource.ResourceRef")
    return std::string{"invalid ResourceRef class"};
  VmField* ptr = nullptr;
  for (auto& field : receiver->fields)
    if (field.name == "ptr") {
      if (ptr)
        return std::string{"ambiguous Native.ptr"};
      ptr = &field;
    }
  if (!ptr || ptr->descriptor != "I" ||
      !std::holds_alternative<int32_t>(ptr->value))
    return std::string{"invalid Native.ptr"};
  const int32_t handle = std::get<int32_t>(ptr->value);
  if (handle != 0) {
    if (handle < 0 ||
        static_cast<size_t>(handle) > arena->resourceStates.size() ||
        arena->resourceStates[static_cast<size_t>(handle) - 1].owner !=
            receiver)
      return std::string{"invalid ResourceRef handle"};
    return std::monostate{};
  }
  if (arena->resourceStates.size() >= static_cast<size_t>(INT32_MAX))
    return std::string{"ResourceRef handle table full"};
  arena->resourceStates.push_back({receiver, {}});
  ptr->value = static_cast<int32_t>(arena->resourceStates.size());
  return std::monostate{};
}

} // namespace

VmResourceState* nativeResourceState(const VmObject* object, VmArena* arena) {
  if (!object || !arena)
    return nullptr;
  bool owned = false;
  for (const auto& candidate : arena->objects)
    if (candidate.get() == object)
      owned = true;
  if (!owned)
    return nullptr;
  const VmField* ptr = nullptr;
  for (const auto& candidate : object->fields)
    if (candidate.name == "ptr") {
      if (ptr || candidate.descriptor != "I")
        return nullptr;
      ptr = &candidate;
    }
  if (!ptr)
    return nullptr;
  const auto* handle = std::get_if<int32_t>(&ptr->value);
  if (!handle || *handle <= 0 ||
      static_cast<size_t>(*handle) > arena->resourceStates.size())
    return nullptr;
  auto& state = arena->resourceStates[static_cast<size_t>(*handle) - 1];
  return state.owner == object ? &state : nullptr;
}

namespace {
NativeResult resourceDuplicate(const std::vector<VmValue>& args,
                               const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
  if (args.size() != 1 || !std::holds_alternative<const VmObject*>(args[0]))
    return std::string{"invalid ResourceRef.duplicate argument"};
  auto* destination = nativeResourceState(receiver, arena);
  if (!destination)
    return std::string{"invalid ResourceRef.duplicate receiver"};
  const auto* sourceObject = std::get<const VmObject*>(args[0]);
  const auto* source =
      sourceObject ? nativeResourceState(sourceObject, arena) : nullptr;
  if (sourceObject && !source)
    return std::string{"invalid ResourceRef.duplicate source"};
  // World-backed references use ReceiverBinding, not the legacy fixture's
  // attachment.
  if (source && arena->gameState) {
    auto& game = *arena->gameState;
    const auto sourceHandle =
        static_cast<int32_t>(source - arena->resourceStates.data()) + 1;
    const auto destHandle =
        static_cast<int32_t>(destination - arena->resourceStates.data()) + 1;
    const auto srcIt = game.bindings.find(sourceHandle);
    const auto dstIt = game.bindings.find(destHandle);
    LinkOwner* sourceOwner = srcIt == game.bindings.end() || !srcIt->second
                                 ? nullptr
                                 : srcIt->second->link.owner;
    const bool destLinked = dstIt != game.bindings.end() && dstIt->second &&
                            dstIt->second->link.owner;
    if (sourceOwner || destLinked) {
      if (source->attachment || destination->attachment)
        return std::string{
            "ResourceRef.duplicate: mixed attachment models unsupported"};
      if (sourceOwner) {
        if (!game.resources)
          return std::string{
              "ResourceRef.duplicate: resource world unavailable"};
        const auto node = game.resources->resolve(sourceOwner);
        if (!node)
          return std::string{"ResourceRef.duplicate: source node unavailable"};
        if (node.type == 14) {
          auto material = game.resources->sceneMaterial(node);
          if (!material && node.packNode) {
            auto loaded = sceneLoadMaterial(*game.resources, node.id);
            if (!loaded.material)
              return "ResourceRef.duplicate: unsupported material payload: " +
                     loaded.error;
            material = std::shared_ptr<SceneMaterialResources>(
                std::move(loaded.material));
            if (!game.resources->bindSceneMaterial(node, material))
              return std::string{
                  "ResourceRef.duplicate: material binding failed"};
          }
          if (!material)
            return std::string{
                "ResourceRef.duplicate: material clone unsupported"};
          std::string error;
          auto* clone = sceneCloneMaterial(*game.resources, node, error);
          if (!clone)
            return "ResourceRef.duplicate: " + error;
          auto& slot = game.bindings[destHandle];
          if (!slot)
            slot = std::make_unique<ReceiverBinding>();
          setLinkOwner(slot->link, clone);
          destination->words[2] = slot->link.cachedValue;
          return std::monostate{};
        }
        if (node.type == 5) {
          std::string error;
          auto* clone = sceneCloneMesh(*game.resources, node, error);
          if (!clone)
            return "ResourceRef.duplicate: " + error;
          auto& slot = game.bindings[destHandle];
          if (!slot)
            slot = std::make_unique<ReceiverBinding>();
          setLinkOwner(slot->link, clone);
          destination->words[2] = slot->link.cachedValue;
          return std::monostate{};
        }
        if (node.type == 7) {
          if (!arena->gameState->renderer)
            return std::string{
                "ResourceRef.duplicate: texture renderer unavailable"};
          std::string error;
          WorldNode* clone = sceneCloneTexture(*game.resources,
                                               *arena->gameState->renderer, node,
                                               error);
          if (!clone)
            return "ResourceRef.duplicate: " + error;
          auto& slot = game.bindings[destHandle];
          if (!slot)
            slot = std::make_unique<ReceiverBinding>();
          setLinkOwner(slot->link, clone);
          destination->words[2] = slot->link.cachedValue;
          return std::monostate{};
        }
        if (node.type == 13) {
          std::string error;
          WorldNode* clone = sceneCloneRenderLight(*game.resources, node, error);
          if (!clone)
            return "ResourceRef.duplicate: " + error;
          auto& slot = game.bindings[destHandle];
          if (!slot)
            slot = std::make_unique<ReceiverBinding>();
          setLinkOwner(slot->link, clone);
          destination->words[2] = slot->link.cachedValue;
          return std::monostate{};
        }
      }
      auto& slot = game.bindings[destHandle];
      if (!slot)
        slot = std::make_unique<ReceiverBinding>();
      setLinkOwner(slot->link, sourceOwner);
      destination->words[2] = slot->link.cachedValue;
      return std::monostate{};
    }
  }
  if (!resourceDuplicateShared(*destination, source))
    return std::string{"resource clone unsupported"};
  return std::monostate{};
}

NativeResult threadInit(const std::vector<VmValue>& args,
                        const VmObject* receiver, VmArena* arena,
                        const std::vector<const TufaClass*>*) {
  if (!arena || !arena->scheduler || args.size() != 1)
    return std::string{"thread scheduler unavailable"};
  const auto* name = std::get_if<const VmObject*>(&args[0]);
  if (!name || !*name)
    return std::string{"thread name unavailable"};
  for (const auto& entry : arena->strings)
    if (entry.second == *name) {
      const auto result = arena->scheduler->createThread(receiver, entry.first);
      return result.ok ? NativeResult{std::monostate{}}
                       : NativeResult{result.error};
    }
  return std::string{"thread name is not an interned string"};
}
NativeResult threadStart(const std::vector<VmValue>&, const VmObject* receiver,
                         VmArena* arena,
                         const std::vector<const TufaClass*>*) {
  if (!arena || !arena->scheduler)
    return std::string{"thread scheduler unavailable"};
  const auto result = arena->scheduler->start(receiver);
  return result.ok ? NativeResult{std::monostate{}}
                   : NativeResult{result.error};
}
NativeResult threadStop(const std::vector<VmValue>&, const VmObject* receiver,
                        VmArena* arena,
                        const std::vector<const TufaClass*>*) {
  if (!arena || !arena->scheduler)
    return std::string{"thread scheduler unavailable"};
  const auto result = arena->scheduler->stop(receiver);
  return result.ok ? NativeResult{std::monostate{}}
                   : NativeResult{result.error};
}
NativeResult threadSleep(const std::vector<VmValue>& args, const VmObject*,
                         VmArena* arena,
                         const std::vector<const TufaClass*>*) {
  if (!arena || !arena->scheduler || args.size() != 1 ||
      !std::holds_alternative<float>(args[0]))
    return std::string{"thread sleep unavailable"};
  const auto result = arena->scheduler->sleep(std::get<float>(args[0]));
  return result.ok ? NativeResult{std::monostate{}}
                   : NativeResult{result.error};
}
NativeResult threadIsAlive(const std::vector<VmValue>&, const VmObject*,
                           VmArena*, const std::vector<const TufaClass*>*) {
  return VmValue{int32_t{0}};
}
NativeResult threadSetPriority(const std::vector<VmValue>& args, const VmObject* receiver,
                               VmArena* arena, const std::vector<const TufaClass*>*) {
  if (!arena || !arena->scheduler || args.size() != 1 ||
      !std::holds_alternative<int32_t>(args[0]))
    return std::string{"thread setPriority unavailable"};
  const auto result =
      arena->scheduler->setPriority(receiver, std::get<int32_t>(args[0]));
  return result.ok ? NativeResult{std::monostate{}}
                   : NativeResult{result.error};
}

NativeResult objectWait(const std::vector<VmValue>& args,
                        const VmObject* receiver, VmArena* arena,
                        const std::vector<const TufaClass*>*) {
  if (!args.empty() || !arena || !arena->scheduler)
    return std::string{"object wait scheduler unavailable"};
  const auto result = arena->scheduler->wait(receiver);
  return result.ok ? NativeResult{std::monostate{}}
                   : NativeResult{result.error};
}
NativeResult objectNotify(const std::vector<VmValue>& args,
                          const VmObject* receiver, VmArena* arena,
                          const std::vector<const TufaClass*>*) {
  if (!args.empty() || !arena || !arena->scheduler)
    return std::string{"object notify scheduler unavailable"};
  const auto result = arena->scheduler->notify(receiver, false);
  return result.ok ? NativeResult{std::monostate{}}
                   : NativeResult{result.error};
}
NativeResult objectNotifyAll(const std::vector<VmValue>& args,
                             const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>*) {
  if (!args.empty() || !arena || !arena->scheduler)
    return std::string{"object notify scheduler unavailable"};
  const auto result = arena->scheduler->notify(receiver, true);
  return result.ok ? NativeResult{std::monostate{}}
                   : NativeResult{result.error};
}

GroundFactoryResult* linkedGroundFactory(const VmObject* receiver, VmArena* arena) {
  if (!arena || !arena->gameState || !arena->gameState->resources)
    return nullptr;
  const auto* state = nativeResourceState(receiver, arena);
  if (!state)
    return nullptr;
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = arena->gameState->bindings.find(handle);
  if (binding == arena->gameState->bindings.end() || !binding->second ||
      !binding->second->link.owner)
    return nullptr;
  const auto resolved = arena->gameState->resources->resolve(binding->second->link.owner);
  if (!resolved.packNode || !resolved.packNode->groundFactory)
    return nullptr;
  return resolved.packNode->groundFactory.get();
}

// FUN_00483d00 queries the road manager using FUN_00581a00 and returns
// the selected crossing's coordinates as a Vector3.
NativeResult groundGetNearestCross(const std::vector<VmValue>& args,
                                   const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>* classes) {
  if (!arena || args.size() != 2 || !std::holds_alternative<const VmObject*>(args[0]) ||
      !std::holds_alternative<float>(args[1]))
    return std::string{"GroundRef.getNearestCross: invalid arguments"};
  const VmObject* position = std::get<const VmObject*>(args[0]);
  GroundFactoryResult* factory = linkedGroundFactory(receiver, arena);
  if (!position || !factory || !factory->preparation || !factory->preparation->files.roads)
    return VmValue{static_cast<const VmObject*>(nullptr)};
  std::array<float, 3> query{};
  if (!readVector3Components(position, query[0], query[1], query[2]))
    return std::string{"GroundRef.getNearestCross: invalid position"};
  const auto& roads = *factory->preparation->files.roads;
  const auto nearest = roadNearestCrossing(roads, query, std::get<float>(args[1]), false);
  if (!nearest)
    return std::string{"GroundRef.getNearestCross: no supported crossing"};
  const size_t group = nearest->kind == RoadRecordKind::FirstNode ? 0 : 1;
  const auto& words = roads.nodes[group][nearest->index].words;
  std::array<float, 3> coordinates{};
  std::memcpy(coordinates.data(), words.data(), sizeof(coordinates));
  return allocateVector3(arena, classes, coordinates[0], coordinates[1], coordinates[2]);
}

NativeResult groundAddTrafficN(const std::vector<VmValue>& args, const VmObject* receiver,
                               VmArena* arena, const std::vector<const TufaClass*>*) {
  if (!arena || args.size() != 5 || !std::holds_alternative<const VmObject*>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]) || !std::holds_alternative<float>(args[2]) ||
      !std::holds_alternative<float>(args[3]) || !std::holds_alternative<float>(args[4]))
    return std::string{"GroundRef.addTrafficN: invalid arguments"};
  const int32_t count = std::get<int32_t>(args[1]);
  if (count <= 0)
    return VmValue{int32_t{0}};
  GroundFactoryResult* factory = linkedGroundFactory(receiver, arena);
  if (!factory)
    return VmValue{int32_t{0}};
  uint32_t templateId = 0;
  if (const auto* car = std::get<const VmObject*>(args[0])) {
    if (const auto* carState = nativeResourceState(car, arena)) {
      templateId = carState->words[2];
      if (arena->gameState) {
        const int32_t handle =
            static_cast<int32_t>(carState - arena->resourceStates.data()) + 1;
        const auto binding = arena->gameState->bindings.find(handle);
        if (binding != arena->gameState->bindings.end() && binding->second)
          templateId = binding->second->link.cachedValue;
      }
    }
  }
  const int placed = placeGroundTraffic(*factory, templateId, count, std::get<float>(args[2]),
                                        std::get<float>(args[3]), std::get<float>(args[4]));
  return VmValue{int32_t{placed}};
}

NativeResult groundSetPedestrianDensityN(const std::vector<VmValue>& args,
                                         const VmObject* receiver, VmArena* arena,
                                         const std::vector<const TufaClass*>*) {
  if (!arena || args.size() != 1 || !std::holds_alternative<float>(args[0]))
    return std::string{"GroundRef.setPedestrianDensityN: invalid arguments"};
  if (GroundFactoryResult* factory = linkedGroundFactory(receiver, arena))
    setGroundPedestrianDensity(*factory, std::get<float>(args[0]));
  return std::monostate{};
}

static uint32_t gameRefTemplateId(const VmObject* gameRef, VmArena* arena) {
  uint32_t templateId = 0;
  if (!gameRef || !arena)
    return templateId;
  if (const auto* state = nativeResourceState(gameRef, arena)) {
    templateId = state->words[2];
    if (arena->gameState) {
      const int32_t handle =
          static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
      const auto binding = arena->gameState->bindings.find(handle);
      if (binding != arena->gameState->bindings.end() && binding->second)
        templateId = binding->second->link.cachedValue;
    }
  }
  return templateId;
}

NativeResult groundAddPedestrianType(const std::vector<VmValue>& args,
                                     const VmObject* receiver, VmArena* arena,
                                     const std::vector<const TufaClass*>*) {
  if (!arena || args.size() != 1 || !std::holds_alternative<const VmObject*>(args[0]))
    return std::string{"GroundRef.addPedestrianType: invalid arguments"};
  GroundFactoryResult* factory = linkedGroundFactory(receiver, arena);
  if (!factory)
    return std::monostate{};
  const auto* gameRef = std::get<const VmObject*>(args[0]);
  addGroundPedestrianType(*factory, gameRefTemplateId(gameRef, arena));
  return std::monostate{};
}

NativeResult groundSetWaterVector3(const std::vector<VmValue>& args,
                                   const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>*) {
  if (!arena || args.size() != 4 || !std::holds_alternative<const VmObject*>(args[0]) ||
      !std::holds_alternative<const VmObject*>(args[1]) ||
      !std::holds_alternative<float>(args[2]) || !std::holds_alternative<float>(args[3]))
    return std::string{"GroundRef.setWater: invalid arguments"};
  GroundFactoryResult* factory = linkedGroundFactory(receiver, arena);
  if (!factory)
    return std::monostate{};
  float posX = 0.f;
  float posY = 0.f;
  float posZ = 0.f;
  float dirX = 0.f;
  float dirY = 0.f;
  float dirZ = 0.f;
  if (!readVector3Components(std::get<const VmObject*>(args[0]), posX, posY, posZ) ||
      !readVector3Components(std::get<const VmObject*>(args[1]), dirX, dirY, dirZ))
    return std::string{"GroundRef.setWater: invalid arguments"};
  setGroundWater(*factory, posX, posY, posZ, dirX, dirY, dirZ, std::get<float>(args[2]),
                 std::get<float>(args[3]));
  return std::monostate{};
}

NativeResult groundAddWaterLimit(const std::vector<VmValue>& args, const VmObject* receiver,
                                 VmArena* arena, const std::vector<const TufaClass*>*) {
  if (!arena || args.size() != 2 || !std::holds_alternative<const VmObject*>(args[0]) ||
      !std::holds_alternative<const VmObject*>(args[1]))
    return std::string{"GroundRef.addWaterLimit: invalid arguments"};
  GroundFactoryResult* factory = linkedGroundFactory(receiver, arena);
  if (!factory)
    return std::monostate{};
  float ax = 0.f;
  float ay = 0.f;
  float az = 0.f;
  float bx = 0.f;
  float by = 0.f;
  float bz = 0.f;
  if (!readVector3Components(std::get<const VmObject*>(args[0]), ax, ay, az) ||
      !readVector3Components(std::get<const VmObject*>(args[1]), bx, by, bz))
    return std::string{"GroundRef.addWaterLimit: invalid arguments"};
  addGroundWaterLimit(*factory, ax, ay, az, bx, by, bz);
  return std::monostate{};
}

NativeResult groundSetFog(const std::vector<VmValue>& args, const VmObject* receiver,
                          VmArena* arena, const std::vector<const TufaClass*>*) {
  if (!arena || args.size() != 3 || !std::holds_alternative<int32_t>(args[0]) ||
      !std::holds_alternative<float>(args[1]) || !std::holds_alternative<float>(args[2]))
    return std::string{"GroundRef.setFog: invalid arguments"};
  GroundFactoryResult* factory = linkedGroundFactory(receiver, arena);
  if (!factory)
    return std::monostate{};
  addGroundFog(*factory, std::get<int32_t>(args[0]), std::get<float>(args[1]),
               std::get<float>(args[2]));
  return std::monostate{};
}

const std::vector<NativeEntry> entries{
    {"java.lang.Object", "wait", "()V", "0047c920", objectWait},
    {"java.render.Viewport", "create", "(IFFFF)V", "004814e0", viewportCreate},
    {"java.render.Camera", "create",
     "(Ljava.util.resource.ResourceRef;Ljava.render.Viewport;IFFFFFII)V",
     "004861e0", cameraCreate},
    {"java.render.Camera", "destroy", "()V", "004864c0", cameraDestroy},
    {"java.util.resource.PhysicsRef", "createBox",
     "(Ljava.util.resource.ResourceRef;FFFLjava.lang.String;)V", "004805b0",
     physicsCreateBox},
    {"java.util.resource.PhysicsRef", "setMatrix",
     "(Ljava.lang.Vector3;Ljava.lang.Ypr;)V", "00480920", physicsSetMatrix},
    {"java.util.resource.PhysicsRef", "getPos", "()Ljava.lang.Vector3;",
     "00480b00", physicsGetPos},
    {"java.util.resource.PhysicsRef", "getOri", "()Ljava.lang.Ypr;", "00480c10",
     physicsGetOri},
    {"java.render.Viewport", "activate", "(I)V", "00481680", viewportActivate},
    {"java.render.Viewport", "deactivate", "()V", "004816c0",
     viewportDeactivate},
    {"java.render.Viewport", "destroy", "()V", "00481600", viewportDestroy},
    {"java.render.Viewport", "resize", "(FFFF)V", "00481700", viewportResize},
    {"java.render.Viewport", "getLeft", "()F", "00481970", viewportGetLeft},
    {"java.render.Viewport", "getTop", "()F", "004818f0", viewportGetTop},
    {"java.render.Viewport", "getWidth", "()F", "004817f0", viewportGetWidth},
    {"java.render.Viewport", "getHeight", "()F", "00481870", viewportGetHeight},
    {"java.render.Viewport", "getAspect", "()F", "004817b0", viewportGetAspect},
    {"java.lang.Object", "notify", "()V", "0047c950", objectNotify},
    {"java.lang.Object", "notifyAll", "()V", "0047c970", objectNotifyAll},
    {"java.lang.Thread", "init", "(Ljava.lang.String;)V", "0047c510",
     threadInit},
    {"java.lang.Thread", "setPriority", "(I)V", "0047c790", threadSetPriority},
    {"java.lang.Thread", "start", "()V", "0047c570", threadStart},
    {"java.lang.Thread", "stop", "()V", "0047c600", threadStop},
    {"java.lang.Thread", "sleep", "(F)V", "0047c650", threadSleep},
    {"java.lang.Thread", "isAlive", "()I", "0047c730", threadIsAlive},
    {"java.io.Input", "getDeviceName", "(I)Ljava.lang.String;", "0047c9c0",
     inputGetDeviceName},
    {"java.lang.Math", "random", "()F", "0047c820", mathRandom},
    {"java.lang.Math", "randomize", "()V", "0047c7d0", mathRandomize},
    {"java.lang.Math", "sqrt", "(F)F", "0047c850", mathSqrt},
    {"java.util.resource.RenderRef", "changeResource",
     "(Ljava.util.resource.ResourceRef;Ljava.util.resource.ResourceRef;)V",
     "00480220", renderRefChangeResource},
    {"java.util.resource.RenderRef", "create",
     "(Ljava.util.resource.ResourceRef;Ljava.util.resource.RenderRef;Ljava."
     "lang.String;)V",
     "00480ee0", renderRefCreate},
    {"java.util.resource.RenderRef", "setMatrix",
     "(Ljava.lang.Vector3;Ljava.lang.Ypr;)V", "00481240", renderRefSetMatrix},
    {"java.util.resource.RenderRef", "setMatrix",
     "(ILjava.util.resource.ResourceRef;Ljava.lang.Vector3;Ljava.lang.Ypr;)"
     "V",
     "004810b0", renderRefSetMatrixBone},
    {"java.util.resource.RenderRef", "setColor", "(I)V", "00480310",
     renderRefSetColor},
    {"java.util.resource.RenderRef", "setLight", "(III)V", "00486ab0",
     renderRefSetLight},
    {"java.util.resource.RenderRef", "setFlare",
     "(Ljava.util.resource.ResourceRef;IFFII)V", "00486b20",
     renderRefSetFlare},
    {"java.util.resource.RenderRef", "getTypeID", "()I", "00486be0",
     renderRefGetTypeId},
    {"java.util.resource.ResourceRef", "duplicate",
     "(Ljava.util.resource.ResourceRef;)V", "004802c0", resourceDuplicate},
    {"java.sound.Sound", "setVolume", "(IF)V", "00487580", soundSetVolume},
    {"java.sound.Sound", "getVolume", "(I)F", "004875f0", soundGetVolume},
    {"java.sound.Sound", "changeMusicSet", "(I)V", "00487460",
     soundChangeMusicSet},
    {"java.sound.Sound", "nextTrack", "()V", "00487500", soundNextTrack},
    {"java.sound.Sound", "prevTrack", "()V", "00487510", soundPrevTrack},
    {"java.sound.Sound", "setMode", "(I)I", "00487520", soundSetMode},
    {"java.sound.Sound", "getMode", "()I", "00487560", soundGetMode},
    {"java.lang.System", "getConfigOptions", "()V", "0047c500", systemGetConfigOptions},
    {"java.lang.System", "setMeasure", "(F)V", "0047c4b0", setMeasure},
    {"java.lang.System", "isLoading", "()I", "0047c3b0", systemIsLoading},
    {"java.lang.System", "isLoadingReset", "()V", "0047c3a0", systemIsLoadingReset},
    {"java.lang.System", "currentTime", "()F", "0047bfe0", systemCurrentTime},
    {"java.lang.System", "syncGameTime", "(F)V", "0047bff0",
     systemSyncGameTime},
    {"java.lang.System", "timeWarp", "(F)F", "0047bf80", systemTimeWarp},
    {"java.lang.System", "setLdPriority", "(I)V", "0047bf30",
     systemSetLdPriority},
    {"java.io.Input", "createHotkey",
     "(IILjava.io.Hotkey;Ljava.util.resource.GameRef;Ljava.render.Osd;I)V",
     "0047cc50", inputCreateHotkey},
    {"java.io.Input", "deleteHotkey", "(Ljava.io.Hotkey;)V", "0047cd00",
     inputDeleteHotkey},
    {"java.io.Input", "flushHotkeys", "()V", "0047ce80", inputFlushHotkeys},
    {"java.io.Input", "checkHotkeys",
     "(Ljava.util.resource.GameRef;Ljava.render.Osd;)V", "0047cd40",
     inputCheckHotkeys},
    {"java.io.MouseCursor", "getPos", "()Ljava.lang.Vector3;", "00487970",
     mouseCursorGetPos},
    {"java.io.Input", "lastKey", "()I", "0047cc10", inputLastKey},
    {"java.io.Input", "getAxis", "(II)F", "0047c990", inputGetAxis},
    {"java.io.Input", "axisName", "(II)Ljava.lang.String;", "0047cb00", inputAxisName},
    {"java.io.Input", "activeAxis", "(I)I", "0047cae0", inputActiveAxis},
    {"java.io.Input", "mapAxis", "(Ljava.util.resource.GameRef;IIIFFFF)V", "0047c9f0", inputMapAxis},
    {"java.io.Input", "setAxisSmooth", "(Ljava.util.resource.GameRef;IFFFF)V", "0047ca70", inputSetAxisSmooth},
    {"java.io.Controller", "user_SetAxisSpeed", "(IF)V", "00477970", controllerUserSetAxisSpeed},
    {"java.io.Controller", "user_GetAxisVal", "(I)F", "0047a800", controllerUserGetAxisVal},
    {"java.io.Controller", "user_SetAxisForce", "(IFF)V", "0047b200", controllerUserSetAxisForce},
    {"java.io.File", "exists", "(Ljava.lang.String;)I", "00486170", fileExists},
    {"java.io.File", "delete", "(Ljava.lang.String;)I", "00486150", fileDelete},
    {"java.io.File", "copy", "(Ljava.lang.String;Ljava.lang.String;)I",
     "004860f0", fileCopy},
    {"java.io.File", "move", "(Ljava.lang.String;Ljava.lang.String;)I",
     "00486120", fileMove},
    {"java.io.FindFile", "first", "(Ljava.lang.String;I)Ljava.lang.String;",
     "00487cb0", findFileFirst},
    {"java.io.FindFile", "next", "()Ljava.lang.String;", "00487da0",
     findFileNext},
    {"java.io.FindFile", "close", "()V", "00487e70", findFileClose},
    {"java.io.File", "open", "(I)I", "00485440", fileOpenRead},
    {"java.io.File", "readInt", "()I", "00485b70", fileReadInt},
    {"java.io.File", "readFloat", "()F", "00485c10", fileReadFloat},
    {"java.io.File", "close", "()V", "00485870", fileClose},
    {"java.io.File", "write", "(I)I", "00485b20", fileWriteInt},
    {"java.io.File", "write", "(F)I", "00485bc0", fileWriteFloat},
    {"java.io.File", "write", "(Ljava.util.resource.ResourceRef;)I",
     "00485c60", fileWriteResource},
    {"java.io.File", "write", "(Ljava.lang.String;)I", "00485fd0",
     fileWriteString},
    {"java.io.File", "readResID", "()I", "00485ea0", fileReadResId},
    {"java.io.File", "readString", "()Ljava.lang.String;", "00486060",
     fileReadString},
    {"java.lang.System", "openLib", "(Ljava.lang.String;)I", "00487be0",
     systemOpenLib},
    {"java.lang.System", "buildNumber", "()I", "0047c0b0", buildNumber},
    {"java.sound.Sound", "enableDebugDump",
     "(Ljava.util.resource.ResourceRef;)I", "00487f00", enableDebugDump},
    {"java.util.resource.ResourceRef", "newNative", "()V", "0047cea0",
     resourceNewNative},
    {"java.util.resource.ResourceRef", "set", "(I)V", "0047cf80",
     resourceRefSetInt},
    {"java.render.Text", "setDefaultColor", "(I)V", "00487020",
     textSetDefaultColor},
    {"java.render.Text", "getDefaultColor", "()I", "00487040",
     textGetDefaultColor},
    {"java.render.Text", "create",
     "(Ljava.util.resource.ResourceRef;Ljava.util.resource.ResourceRef;FF)V",
     "00487050", textCreate},
    {"java.render.Text", "update", "()V", "00487350", textUpdate},
    {"java.render.Text", "changeAlign", "(I)V", "004871d0", textChangeAlign},
    {"java.render.Text", "changeColor", "(I)V", "00487150", textChangeColor},
    {"java.render.Text", "setPos", "(FF)V", "004870d0", textSetPos},
    {"java.render.Text", "getWidthPixels",
     "(Ljava.lang.String;Ljava.util.resource.ResourceRef;)F", "00487410",
     textGetWidthPixels},
    {"java.render.GfxEngine", "openVideo", "(Ljava.lang.String;II)I",
     "0047c330", gfxOpenVideo},
    {"java.render.GfxEngine", "closeVideo", "()V", "0047c380", gfxCloseVideo},
    {"java.render.GfxEngine", "isPlayingVideo", "()I", "0047c390",
     gfxIsPlayingVideo},
    {"java.render.GfxEngine", "setGlobalEnvmap",
     "(Ljava.util.resource.ResourceRef;)V", "0047c220", gfxSetGlobalEnvmap},
    {"java.render.GfxEngine", "forceRendering", "()V", "0047c1d0",
     gfxForceRendering},
    {"java.render.GfxEngine", "flush", "()V", "0047c2d0", gfxFlush},
    {"java.game.Navigator", "updateNavigator",
     "(Ljava.util.resource.GameRef;I)V", "00482d30", navigatorUpdate},
    {"java.render.GfxEngine", "changeVideoMode", "(III)V", "0047c3f0", gfxChangeVideoMode},
    {"java.render.GfxEngine", "numDisplayModes", "()I", "0047c440",
     gfxNumDisplayModes},
    {"java.render.GfxEngine", "currDisplayMode", "()I", "0047c450",
     gfxCurrDisplayMode},
    {"java.render.GfxEngine", "displayModeName", "(I)Ljava.lang.String;",
     "0047c460", gfxDisplayModeName},
    {"java.lang.GameType", "registerCallback", "(I)V", "00481c40",
     gameTypeRegisterCallback},
    {"java.lang.GameType", "unregisterCallback", "(I)V", "00481c80",
     gameTypeUnregisterCallback},
    {"java.lang.GameType", "unregisterCallbacks", "()V", "00481cc0",
     gameTypeUnregisterCallbacks},
    {"java.lang.GameType", "addTimer", "(FI)V", "0047e1c0", gameTypeAddTimer},
    {"java.lang.GameType", "removeAllTimers", "()V", "0047e260",
     gameTypeRemoveAllTimers},
    {"java.util.resource.ResourceRef", "cache", "()V", "0047e8d0",
     resourceRefCache},
    {"java.util.resource.ResourceRef", "precache", "()V", "0047e950",
     resourceRefPrecache},
    {"java.util.resource.SfxRef", "nplay", "(Ljava.lang.Vector3;FFFII)I",
     "00480d40", sfxPlay},
    {"java.util.resource.SfxRef", "stop", "(I)V", "00480e60", sfxStop},
    {"java.util.resource.ResourceRef", "getFirstChild",
     "()Ljava.util.resource.ResourceRef;", "0047d630", resourceRefFirstChild},
    {"java.util.resource.ResourceRef", "getNextChild",
     "()Ljava.util.resource.ResourceRef;", "0047d6f0", resourceRefNextChild},
    {"java.util.resource.ResourceRef", "id", "()I", "0047d290", resourceRefId},
    {"java.util.resource.ResourceRef", "scaleMesh", "(FFF)V", "00480390",
     resourceRefScaleMesh},
    {"java.util.resource.GameRef", "isScripted", "(Ljava.lang.String;)I",
     "00486da0", gameRefIsScripted},
    {"java.util.resource.GameRef", "create",
     "(Ljava.util.resource.GameRef;Ljava.util.resource.GameRef;Ljava.lang."
     "String;Ljava.lang.String;)Ljava.lang.GameType;",
     "0047d7b0", gameRefCreate},
    {"java.util.resource.GameRef", "setFlags", "(I)V", "00486c80",
     gameRefSetFlags},
    {"java.lang.GameType", "setEventMask", "(I)V", "004819f0",
     gameTypeSetEventMask},
    {"java.lang.GameType", "addNotification",
     "(Ljava.util.resource.GameRef;IILjava.lang.String;)V", "0047e050",
     gameTypeAddNotification},
    {"java.lang.GameType", "addNotification",
     "(Ljava.util.resource.GameRef;IILjava.lang.String;Ljava.lang.String;)V",
     "0047e100", gameTypeAddNotification},
    {"java.lang.GameType", "clearEventMask", "(I)V", "00481a30",
     gameTypeClearEventMask},
    {"java.util.resource.GameRef", "clearFlags", "(I)V", "00486cd0",
     gameRefClearFlags},
    {"java.util.resource.GameRef", "queueEvent",
     "(Ljava.util.resource.ResourceRef;ILjava.lang.String;)V", "0047da30",
     gameRefQueueEvent},
    {"java.io.Controller", "user_Reset", "()V", "00477a00",
     controllerUserReset},
    {"java.io.Controller", "user_Del", "(III)I", "00477810", controllerUserDel},
    {"java.io.Controller", "user_Add", "(IIIFFFF)I", "00477740",
     controllerUserAdd},
    {"java.io.Controller", "user_SetAxisSmooth", "(IFFFFF)I", "004778b0",
     controllerUserSetAxisSmooth},
    {"java.lang.GameType", "createNativeInstance",
     "(Ljava.util.resource.GameRef;ILjava.lang.String;Ljava.lang.String;)V",
     "00481a70", gameTypeCreateNativeInstance},
    {"java.lang.Object", "toString", "()Ljava.lang.String;", "0047c870",
     objectToString},
    {"java.lang.String", "intValue", "()I", "00481ed0", stringIntValue},
    {"java.lang.String", "floatValue", "()F", "00481f10", stringFloatValue},
    {"java.lang.Float", "toString", "(FLjava.lang.String;)Ljava.lang.String;",
     "00481dd0", floatToString},
    {"java.lang.Integer", "toString", "(I)Ljava.lang.String;", "00481e30",
     integerToString},
    {"java.lang.String", "append", "(I)Ljava.lang.String;", "00481f50",
     stringAppend},
    {"java.lang.String", "length", "()I", "00481fe0", stringLength},
    {"java.lang.String", "token", "(ILjava.lang.String;)Ljava.lang.String;",
     "00481cf0", stringToken},
    {"java.lang.Vector3", "length", "()F", "004823e0", vector3Length},
    {"java.lang.Vector3", "distance", "(Ljava.lang.Vector3;)F", "00482550",
     vector3Distance},
    {"java.lang.Vector3", "normalize", "()V", "00482450", vector3Normalize},
    {"java.lang.Vector3", "rotate", "(Ljava.lang.Ypr;)Ljava.lang.Vector3;",
     "00482110", vector3Rotate},
    {"java.lang.Ypr", "<init>", "(Ljava.lang.Vector3;)V", "00482610",
     yprFromVector3},
    {"java.lang.System", "arraycopy",
     "(Ljava.lang.Object;ILjava.lang.Object;II)V", "00487660", systemArraycopy},
    {"java.lang.System", "exit", "(Ljava.lang.String;)V", "0047be60",
     systemExitString},
    {"java.lang.System", "log", "(Ljava.lang.String;)V", "0047be80", systemLog},
    {"java.util.resource.ResourceRef", "deleteNative", "()V", "0047cf00",
     resourceRefDeleteNative},
    {"java.util.resource.ResourceRef", "set",
     "(Ljava.util.resource.ResourceRef;)V", "0047cfc0", resourceRefSetRef},
    {"java.util.resource.ResourceRef", "load", "()V", "0047d0a0",
     resourceRefLoad},
    {"java.util.resource.ResourceRef", "unload", "()V", "0047d170",
     resourceRefUnload},
    {"java.util.resource.ResourceRef", "destroy", "()V", "0047d1e0",
     resourceRefDestroy},
    {"java.util.resource.ResourceRef", "type", "()I", "0047d210",
     resourceRefType},
    {"java.util.resource.ResourceRef", "getParentID", "()I", "0047d3e0",
     resourceRefGetParentId},
    {"java.util.resource.ResourceRef", "getParent",
     "()Ljava.util.resource.ResourceRef;", "0047d4b0", resourceRefGetParent},
    {"java.util.resource.ResourceRef", "getWTRoot",
     "()Ljava.util.resource.ResourceRef;", "0047d570", resourceRefGetWTRoot},
    {"java.lang.GameType", "remNotification",
     "(Ljava.util.resource.GameRef;I)V", "0047e000", gameTypeRemNotification},
    {"java.util.resource.GameRef", "create_native",
     "(Ljava.util.resource.GameRef;Ljava.util.resource.GameRef;Ljava.lang."
     "String;Ljava.lang.String;)V",
     "0047d900", gameRefCreateNative},
    {"java.util.resource.GameRef", "getDetail", "()F", "00480440",
     gameRefGetDetail},
    {"java.util.resource.GameRef", "getFlags", "()I", "0047df40",
     gameRefGetFlags},
    {"java.util.resource.GameRef", "getInfo", "(II)I", "0047ddf0",
     gameRefGetInfoIntInt},
    {"java.util.resource.GameRef", "getInfo", "(ILjava.lang.String;)I",
     "0047ddf0", gameRefGetInfoIntString},
    {"java.util.resource.GameRef", "getOri", "()Ljava.lang.Ypr;", "0047dbe0",
     gameRefGetOri},
    {"java.util.resource.GameRef", "getPos", "()Ljava.lang.Vector3;",
     "0047dad0", gameRefGetPos},
    {"java.util.resource.GameRef", "getScriptInstance", "()Ljava.lang.Object;",
     "00486f30", gameRefGetScriptInstance},
    {"java.util.resource.GameRef", "getVel", "()Ljava.lang.Vector3;",
     "0047dce0", gameRefGetVel},
    {"java.util.resource.GameRef", "isEmpty", "()I", "00486d10",
     gameRefIsEmpty},
    {"java.util.resource.GameRef", "setActiveCollision", "()V", "0047df80",
     gameRefSetActiveCollision},
    {"java.util.resource.GameRef", "setMatrix",
     "(Ljava.lang.Vector3;Ljava.lang.Ypr;)V", "0047e490", gameRefSetMatrix},
    {"java.util.resource.GameRef", "setParent",
     "(Ljava.util.resource.ResourceRef;)V", "0047e2d0", gameRefSetParent},
    {"java.util.resource.GameRef", "setPos", "(Ljava.lang.Vector3;)V",
     "0047e350", gameRefSetPos},
    {"java.util.resource.GameRef", "setState",
     "(Ljava.lang.Vector3;Ljava.lang.Ypr;Ljava.lang.Vector3;Ljava.lang.Vector3;"
     ")V",
     "0047e630", gameRefSetState},
    {"java.game.parts.Part", "disableSlot", "(II)V", "00469770",
     partDisableSlot},
    {"java.game.parts.Part", "flap", "(I)I", "0046a5a0", partFlap},
    {"java.game.parts.Part", "getCar", "()I", "004690a0", partGetCar},
    {"java.game.parts.Part", "getCarRef", "()Ljava.game.parts.Part;",
     "00469120", partGetCarRef},
    {"java.game.parts.Part", "getLogo", "()I", "0046a0e0", partGetLogo},
    {"java.game.parts.Part", "getMass", "()F", "00469290", partGetMass},
    {"java.game.parts.Part", "getMesh", "()I", "00469cc0", partGetMesh},
    {"java.game.parts.Part", "getRenderType", "()I", "00469ed0",
     partGetRenderType},
    {"java.game.parts.Part", "getSlotDamage", "(I)Ljava.lang.String;",
     "00468ab0", partGetSlotDamage},
    {"java.game.parts.Part", "getSlotID", "(I)I", "00468570", partGetSlotId},
    {"java.game.parts.Part", "getSlotIndex", "(I)I", "004686e0",
     partGetSlotIndex},
    {"java.game.parts.Part", "getSlots", "()I", "004684a0", partGetSlots},
    {"java.game.parts.Part", "getTear", "()F", "004695d0", partGetTear},
    {"java.game.parts.Part", "getTexture", "()I", "00469ab0", partGetTexture},
    {"java.game.parts.Part", "getWear", "()F", "00469340", partGetWear},
    {"java.game.parts.Part", "getWheelID", "()I", "004691f0", partGetWheelId},
    {"java.game.parts.Part", "install_OK",
     "(Ljava.util.resource.GameRef;ILjava.util.resource.GameRef;ILjava.lang."
     "Vector3;)[I",
     "0046a680", partInstallOk},
    {"java.game.parts.Part", "isSlotDisabled", "(I)I", "00469970",
     partIsSlotDisabled},
    {"java.game.parts.Part", "partOnSlot", "(I)Ljava.game.parts.Part;",
     "004687f0", partPartOnSlot},
    {"java.game.parts.Part", "setMaxWear", "(F)V", "00469500", partSetMaxWear},
    {"java.game.parts.Part", "setMesh", "(I)I", "00469d90", partSetMesh},
    {"java.game.parts.Part", "setRenderType", "(I)I", "00469fa0",
     partSetRenderType},
    {"java.game.parts.Part", "setSfxLoopParams", "(FF)I", "0046a1b0",
     partSetSfxLoopParams},
    {"java.game.parts.Part", "setSlotDamage", "(ILjava.lang.String;)V",
     "00468c50", partSetSlotDamage},
    {"java.game.parts.Part", "setSlotPos",
     "(ILjava.lang.Vector3;Ljava.lang.Ypr;)V", "00468df0", partSetSlotPos},
    {"java.game.parts.Part", "setTear", "(F)F", "004696a0", partSetTear},
    {"java.game.parts.Part", "setTexture", "(I)I", "00469b80", partSetTexture},
    {"java.game.parts.Part", "setWear", "(F)F", "00469420", partSetWear},
    {"java.game.parts.Part", "slotIDOnSlot", "(I)I", "00468990",
     partSlotIdOnSlot},
    {"java.game.parts.DynoData", "calcDyno", "(F)F", "0046aa20",
     dynoDataCalcDyno},
    {"java.game.parts.DynoData", "deleteNative", "()V", "0046a9c0",
     dynoDataDeleteNative},
    {"java.game.parts.DynoData", "getHP", "(FF)F", "0046b210", dynoDataGetHp},
    {"java.game.parts.DynoData", "getTorque", "(FF)F", "0046b0f0",
     dynoDataGetTorque},
    {"java.game.parts.DynoData", "newNative", "()V", "0046a960",
     dynoDataNewNative},
    {"java.game.parts.bodypart.Chassis", "forceUpdate", "()V", "0043e320",
     chassisForceUpdate},
    {"java.game.parts.bodypart.Chassis", "getCM", "()Ljava.lang.Vector3;",
     "0043cb80", chassisGetCm},
    {"java.game.parts.bodypart.Chassis", "getMass", "()F", "0043caa0",
     chassisGetMass},
    {"java.game.parts.bodypart.Chassis", "getMax", "()Ljava.lang.Vector3;",
     "0043d8b0", chassisGetMax},
    {"java.game.parts.bodypart.Chassis", "getMileage", "()F", "0043dff0",
     chassisGetMileage},
    {"java.game.parts.bodypart.Chassis", "getMin", "()Ljava.lang.Vector3;",
     "0043d600", chassisGetMin},
    {"java.game.parts.bodypart.Chassis", "getSfxTable",
     "(I)Ljava.game.parts.SfxTable;", "00442210", chassisGetSfxTable},
    {"java.game.parts.bodypart.Chassis", "getTorque", "(FF)F", "0043c7f0",
     chassisGetTorque},
    {"java.game.parts.bodypart.Chassis", "getWheel",
     "(I)Ljava.game.parts.WheelRef;", "00440c80", chassisGetWheel},
    {"java.game.parts.bodypart.Chassis", "getWheelDamage",
     "(I)Ljava.lang.String;", "0043d080", chassisGetWheelDamage},
    {"java.game.parts.bodypart.Chassis", "getWheelPos",
     "(I)Ljava.lang.Vector3;", "0043ce30", chassisGetWheelPos},
    {"java.game.parts.bodypart.Chassis", "getWheels", "()I", "0043cd70",
     chassisGetWheels},
    {"java.game.parts.bodypart.Chassis", "setAckermann", "(F)V", "00440b70",
     chassisSetAckermann},
    {"java.game.parts.bodypart.Chassis", "setBuck", "(IIFFFF)I", "0043e110",
     chassisSetBuck},
    {"java.game.parts.bodypart.Chassis", "setCooling", "(FFF)V", "0043deb0",
     chassisSetCooling},
    {"java.game.parts.bodypart.Chassis", "setHornSFX",
     "(Ljava.util.resource.ResourceRef;FI)V", "0043dc00", chassisSetHornSfx},
    {"java.game.parts.bodypart.Chassis", "setMileage", "(F)V", "0043e080",
     chassisSetMileage},
    {"java.game.parts.bodypart.Chassis", "setNitroSFX",
     "(Ljava.util.resource.ResourceRef;F)V", "0043dd70", chassisSetNitroSfx},
    {"java.game.parts.bodypart.Chassis", "setSfxExhaustMinVol", "(F)V",
     "004409d0", chassisSetSfxExhaustMinVol},
    {"java.game.parts.bodypart.Chassis", "setSteerWheel", "(FF)V", "00440a50",
     chassisSetSteerWheel},
    {"java.game.parts.bodypart.Chassis", "setSteerWheelRadius", "(F)V",
     "00440af0", chassisSetSteerWheelRadius},
    {"java.game.parts.bodypart.Chassis", "setTorque", "(F)V", "0043c930",
     chassisSetTorque},
    {"java.game.parts.bodypart.Chassis", "setWheelDamage",
     "(ILjava.lang.String;)V", "0043d280", chassisSetWheelDamage},
    {"java.game.parts.WheelRef", "getBrake", "()F", "00441540",
     wheelRefGetBrake},
    {"java.game.parts.WheelRef", "getDrive", "()F", "00440dc0",
     wheelRefGetDrive},
    {"java.game.parts.WheelRef", "getHBrake", "()F", "00441600",
     wheelRefGetHBrake},
    {"java.game.parts.WheelRef", "getPos", "()Ljava.lang.Vector3;", "00441b60",
     wheelRefGetPos},
    {"java.game.parts.WheelRef", "getRadius", "()F", "00440f50",
     wheelRefGetRadius},
    {"java.game.parts.WheelRef", "getSteer", "()F", "00440e50",
     wheelRefGetSteer},
    {"java.game.parts.WheelRef", "getYpr", "()Ljava.lang.Ypr;", "00441fd0",
     wheelRefGetYpr},
    {"java.game.parts.WheelRef", "setArm", "(FFFFFFF)V", "00441770",
     wheelRefSetArm},
    {"java.game.parts.WheelRef", "setBearing", "(F)V", "00441150",
     wheelRefSetBearing},
    {"java.game.parts.WheelRef", "setBrake", "(F)V", "00441580",
     wheelRefSetBrake},
    {"java.game.parts.WheelRef", "setCPatch", "(FFF)V", "00441700",
     wheelRefSetCPatch},
    {"java.game.parts.WheelRef", "setDamping", "(F)V", "004412e0",
     wheelRefSetDamping1},
    {"java.game.parts.WheelRef", "setDamping", "(FF)V", "00441360",
     wheelRefSetDamping2},
    {"java.game.parts.WheelRef", "setDrive", "(F)V", "00440e00",
     wheelRefSetDrive},
    {"java.game.parts.WheelRef", "setForce", "(F)V", "00441270",
     wheelRefSetForce},
    {"java.game.parts.WheelRef", "setFriction", "(F)V", "00440f90",
     wheelRefSetFriction},
    {"java.game.parts.WheelRef", "setFrictn_x", "(F)V", "00441050",
     wheelRefSetFrictnX},
    {"java.game.parts.WheelRef", "setHBrake", "(F)V", "00441640",
     wheelRefSetHBrake},
    {"java.game.parts.WheelRef", "setHub", "(FFFFFFFFFF)V", "00441880",
     wheelRefSetHub},
    {"java.game.parts.WheelRef", "setInstantCenter", "(FFFFFF)V", "00441a80",
     wheelRefSetInstantCenter},
    {"java.game.parts.WheelRef", "setLoadSmooth", "(F)V", "004411d0",
     wheelRefSetLoadSmooth},
    {"java.game.parts.WheelRef", "setMaxLen", "(F)V", "004414d0",
     wheelRefSetMaxLen},
    {"java.game.parts.WheelRef", "setMaxLoad", "(F)V", "00441190",
     wheelRefSetMaxLoad},
    {"java.game.parts.WheelRef", "setMinLen", "(F)V", "00441460",
     wheelRefSetMinLen},
    {"java.game.parts.WheelRef", "setOppWheel", "(I)V", "00441a10",
     wheelRefSetOppWheel},
    {"java.game.parts.WheelRef", "setPacejka", "(IF)V", "00441210",
     wheelRefSetPacejka},
    {"java.game.parts.WheelRef", "setPos", "(Ljava.lang.Vector3;)V", "00441d10",
     wheelRefSetPos},
    {"java.game.parts.WheelRef", "setRadius", "(F)V", "00440ed0",
     wheelRefSetRadius},
    {"java.game.parts.WheelRef", "setRestLen", "(F)V", "004413e0",
     wheelRefSetRestLen},
    {"java.game.parts.WheelRef", "setRollRes", "(F)V", "00441110",
     wheelRefSetRollRes},
    {"java.game.parts.WheelRef", "setSliction", "(F)V", "00441090",
     wheelRefSetSliction},
    {"java.game.parts.WheelRef", "setSteer", "(F)V", "00440e90",
     wheelRefSetSteer},
    {"java.game.parts.WheelRef", "setStiffness", "(F)V", "004410d0",
     wheelRefSetStiffness},
    {"java.game.parts.WheelRef", "setWidth", "(F)V", "004416c0",
     wheelRefSetWidth},
    {"java.game.parts.WheelRef", "setYpr", "(Ljava.lang.Ypr;)V", "00442090",
     wheelRefSetYpr},
    {"java.game.parts.SfxTable", "addItem",
     "(Ljava.util.resource.ResourceRef;FFFFF)V", "004423e0", sfxTableAddItem},
    {"java.game.parts.SfxTable", "clear", "()V", "00442350", sfxTableClear},
    {"java.game.parts.SfxTable", "getItems", "()I", "00442310",
     sfxTableGetItems},
    {"java.game.Vehicle", "getHorn", "()I", "0043db60", vehicleGetHorn},
    {"java.game.Vehicle", "getSpeedSquare", "()F", "00480500",
     vehicleGetSpeedSquare},
    {"java.game.Vehicle", "hasCrime", "()F", "00440bf0", vehicleHasCrime},
    {"java.util.resource.GroundRef", "alignToRoad",
     "(Ljava.lang.Vector3;)[Ljava.lang.Vector3;", "00483e60", groundAlignToRoad},
    {"java.util.resource.GroundRef", "getStartDirection",
     "(Ljava.lang.Vector3;Ljava.lang.Vector3;)Ljava.lang.Vector3;", "00483400",
     groundGetStartDirection},
    {"java.util.resource.GroundRef", "haltTrafficPath",
     "(Ljava.lang.Vector3;Ljava.lang.Vector3;)V", "004835e0", groundHaltTrafficPath},
    {"java.util.resource.GroundRef", "getNearestCross",
     "(Ljava.lang.Vector3;F)Ljava.lang.Vector3;", "00483d00", groundGetNearestCross},
    {"java.util.resource.GroundRef", "addTrafficN",
     "(Ljava.util.resource.GameRef;IFFF)I", "00484050", groundAddTrafficN},
    {"java.util.resource.GroundRef", "setPedestrianDensityN", "(F)V", "00484d30",
     groundSetPedestrianDensityN},
    {"java.util.resource.GroundRef", "addPedestrianType",
     "(Ljava.util.resource.GameRef;)V", "00484d90", groundAddPedestrianType},
    {"java.util.resource.GroundRef", "setWater",
     "(Ljava.lang.Vector3;Ljava.lang.Vector3;FF)V", "004867b0", groundSetWaterVector3},
    {"java.util.resource.GroundRef", "addWaterLimit",
     "(Ljava.lang.Vector3;Ljava.lang.Vector3;)V", "00486920", groundAddWaterLimit},
    {"java.util.resource.GroundRef", "setFog", "(IFF)V", "00486a20", groundSetFog},
};
} // namespace

const std::vector<NativeEntry>& nativeRegistry() { return entries; }

NativeResult invokeNative(std::string_view className, std::string_view method,
                          std::string_view descriptor,
                          const std::vector<VmValue>& args,
                          const VmObject* receiver, VmArena* arena,
                          const std::vector<const TufaClass*>* classes) {
  for (const auto& entry : nativeRegistry()) {
    if (entry.className == className && entry.method == method &&
        entry.descriptor == descriptor)
      return entry.handler(args, receiver, arena, classes);
  }
  return std::string{"native not registered"};
}
