#include "natives/resource_ref.hpp"
#include "run_log.hpp"

#include "natives/game_type.hpp"
#include "render2d.hpp"
#include "resource_sound.hpp"
#include "resource_texture_cache.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"

#include <algorithm>
#include <cstring>

NativeResult
resourceRefPrecache(const std::vector<VmValue> &args, const VmObject *receiver,
                    VmArena *arena,
                    const std::vector<const TufaClass *> *classes) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources || !arena->gameState->world)
    return std::string{"ResourceRef.precache: invalid call or host"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"ResourceRef.precache: invalid resource state"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second ||
      !binding->second->link.cachedValue)
    return std::monostate{};
  const auto node = game.resources->resolve(binding->second->link.owner);
  if (!node || node.type != 6 || !node.packNode)
    return std::string{"ResourceRef.precache: unsupported resource"};
  auto &packed = *node.packNode;
  if (!packed.payloadSize)
    return resourceRefCache(args, receiver, arena, classes);
  if (!packed.field6c && packed.stamp7c >= game.world->frameCounter())
    return std::string{"ResourceRef.precache: unknown load value"};
  if (packed.stamp7c < game.world->frameCounter() ||
      (packed.field6c && *packed.field6c < 1.0f))
    packed.field6c = 1.0f;
  if (!packed.loaded()) {
    std::string error;
    if (!game.resources->queueSoundPrecache(packed, error))
      return "ResourceRef.precache: " + error;
    return std::monostate{};
  }
  if (!packed.sound || packed.loadPriority < 1.0f)
    return std::string{
        "ResourceRef.precache: loaded sound refresh unsupported"};
  if (packed.stamp74 != game.world->frameCounter() && !packed.recentUseQueued)
    packed.stateWord = (packed.stateWord & ~uint32_t{0x60}) | 0x10;
  game.world->noteRecentUse(*node.owner, node.type, packed.stamp74,
                            packed.recentUseQueued);
  return std::monostate{};
}

NativeResult cacheDrawMaterial(ResourceWorld &resources, Render2D &renderer,
                               const ResolvedNode &resolved) {
  if (!resolved || resolved.type != 14 ||
      (!resolved.packNode && !resolved.worldNode))
    return std::string{"ResourceRef.cache: font resource state unavailable"};
  if (resolved.packNode && (resolved.packNode->flags & 0x2000000u))
    return std::string{"ResourceRef.cache: synchronous load of a pending "
                       "resource unsupported"};
  auto material = resources.sceneMaterial(resolved);
  if (!material) {
    if (!resolved.packNode || resolved.packNode->loaded())
      return std::string{"ResourceRef.cache: loaded material unavailable"};
    auto parsed = sceneLoadMaterial(resources, resolved.id);
    if (!parsed.material)
      return "ResourceRef.cache: unsupported material: " + parsed.error;
    material = std::move(parsed.material);
  }
  // Fonts carry flags 0/4100/4096; general render materials (e.g.
  // particles.rpk) carry other flag words such as 256. The mesh font loader
  // key stays flags==4100.
  if (material->textureCount() != 1)
    return std::string{"ResourceRef.cache: unsupported material texture count"};
  const auto meshNode = resources.lookup(material->meshId(), 5);
  const auto textureNode = resources.lookup(material->textureId(0), 7);
  if (meshNode.status != LookupStatus::Found ||
      (!meshNode.node.packNode && !meshNode.node.worldNode) ||
      textureNode.status != LookupStatus::Found || !textureNode.node.packNode)
    return std::string{"ResourceRef.cache: font dependencies unavailable"};
  auto mesh = resources.sceneMesh(meshNode.node);
  std::string error;
  if (!mesh) {
    if (!meshNode.node.packNode || meshNode.node.packNode->loaded())
      return std::string{"ResourceRef.cache: loaded font mesh unavailable"};
    mesh = std::make_shared<ScxModel>();
    if (!sceneLoadMesh(resources, meshNode.node.id, resources.packs().root(),
                       *mesh, error, material->flags() == 4100))
      return "ResourceRef.cache: " + error;
  }
  if (!resources.sceneMaterial(resolved) &&
      !resources.bindSceneMaterial(resolved, material))
    return std::string{"ResourceRef.cache: font material binding failed"};
  if (!resources.sceneMesh(meshNode.node) &&
      !resources.bindSceneMesh(meshNode.node, mesh))
    return std::string{"ResourceRef.cache: font mesh binding failed"};
  for (const auto &item : {resolved, meshNode.node}) {
    if (item.worldNode) {
      auto &live = *item.worldNode;
      if (!live.loaded)
        return std::string{
            "ResourceRef.cache: runtime material dependency not loaded"};
      const uint32_t stamp = item.type == 14 ? live.stamp78 : live.stamp7c;
      const float demand =
          item.type == 14
              ? std::clamp(live.visibilityBias + live.visibilityGain, 0.0f,
                           1.0f)
              : 1.0f;
      if (stamp < resources.world().frameCounter() ||
          (live.field6c && *live.field6c < demand))
        live.field6c = demand;
      else if (!live.field6c)
        return std::string{"ResourceRef.cache: runtime demand unavailable"};
      resources.world().noteRecentUse(*item.owner, item.type, live.stamp74,
                                      live.recentUseQueued);
      continue;
    }
    auto &packed = *item.packNode;
    const uint32_t stamp = item.type == 14 ? packed.stamp78 : packed.stamp7c;
    const float demand =
        item.type == 14
            ? std::clamp(packed.visibilityBias + packed.visibilityGain, 0.0f,
                         1.0f)
            : 1.0f;
    if (stamp < resources.world().frameCounter() ||
        (packed.field6c && *packed.field6c < demand))
      packed.field6c = demand;
    else if (!packed.field6c)
      return std::string{"ResourceRef.cache: font demand unavailable"};
    if (!packed.loaded()) {
      packed.stateWord = (packed.stateWord & ~uint32_t{0x50}) | 0x21;
      packed.loadPriority = 1.0f;
      resources.world().trackLoadState(*item.owner, item.type, true);
    }
    if (packed.stamp74 != resources.world().frameCounter() &&
        !packed.recentUseQueued)
      packed.stateWord = (packed.stateWord & ~uint32_t{0x60}) | 0x10;
    resources.world().noteRecentUse(*item.owner, item.type, packed.stamp74,
                                    packed.recentUseQueued);
  }
  if (!cacheResourceTexture(resources, renderer, textureNode.node, 1.0f, error))
    return "ResourceRef.cache: font texture: " + error;
  return std::monostate{};
}

NativeResult resourceRefCache(const std::vector<VmValue> &args,
                              const VmObject *receiver, VmArena *arena,
                              const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty())
    return std::string{"ResourceRef.cache: invalid call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state || !arena->gameState || !arena->gameState->resources ||
      !arena->gameState->packs || !arena->gameState->world)
    return std::string{"ResourceRef.cache: resource state unavailable"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.cachedValue)
    return std::monostate{};
  auto resolved = game.resources->resolve(it->second->link.owner);
  if (resolved && resolved.packNode && (resolved.packNode->flags & 0x2000000u))
    return std::string{"ResourceRef.cache: synchronous load of a pending "
                       "resource unsupported"};
  if (resolved && (resolved.type == 6 || resolved.type == 8) &&
      resolved.packNode && resolved.packNode->payloadSize == 0) {
    auto &node = *resolved.packNode;
    if (node.stamp7c < game.world->frameCounter() ||
        (node.field6c && *node.field6c < 1.0f))
      node.field6c = 1.0f;
    else if (!node.field6c)
      return std::string{"ResourceRef.cache: unknown script load value"};
    std::string error;
    if (!game.resources->loadEmptyResource(node, error))
      return "ResourceRef.cache: " + error;
    if (node.stamp74 != game.world->frameCounter() && !node.recentUseQueued)
      node.stateWord = (node.stateWord & ~uint32_t{0x60}) | 0x10;
    game.world->noteRecentUse(*resolved.owner, node.type, node.stamp74,
                              node.recentUseQueued);
    return std::monostate{};
  }
  if (resolved && resolved.type == 7 && resolved.packNode) {
    if (!game.renderer)
      return std::string{"ResourceRef.cache: texture renderer unavailable"};
    std::string error;
    if (!cacheResourceTexture(*game.resources, *game.renderer, resolved, 1.0f,
                              error))
      return "ResourceRef.cache: " + error;
    return std::monostate{};
  }
  if (resolved && resolved.type == 14 && resolved.packNode)
    return game.renderer
               ? cacheDrawMaterial(*game.resources, *game.renderer, resolved)
               : NativeResult{std::string{
                     "ResourceRef.cache: font renderer unavailable"}};
  if (!resolved || !resolved.packNode || resolved.type != 6)
    return "ResourceRef.cache: only mounted type-6 sounds are supported "
           "(resource " +
           std::to_string(resolved.id) + ", type " +
           std::to_string(resolved.type) + ")";
  auto &node = *resolved.packNode;
  if (!node.field6c && node.stamp7c >= game.world->frameCounter())
    return std::string{"ResourceRef.cache: unknown load value"};
  if (node.loaded() && !node.sound)
    return std::string{"ResourceRef.cache: loaded sound has no host buffer"};
  if (!node.sound) {
    PackResource resource;
    std::string error;
    if (!game.packs->lookup(node.value, resource, error) || !resource.packBytes)
      return "ResourceRef.cache: " + error;
    auto sample = loadSoundSample(*resource.packBytes, resource.entry,
                                  game.packs->root(), error);
    if (!sample)
      return "ResourceRef.cache: " + error;
    node.soundBufferBytes = sample->stockBufferBytes;
    runlog::event(runlog::EventKind::Resource, "sound 0x%08x cached",
                  static_cast<unsigned>(node.value));
    node.sound = std::move(sample);
    node.stateWord = (node.stateWord & ~uint32_t{0x50}) | 0x21;
    node.loadPriority = 1.0f;
    game.world->trackLoadState(node, node.type, true);
  }
  if (node.stamp7c < game.world->frameCounter() ||
      (node.field6c && *node.field6c < 1.0f))
    node.field6c = 1.0f;
  else if (!node.field6c)
    return std::string{"ResourceRef.cache: unknown load value"};
  return std::monostate{};
}

NativeResult resourceRefId(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty())
    return std::string{"invalid ResourceRef.id call"};
  const VmField *ptr = nullptr;
  for (const auto &field : receiver->fields)
    if (field.name == "ptr") {
      if (ptr)
        return std::string{"ResourceRef.id: ambiguous ptr field"};
      ptr = &field;
    }
  if (!ptr || ptr->descriptor != "I" ||
      !std::holds_alternative<int32_t>(ptr->value))
    return std::string{"ResourceRef.id: receiver has no ptr field"};
  if (std::get<int32_t>(ptr->value) == 0)
    return VmValue{int32_t{0}};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"ResourceRef.id: invalid native link"};
  uint32_t id = state->words[2];
  if (arena->gameState) {
    const int32_t handle =
        static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
    const auto binding = arena->gameState->bindings.find(handle);
    if (binding != arena->gameState->bindings.end() && binding->second)
      id = binding->second->link.cachedValue;
  }
  int32_t signedId;
  std::memcpy(&signedId, &id, sizeof id);
  return VmValue{signedId};
}

NativeResult resourceRefSetInt(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!arena || !receiver)
    return std::string{"invalid ResourceRef.set receiver"};
  if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid ResourceRef.set call"};

  // FUN_0042ab50 reads the receiver's `ptr` int field. Zero is a record that
  // newNative has not made: stock does nothing.
  const VmField *ptr = nullptr;
  for (const auto &field : receiver->fields)
    if (field.name == "ptr") {
      if (ptr)
        return std::string{"ResourceRef.set: ambiguous ptr field"};
      ptr = &field;
    }
  if (!ptr || ptr->descriptor != "I" ||
      !std::holds_alternative<int32_t>(ptr->value))
    return std::string{"ResourceRef.set: receiver has no ptr field"};
  if (std::get<int32_t>(ptr->value) == 0)
    return std::monostate{};

  VmResourceState *receiverState = nativeResourceState(receiver, arena);
  if (!receiverState)
    return std::string{
        "ResourceRef.set: receiver ptr is not a link handle of this arena"};
  if (!arena->gameState || !arena->gameState->resources)
    return std::string{"ResourceRef.set: no resource world state"};
  VmGameState &state = *arena->gameState;

  const int32_t handle =
      static_cast<int32_t>(receiverState - arena->resourceStates.data()) + 1;
  auto &slot = state.bindings[handle];
  if (!slot)
    slot = std::make_unique<ReceiverBinding>();
  ReceiverBinding &binding = *slot;

  uint32_t id;
  const int32_t raw = std::get<int32_t>(args[0]);
  std::memcpy(&id, &raw, sizeof id);
  const RefSetResult result =
      resourceRefSet(*state.resources, binding.link, id);
  if (result.status == RefSetStatus::StockCrash)
    return "ResourceRef.set: stock crash: " + result.detail;
  if (result.status == RefSetStatus::MountRefused)
    return "ResourceRef.set: pack mount refused: " + result.detail;
  // FUN_00545fc0 leaves the id in record +8 (or the old one, or 0). The
  // script-visible id word follows it in every case.
  receiverState->words[2] = binding.link.cachedValue;
  return std::monostate{};
}

NativeResult renderRefChangeResource(const std::vector<VmValue> &args,
                                     const VmObject *receiver, VmArena *arena,
                                     const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 2 ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]))
    return std::string{"invalid RenderRef.changeResource call"};
  const auto *oldObject = std::get<const VmObject *>(args[0]);
  const auto *newObject = std::get<const VmObject *>(args[1]);
  if (!oldObject || !newObject)
    return std::string{"RenderRef.changeResource: null resource unsupported"};
  const auto *self = nativeResourceState(receiver, arena);
  const auto *oldState = nativeResourceState(oldObject, arena);
  const auto *newState = nativeResourceState(newObject, arena);
  if (!self || !oldState || !newState)
    return std::string{"RenderRef.changeResource: invalid resource handle"};
  if (!arena->gameState || !arena->gameState->resources)
    return std::string{"RenderRef.changeResource: no resource world"};
  auto &game = *arena->gameState;
  const auto linkFor = [&](const VmResourceState *state) -> OwnerLink * {
    const auto handle =
        static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
    const auto it = game.bindings.find(handle);
    return it == game.bindings.end() || !it->second ? nullptr
                                                    : &it->second->link;
  };
  const OwnerLink *selfLink = linkFor(self);
  if (!selfLink || !selfLink->owner)
    return std::monostate{};
  const auto node = game.resources->resolve(selfLink->owner);
  if (!node || node.type != 14)
    return std::string{"RenderRef.changeResource: unsupported resource type"};
  const auto material = game.resources->sceneMaterial(node);
  if (!material)
    return std::string{"RenderRef.changeResource: type-14 material not loaded"};
  const OwnerLink *oldLink = linkFor(oldState);
  const OwnerLink *newLink = linkFor(newState);
  const uint32_t oldId = oldLink ? oldLink->cachedValue : 0;
  LinkOwner *newOwner = newLink ? newLink->owner : nullptr;
  material->replace(oldId, newOwner,
                    [&] { game.resources->refreshSceneMaterial(node); });
  return std::monostate{};
}

NativeResult resourceRefScaleMesh(const std::vector<VmValue> &args,
                                  const VmObject *receiver, VmArena *arena,
                                  const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 3 ||
      !std::holds_alternative<float>(args[0]) ||
      !std::holds_alternative<float>(args[1]) ||
      !std::holds_alternative<float>(args[2]))
    return std::string{"invalid ResourceRef.scaleMesh call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"ResourceRef.scaleMesh: invalid resource handle"};
  if (!arena->gameState || !arena->gameState->resources)
    return std::string{"ResourceRef.scaleMesh: no resource world"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return std::monostate{};
  const auto node = game.resources->resolve(it->second->link.owner);
  if (!node || node.type != 5)
    return std::string{"ResourceRef.scaleMesh: unsupported resource type"};
  if (game.resources->hasActiveSceneMaterialForMesh(node.id))
    return std::string{
        "ResourceRef.scaleMesh: live renderer update unsupported"};
  auto mesh = game.resources->sceneMesh(node);
  if (!mesh && node.packNode) {
    ScxModel loaded;
    std::string error;
    if (!sceneLoadMesh(*game.resources, node.id, game.resources->packs().root(),
                       loaded, error))
      return "ResourceRef.scaleMesh: " + error;
    mesh = std::make_shared<ScxModel>(std::move(loaded));
    if (!game.resources->bindSceneMesh(node, mesh))
      return std::string{"ResourceRef.scaleMesh: binding failed"};
  }
  if (!mesh)
    return std::string{"ResourceRef.scaleMesh: runtime mesh unavailable"};
  if (!sceneScaleMesh(*mesh, std::get<float>(args[0]), std::get<float>(args[1]),
                      std::get<float>(args[2])))
    return std::string{"ResourceRef.scaleMesh: invalid mesh geometry"};
  return std::monostate{};
}

NativeResult renderRefGetTypeId(const std::vector<VmValue> &args,
                                const VmObject *receiver, VmArena *arena,
                                const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"RenderRef.getTypeID: invalid call or host"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state) return std::string{"RenderRef.getTypeID: invalid resource handle"};
  auto &game = *arena->gameState;
  const auto handle = static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second || !binding->second->link.owner)
    return VmValue{int32_t{0}};
  const auto node = game.resources->resolve(binding->second->link.owner);
  if (!node || node.type != 3) return VmValue{int32_t{0}};
  if (!node.worldNode || !node.worldNode->loaded)
    return std::string{"RenderRef.getTypeID: render instance loading unsupported"};
  const auto instance = game.resources->sceneRenderInstance(node);
  if (!instance) return std::string{"RenderRef.getTypeID: render instance unavailable"};
  int32_t id;
  const auto type = instance->typeId();
  std::memcpy(&id, &type, sizeof id);
  return VmValue{id};
}

bool renderRefCreateTypeTemplate(const ResolvedNode &type) {
  if (!type)
    return false;
  switch (type.type) {
  case 6:
  case 12:
  case 13:
  case 14:
  case 20:
    return true;
  default:
    return false;
  }
}

NativeResult renderRefCreate(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *arena,
                             const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 3 ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]) ||
      !std::holds_alternative<const VmObject *>(args[2]))
    return std::string{"invalid RenderRef.create call"};
  const auto *parentObject = std::get<const VmObject *>(args[0]);
  const auto *typeObject = std::get<const VmObject *>(args[1]);
  if (!parentObject || !typeObject)
    return std::string{"RenderRef.create: null resource handle"};
  auto *self = nativeResourceState(receiver, arena);
  const auto *parentState = nativeResourceState(parentObject, arena);
  const auto *typeState = nativeResourceState(typeObject, arena);
  if (!self || !parentState || !typeState)
    return std::string{"RenderRef.create: invalid resource handle"};
  if (!arena->gameState || !arena->gameState->resources ||
      !arena->gameState->world || !arena->gameState->packs)
    return std::string{"RenderRef.create: no resource world"};
  auto &game = *arena->gameState;
  const auto linkFor = [&](const VmResourceState *state) -> OwnerLink * {
    const auto handle =
        static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
    const auto it = game.bindings.find(handle);
    return it == game.bindings.end() || !it->second ? nullptr
                                                    : &it->second->link;
  };
  const auto *parentLink = linkFor(parentState);
  if (!parentLink || !parentLink->owner)
    return std::monostate{};
  const auto *typeLink = linkFor(typeState);
  if (typeLink && typeLink->cachedValue && !typeLink->owner)
    return std::string{"RenderRef.create: unresolved type unsupported"};
  const auto parent = game.resources->resolve(parentLink->owner);
  const auto type = typeLink && typeLink->owner
                        ? game.resources->resolve(typeLink->owner)
                        : ResolvedNode{};
  if (!parent || parent.type != 1 ||
      (typeLink && typeLink->owner && (!type || !renderRefCreateTypeTemplate(type))))
    return std::string{"RenderRef.create: unsupported parent or type"};
  if (!game.renderer)
    return std::string{
        "RenderRef.create: renderer object creation unsupported"};
  // FUN_00539880 never checks the parent's load state: it copies the parent's
  // +0x68/+0x6c/+0x70 as they stand (constructor defaults while unloaded).
  if (parent.type != 1 || (!parent.worldNode && !parent.packNode))
    return std::string{"RenderRef.create: unsupported parent"};
  const auto *nameObject = std::get<const VmObject *>(args[2]);
  std::string name = "_renderinst";
  if (nameObject) {
    const auto found = std::find_if(
        arena->strings.begin(), arena->strings.end(),
        [&](const auto &entry) { return entry.second == nameObject; });
    if (found == arena->strings.end())
      return std::string{"RenderRef.create: name string unavailable"};
    name = found->first.substr(0, 30);
  }
  // Scene.class selects the config in script; Track.enter supplies GameLogic.getTime().
  // Free ride initializes it with (Math.random()*14+4)*3600 in MainMenuDialog.
  if (name.rfind("egbolt-", 0) == 0) {
    static bool timeConfigLogged = false;
    if (!timeConfigLogged) {
      timeConfigLogged = true;
      runlog::event(runlog::EventKind::Note,
                    "scene time config: seconds=%d skydome=%s type=0x%x",
                    game.gameTimeOfDay, name.c_str(),
                    type && type.worldNode ? type.worldNode->instanceId : 0u);
    }
  }
  {
    static unsigned createCount = 0;
    if (createCount < 12) {
      ++createCount;
      runlog::event(runlog::EventKind::Note,
                    "RenderRef.create #%u: parent=0x%x type=0x%x name=%s "
                    "childType=3",
                    createCount, parent.id,
                    type && type.worldNode ? type.worldNode->instanceId : 0u,
                    name.c_str());
    }
  }
  WorldNode *node =
      parent.worldNode
          ? game.world->addTypedChild(*parent.worldNode, 3, name, 0x200)
          : game.world->addTypedChildUnderExternal(
                parent.packNode->handle, *parent.packNode, parent.type,
                parent.packNode->sectorLink.owner, 3, name, 0x200);
  if (!node)
    return std::string{"RenderRef.create: render instance allocation failed"};
  node->bounds = parent.worldNode
                     ? parent.worldNode->bounds
                     : parent.packNode->bounds.value_or(ResourceBounds{});
  node->field6c =
      parent.worldNode ? parent.worldNode->field6c : parent.packNode->field6c;
  node->field70 =
      parent.worldNode ? parent.worldNode->field70 : parent.packNode->field70;
  game.world->markLoaded(*node);
  node->loadPriority = 1.0f;
  auto instance =
      std::make_shared<SceneRenderInstance>(type ? type.owner : nullptr);
  if (!game.resources->bindSceneRenderInstance(game.resources->resolve(node),
                                               instance)) {
    game.world->destroy(*node);
    return std::string{"RenderRef.create: render instance binding failed"};
  }
  const auto handle =
      static_cast<int32_t>(self - arena->resourceStates.data()) + 1;
  auto &binding = game.bindings[handle];
  if (!binding)
    binding = std::make_unique<ReceiverBinding>();
  setLinkOwner(binding->link, node);
  self->words[2] = binding->link.cachedValue;
  return std::monostate{};
}

namespace {

uint32_t vmColorArg(const VmValue &value, uint32_t fallback) {
  const auto *raw = std::get_if<int32_t>(&value);
  if (!raw)
    return fallback;
  uint32_t bits;
  std::memcpy(&bits, raw, sizeof bits);
  return bits;
}

} // namespace

// 00480310 -> 0048c8c0: stores the color word at render instance +0xcc.
NativeResult renderRefSetColor(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 1 ||
      !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid RenderRef.setColor call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"RenderRef.setColor: invalid resource handle"};
  if (!arena->gameState || !arena->gameState->resources)
    return std::string{"RenderRef.setColor: no resource world"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return std::monostate{};
  const auto instance = game.resources->sceneRenderInstance(
      game.resources->resolve(it->second->link.owner));
  if (!instance)
    return std::string{"RenderRef.setColor: render instance unavailable"};
  instance->setInstanceColor(vmColorArg(args[0], 0xffffffffu));
  return std::monostate{};
}

NativeResult renderRefSetLight(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 3 ||
      !std::holds_alternative<int32_t>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]) ||
      !std::holds_alternative<int32_t>(args[2]))
    return std::string{"invalid RenderRef.setLight call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"RenderRef.setLight: invalid resource handle"};
  if (!arena->gameState || !arena->gameState->resources)
    return std::string{"RenderRef.setLight: no resource world"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return std::monostate{};
  const auto node = game.resources->resolve(it->second->link.owner);
  if (!node || node.type != 13)
    return std::string{"RenderRef.setLight: light node unavailable"};
  if (!node.worldNode || !node.worldNode->loaded)
    return std::string{"RenderRef.setLight: light loading unsupported"};
  const uint32_t first = vmColorArg(args[0], 0xffffff);
  const uint32_t second = vmColorArg(args[1], 0x404040);
  const uint32_t third = vmColorArg(args[2], 0xffffff);
  // One-shot time-of-day signature: with the sky config these three colours
  // (diffuse/ambient/specular after FUN_0048c9d0's channel order) identify
  // the active scene look in the run log.
  static bool setLightLogged = false;
  if (!setLightLogged) {
    setLightLogged = true;
    runlog::event(runlog::EventKind::Note,
                  "RenderRef.setLight: diffuse=0x%06x ambient=0x%06x "
                  "specular=0x%06x",
                  first & 0xffffffu, second & 0xffffffu, third & 0xffffffu);
  }
  SceneRenderLight light = game.resources->sceneLight(node)
                               ? *game.resources->sceneLight(node)
                               : SceneRenderLight::stockDefaults();
  light.applySetLight(first, second, third);
  if (!game.resources->bindSceneLight(node, light))
    return std::string{"RenderRef.setLight: light binding failed"};
  game.resources->markConfiguredLight(node.id);
  return std::monostate{};
}

NativeResult renderRefSetFlare(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 6 ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]) ||
      !std::holds_alternative<float>(args[2]) ||
      !std::holds_alternative<float>(args[3]) ||
      !std::holds_alternative<int32_t>(args[4]) ||
      !std::holds_alternative<int32_t>(args[5]))
    return std::string{"invalid RenderRef.setFlare call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"RenderRef.setFlare: invalid resource handle"};
  if (!arena->gameState || !arena->gameState->resources)
    return std::string{"RenderRef.setFlare: no resource world"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return std::monostate{};
  const auto node = game.resources->resolve(it->second->link.owner);
  if (!node || node.type != 13)
    return std::string{"RenderRef.setFlare: light node unavailable"};
  if (!node.worldNode || !node.worldNode->loaded)
    return std::string{"RenderRef.setFlare: light loading unsupported"};
  SceneRenderLight light = game.resources->sceneLight(node)
                               ? *game.resources->sceneLight(node)
                               : SceneRenderLight::stockDefaults();
  SceneRenderLight::Flare flare;
  const auto *textureObject = std::get<const VmObject *>(args[0]);
  if (textureObject) {
    const auto *textureState = nativeResourceState(textureObject, arena);
    if (!textureState)
      return std::string{"RenderRef.setFlare: invalid flare resource"};
    flare.textureId = textureState->words[2];
  }
  flare.wordE0 = std::get<int32_t>(args[1]);
  flare.atD4 = std::get<float>(args[2]);
  flare.atD8 = std::get<float>(args[3]);
  flare.at100 = std::get<int32_t>(args[4]);
  flare.atE4 = std::get<int32_t>(args[5]);
  light.flare = flare;
  if (!game.resources->bindSceneLight(node, light))
    return std::string{"RenderRef.setFlare: light binding failed"};
  return std::monostate{};
}

namespace {
NativeResult renderRefSetMatrixCommon(const VmValue &positionArg,
                                      const VmValue &orientationArg,
                                      const VmObject *receiver, VmArena *arena);
}

NativeResult renderRefSetMatrix(const std::vector<VmValue> &args,
                                const VmObject *receiver, VmArena *arena,
                                const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 2 ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]))
    return std::string{"invalid RenderRef.setMatrix call"};
  return renderRefSetMatrixCommon(args[0], args[1], receiver, arena);
}

// 004810b0 uses the bone id and parent resource to establish a bone link.
// The host only models a single unparented bone.
NativeResult renderRefSetMatrixBone(const std::vector<VmValue> &args,
                                    const VmObject *receiver, VmArena *arena,
                                    const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 4 ||
      !std::holds_alternative<int32_t>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]) ||
      !std::holds_alternative<const VmObject *>(args[2]) ||
      !std::holds_alternative<const VmObject *>(args[3]))
    return std::string{"invalid RenderRef.setMatrix call"};
  return std::string{"RenderRef.setMatrix: bone parent linkage unsupported"};
}

namespace {
NativeResult renderRefSetMatrixCommon(const VmValue &positionArg,
                                      const VmValue &orientationArg,
                                      const VmObject *receiver, VmArena *arena) {
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"RenderRef.setMatrix: invalid resource handle"};
  if (!arena->gameState || !arena->gameState->resources)
    return std::string{"RenderRef.setMatrix: no resource world"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return std::monostate{};
  const auto node = game.resources->resolve(it->second->link.owner);
  const auto instance = game.resources->sceneRenderInstance(node);
  if (!instance)
    return std::string{"RenderRef.setMatrix: render instance unavailable"};
  const auto readFields = [](const VmObject *object,
                             const char *const(&names)[3], float (&out)[3]) {
    if (!object)
      return true;
    for (size_t i = 0; i < 3; ++i) {
      const VmField *match = nullptr;
      for (const auto &field : object->fields)
        if (field.name == names[i]) {
          if (match)
            return false;
          match = &field;
        }
      if (!match || match->descriptor != "F" ||
          !std::holds_alternative<float>(match->value))
        return false;
      out[i] = std::get<float>(match->value);
    }
    return true;
  };
  const char *const xyz[] = {"x", "y", "z"};
  const char *const ypr[] = {"y", "p", "r"};
  float position[3]{}, orientation[3]{};
  if (!readFields(std::get<const VmObject *>(positionArg), xyz, position) ||
      !readFields(std::get<const VmObject *>(orientationArg), ypr, orientation))
    return std::string{"RenderRef.setMatrix: invalid vector or orientation"};
  // FUN_0048bf50 SetBoneMatrixParentLink: Ypr_toMatrix + position*10 into the
  // bone-local matrix, then a bone relink. Null position/rotation read as
  // zeros (identity) exactly like stock.
  instance->setBoneMatrix(position, orientation);
  return std::monostate{};
}
} // namespace

NativeResult
wrapResourceReference(LinkOwner *target, VmArena *arena,
                      const std::vector<const TufaClass *> *classes) {
  if (!arena || !classes || !arena->gameState || !arena->gameState->resources)
    return std::string{"ResourceRef child: missing context"};
  if (!target)
    return VmValue{static_cast<const VmObject *>(nullptr)};
  auto &game = *arena->gameState;
  const auto node = game.resources->resolve(target);
  if (!node || (!node.id && node.type != 0))
    return std::string{"ResourceRef child: unsupported child"};
  const char *name =
      node.type == 1 || node.type == 8    ? "java.util.resource.GameRef"
      : node.type == 3 || node.type == 14 ? "java.util.resource.RenderRef"
                                          : "java.util.resource.ResourceRef";
  const TufaClass *cls = nullptr;
  for (const auto *c : *classes)
    if (c && c->name == name)
      cls = c;
  if (!cls)
    return std::string{"ResourceRef child: result class unavailable: "} + name;
  if (arena->objects.size() >= arena->limit ||
      arena->resourceStates.size() >= static_cast<size_t>(INT32_MAX))
    return std::string{"ResourceRef child: arena full"};
  const TufaClass *current = cls;
  size_t count = 0;
  bool ptrFound = false;
  while (current) {
    if (++count > classes->size())
      return std::string{"ResourceRef child: cyclic class hierarchy"};
    for (const auto &list : current->fields)
      for (const auto &field : list) {
        const auto &w = field.words;
        if (w[0] & 8)
          continue;
        if (w[1] >= current->constants.size() ||
            w[2] >= current->constants.size() ||
            current->constants[w[1]].text != "ptr" ||
            current->constants[w[2]].text != "I" || w[3] != UINT32_MAX ||
            ptrFound)
          return std::string{"ResourceRef child: unsupported wrapper fields"};
        ptrFound = true;
      }
    const auto parent = current->classHeader[3];
    if (parent == UINT32_MAX)
      break;
    if (parent >= current->constants.size() ||
        current->constants[parent].kind != 4 ||
        current->constants[parent].a >= current->constants.size())
      return std::string{"ResourceRef child: invalid class hierarchy"};
    const auto &parentName =
        current->constants[current->constants[parent].a].text;
    current = nullptr;
    for (const auto *c : *classes)
      if (c && c->name == parentName)
        current = c;
    if (!current)
      return std::string{"ResourceRef child: parent class unavailable"};
  }
  if (!ptrFound)
    return std::string{"ResourceRef child: ptr field unavailable"};
  auto object = std::make_unique<VmObject>();
  object->runtimeClass = cls;
  const auto newHandle = static_cast<int32_t>(arena->resourceStates.size() + 1);
  object->fields.push_back({"ptr", newHandle, "I"});
  auto *result = object.get();
  arena->objects.push_back(std::move(object));
  arena->resourceStates.push_back({result, {}});
  arena->resourceStates.back().words[2] = node.id;
  auto binding = std::make_unique<ReceiverBinding>();
  setLinkOwner(binding->link, target);
  game.bindings.emplace(newHandle, std::move(binding));
  return VmValue{static_cast<const VmObject *>(result)};
}

namespace {
NativeResult childReference(bool next, const std::vector<VmValue> &args,
                            const VmObject *receiver, VmArena *arena,
                            const std::vector<const TufaClass *> *classes) {
  if (!arena || !receiver || !classes || !args.empty() || !arena->gameState ||
      !arena->gameState->resources || !arena->gameState->world)
    return std::string{"ResourceRef child: missing context"};
  auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"ResourceRef child: invalid link"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return VmValue{static_cast<const VmObject *>(nullptr)};
  const auto source = game.resources->resolve(it->second->link.owner);
  if (!source)
    return std::string{"ResourceRef child: unresolved owner"};
  const auto sourceHandle = source.packNode    ? source.packNode->handle
                            : source.worldNode ? source.worldNode->handle
                                               : ResourceTreeHandle{};
  if (!sourceHandle.id)
    return std::string{"ResourceRef child: unknown tree node"};
  LinkOwner *target = nullptr;
  if (!next) {
    const auto children = game.world->childOwners(sourceHandle);
    if (!children)
      return std::string{"ResourceRef child: unavailable children"};
    if (!children->empty())
      target = children->front();
  } else {
    auto *parent = game.world->parentOwner(sourceHandle);
    if (!parent)
      return VmValue{static_cast<const VmObject *>(nullptr)};
    const auto parentNode = game.resources->resolve(parent);
    const auto parentHandle = parentNode.packNode ? parentNode.packNode->handle
                              : parentNode.worldNode
                                  ? parentNode.worldNode->handle
                                  : ResourceTreeHandle{};
    if (!parentHandle.id)
      return std::string{"ResourceRef child: unknown parent"};
    const auto children = game.world->childOwners(parentHandle);
    if (!children)
      return std::string{"ResourceRef child: unavailable siblings"};
    auto current = std::find(children->begin(), children->end(), source.owner);
    if (current == children->end())
      return std::string{"ResourceRef child: inconsistent sibling list"};
    if (++current != children->end())
      target = *current;
  }
  return wrapResourceReference(target, arena, classes);
}
} // namespace
NativeResult
resourceRefFirstChild(const std::vector<VmValue> &args,
                      const VmObject *receiver, VmArena *arena,
                      const std::vector<const TufaClass *> *classes) {
  return childReference(false, args, receiver, arena, classes);
}
NativeResult
resourceRefNextChild(const std::vector<VmValue> &args, const VmObject *receiver,
                     VmArena *arena,
                     const std::vector<const TufaClass *> *classes) {
  return childReference(true, args, receiver, arena, classes);
}

NativeResult resourceRefDeleteNative(const std::vector<VmValue> &args,
                                     const VmObject *receiver, VmArena *arena,
                                     const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty())
    return std::string{"invalid ResourceRef.deleteNative call"};
  auto *mutableReceiver = const_cast<VmObject *>(receiver);
  VmField *ptr = nullptr;
  for (auto &field : mutableReceiver->fields)
    if (field.name == "ptr") {
      if (ptr)
        return std::string{"ResourceRef.deleteNative: ambiguous ptr field"};
      ptr = &field;
    }
  if (!ptr || ptr->descriptor != "I" ||
      !std::holds_alternative<int32_t>(ptr->value))
    return std::string{"ResourceRef.deleteNative: receiver has no ptr field"};
  const int32_t handle = std::get<int32_t>(ptr->value);
  if (handle == 0)
    return std::monostate{};
  if (auto *state = nativeResourceState(receiver, arena)) {
    state->words[2] = 0;
    if (arena->gameState) {
      auto it = arena->gameState->bindings.find(handle);
      if (it != arena->gameState->bindings.end() && it->second)
        setLinkOwner(it->second->link, nullptr);
    }
  }
  ptr->value = int32_t{0};
  return std::monostate{};
}

NativeResult resourceRefSetRef(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 1 ||
      !std::holds_alternative<const VmObject *>(args[0]))
    return std::string{"invalid ResourceRef.set(ResourceRef) call"};
  const VmField *ptr = nullptr;
  for (const auto &field : receiver->fields)
    if (field.name == "ptr") {
      if (ptr)
        return std::string{"ResourceRef.set: ambiguous ptr field"};
      ptr = &field;
    }
  if (!ptr || ptr->descriptor != "I" ||
      !std::holds_alternative<int32_t>(ptr->value))
    return std::string{"ResourceRef.set: receiver has no ptr field"};
  if (std::get<int32_t>(ptr->value) == 0)
    return std::monostate{};
  VmResourceState *receiverState = nativeResourceState(receiver, arena);
  if (!receiverState || !arena->gameState || !arena->gameState->resources)
    return std::string{"ResourceRef.set: invalid receiver state"};
  auto &game = *arena->gameState;
  const int32_t handle =
      static_cast<int32_t>(receiverState - arena->resourceStates.data()) + 1;
  auto &slot = game.bindings[handle];
  if (!slot)
    slot = std::make_unique<ReceiverBinding>();
  const auto *other = std::get<const VmObject *>(args[0]);
  if (!other) {
    setLinkOwner(slot->link, nullptr);
    receiverState->words[2] = 0;
    return std::monostate{};
  }
  const VmField *otherPtr = nullptr;
  for (const auto &field : other->fields)
    if (field.name == "ptr" && field.descriptor == "I")
      otherPtr = &field;
  if (!otherPtr || !std::holds_alternative<int32_t>(otherPtr->value) ||
      std::get<int32_t>(otherPtr->value) == 0) {
    setLinkOwner(slot->link, nullptr);
    receiverState->words[2] = 0;
    return std::monostate{};
  }
  const auto *otherState = nativeResourceState(other, arena);
  if (!otherState)
    return std::string{"ResourceRef.set: invalid source state"};
  const int32_t otherHandle =
      static_cast<int32_t>(otherState - arena->resourceStates.data()) + 1;
  auto otherIt = game.bindings.find(otherHandle);
  if (otherIt != game.bindings.end() && otherIt->second &&
      otherIt->second->link.owner) {
    setLinkOwner(slot->link, otherIt->second->link.owner);
    receiverState->words[2] = slot->link.cachedValue;
    return std::monostate{};
  }
  const uint32_t otherId = (otherIt != game.bindings.end() && otherIt->second)
                               ? otherIt->second->link.cachedValue
                               : otherState->words[2];
  if (otherId == 0) {
    setLinkOwner(slot->link, nullptr);
    receiverState->words[2] = 0;
    return std::monostate{};
  }
  const RefSetResult result =
      resourceRefSet(*game.resources, slot->link, otherId);
  if (result.status == RefSetStatus::StockCrash)
    return "ResourceRef.set: stock crash: " + result.detail;
  if (result.status == RefSetStatus::MountRefused)
    return "ResourceRef.set: pack mount refused: " + result.detail;
  receiverState->words[2] = slot->link.cachedValue;
  return std::monostate{};
}

NativeResult resourceRefLoad(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *arena,
                             const std::vector<const TufaClass *> *classes) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources || !arena->gameState->world)
    return std::string{"ResourceRef.load: invalid call or host"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::monostate{};
  auto &game = *arena->gameState;
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return std::monostate{};
  auto resolved = game.resources->resolve(it->second->link.owner);
  if (!resolved)
    return std::monostate{};
  if (resolved.worldNode) {
    game.world->markLoaded(*resolved.worldNode);
    resolved.worldNode->loadPriority = 1.0f;
    resolved.worldNode->field6c = 1.0f;
    game.world->noteRecentUse(*resolved.owner, resolved.type,
                              resolved.worldNode->stamp74,
                              resolved.worldNode->recentUseQueued);
    return std::monostate{};
  }
  if (resolved.packNode) {
    auto &packed = *resolved.packNode;
    packed.field6c = 1.0f;
    packed.loadPriority = 1.0f;
    if (resolved.type == 1 && !packed.loaded()) {
      std::string error;
      if (!game.resources->loadEmptyGameParent(packed, error))
        return "ResourceRef.load: " + error;
    }
    if (resolved.type == 6 || (resolved.type == 7 && game.renderer) ||
        (resolved.type == 14 && game.renderer)) {
      auto cached = resourceRefCache(args, receiver, arena, classes);
      if (std::holds_alternative<std::monostate>(cached))
        return cached;
    }
    if (!packed.loaded() && packed.payloadSize == 0) {
      std::string error;
      game.resources->loadEmptyResource(packed, error);
    }
    if (!packed.loaded()) {
      packed.stateWord = (packed.stateWord & ~uint32_t{0x50}) | 0x21;
      game.world->trackLoadState(*resolved.owner, resolved.type, true);
    }
    if (packed.stamp74 != game.world->frameCounter() && !packed.recentUseQueued)
      packed.stateWord = (packed.stateWord & ~uint32_t{0x60}) | 0x10;
    game.world->noteRecentUse(*resolved.owner, resolved.type, packed.stamp74,
                              packed.recentUseQueued);
  }
  return std::monostate{};
}

NativeResult resourceRefUnload(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty())
    return std::string{"ResourceRef.unload: invalid call"};
  return std::monostate{};
}

NativeResult resourceRefDestroy(const std::vector<VmValue> &args,
                                const VmObject *receiver, VmArena *arena,
                                const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->world)
    return std::string{"ResourceRef.destroy: invalid call or host"};
  auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::monostate{};
  auto &game = *arena->gameState;
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return std::monostate{};
  // Stock 0047d1e0 (ResourceRef.destroy) resolves the resource and, when it
  // is live, posts a render-refresh command record to the resource's queue
  // (0048a8d0 -> 00427620, a 0x1c record vtable 005f09b4); it does NOT
  // destroy the instance. Destroying the node here removed freshly installed
  // car parts when scripts dropped their temporary refs. The port's renderer
  // refreshes every frame, so the refresh record is a no-op; the binding is
  // detached like the stock ref release.
  static bool destroyTraceLogged = false;
  if (!destroyTraceLogged) {
    destroyTraceLogged = true;
    runlog::event(runlog::EventKind::Note,
                  "ResourceRef.destroy: stock posts a render-refresh record "
                  "(0047d1e0/00427620), no destruction; port detaches the "
                  "binding only");
  }
  setLinkOwner(it->second->link, nullptr);
  state->words[2] = 0;
  return std::monostate{};
}

NativeResult resourceRefType(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *arena,
                             const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"ResourceRef.type: invalid call or host"};
  const VmField *ptr = nullptr;
  for (const auto &field : receiver->fields)
    if (field.name == "ptr" && field.descriptor == "I")
      ptr = &field;
  if (!ptr || !std::holds_alternative<int32_t>(ptr->value) ||
      std::get<int32_t>(ptr->value) == 0)
    return VmValue{int32_t{0}};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return VmValue{int32_t{0}};
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = arena->gameState->bindings.find(handle);
  if (it == arena->gameState->bindings.end() || !it->second ||
      !it->second->link.owner)
    return VmValue{int32_t{0}};
  const auto resolved =
      arena->gameState->resources->resolve(it->second->link.owner);
  if (!resolved)
    return VmValue{int32_t{0}};
  return VmValue{static_cast<int32_t>(resolved.type)};
}

NativeResult resourceRefGetParentId(const std::vector<VmValue> &args,
                                    const VmObject *receiver, VmArena *arena,
                                    const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources || !arena->gameState->world)
    return std::string{"ResourceRef.getParentID: invalid call or host"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return VmValue{int32_t{0}};
  auto &game = *arena->gameState;
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return VmValue{int32_t{0}};
  const auto source = game.resources->resolve(it->second->link.owner);
  const auto sourceHandle = source.packNode    ? source.packNode->handle
                            : source.worldNode ? source.worldNode->handle
                                               : ResourceTreeHandle{};
  if (!sourceHandle.id)
    return VmValue{int32_t{0}};
  auto *parent = game.world->parentOwner(sourceHandle);
  if (!parent)
    return VmValue{int32_t{0}};
  const auto parentNode = game.resources->resolve(parent);
  int32_t signedId = 0;
  std::memcpy(&signedId, &parentNode.id, sizeof signedId);
  return VmValue{signedId};
}

NativeResult
resourceRefGetParent(const std::vector<VmValue> &args, const VmObject *receiver,
                     VmArena *arena,
                     const std::vector<const TufaClass *> *classes) {
  if (!arena || !receiver || !classes || !args.empty() || !arena->gameState ||
      !arena->gameState->resources || !arena->gameState->world)
    return std::string{"ResourceRef.getParent: invalid call or host"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return VmValue{static_cast<const VmObject *>(nullptr)};
  auto &game = *arena->gameState;
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return VmValue{static_cast<const VmObject *>(nullptr)};
  const auto source = game.resources->resolve(it->second->link.owner);
  const auto sourceHandle = source.packNode    ? source.packNode->handle
                            : source.worldNode ? source.worldNode->handle
                                               : ResourceTreeHandle{};
  if (!sourceHandle.id)
    return VmValue{static_cast<const VmObject *>(nullptr)};
  auto *parent = game.world->parentOwner(sourceHandle);
  return wrapResourceReference(parent, arena, classes);
}

NativeResult
resourceRefGetWTRoot(const std::vector<VmValue> &args, const VmObject *,
                     VmArena *arena,
                     const std::vector<const TufaClass *> *classes) {
  if (!arena || !classes || !args.empty() || !arena->gameState ||
      !arena->gameState->world)
    return std::string{"ResourceRef.getWTRoot: invalid call or host"};
  return wrapResourceReference(arena->gameState->world->defaultParent(), arena,
                               classes);
}
