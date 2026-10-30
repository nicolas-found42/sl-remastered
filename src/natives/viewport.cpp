#include "natives/viewport.hpp"
#include "natives/game_type.hpp"
#include "camera_frame.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"

#include <cmath>

namespace {
struct Resolved {
  VmResourceState *resource = nullptr;
  VmGameState *game = nullptr;
  ReceiverBinding *binding = nullptr;
  WorldNode *node = nullptr;
};

Resolved resolve(const VmObject *receiver, VmArena *arena) {
  Resolved result;
  if (!arena || !arena->gameState || !arena->gameState->world)
    return result;
  result.game = arena->gameState;
  result.resource = nativeResourceState(receiver, arena);
  if (!result.resource)
    return result;
  const auto handle =
      static_cast<int32_t>(result.resource - arena->resourceStates.data()) + 1;
  const auto it = result.game->bindings.find(handle);
  if (it == result.game->bindings.end())
    return result;
  result.binding = it->second.get();
  result.node = result.game->world->nodeFor(result.binding->link.owner);
  return result;
}

bool rectangleArgs(const std::vector<VmValue> &args, size_t offset) {
  return args.size() == offset + 4 &&
         std::holds_alternative<float>(args[offset]) &&
         std::holds_alternative<float>(args[offset + 1]) &&
         std::holds_alternative<float>(args[offset + 2]) &&
         std::holds_alternative<float>(args[offset + 3]);
}

ViewportInstance *viewport(Resolved &state) {
  if (!state.node || state.node->type != 0x12 || !state.node->viewport)
    return nullptr;
  return &*state.node->viewport;
}

NativeResult getCoordinate(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           float ViewportInstance::*member) {
  if (!args.empty())
    return std::string{"invalid Viewport getter call"};
  auto state = resolve(receiver, arena);
  auto *instance = viewport(state);
  if (!instance)
    return std::string{"Viewport getter: missing viewport; stock receiver-bits "
                       "fallback is unmodeled"};
  return VmValue{instance->*member};
}
} // namespace

NativeResult cameraCreate(const std::vector<VmValue> &args,
                          const VmObject *receiver, VmArena *arena,
                          const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 10 ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]) ||
      !std::holds_alternative<int32_t>(args[2]) ||
      !std::holds_alternative<int32_t>(args[8]) ||
      !std::holds_alternative<int32_t>(args[9]))
    return std::string{"invalid Camera.create call"};
  for (size_t i = 3; i <= 7; ++i)
    if (!std::holds_alternative<float>(args[i]) || !std::isfinite(std::get<float>(args[i])))
      return std::string{"Camera.create: invalid camera parameter"};
  if (!arena->gameState || !arena->gameState->world ||
      !arena->gameState->resources || !arena->gameState->packs ||
      !arena->gameState->renderer)
    return std::string{"Camera.create: live renderer and resource world unavailable"};
  auto &game = *arena->gameState;
  auto *self = nativeResourceState(receiver, arena);
  auto *parentState = nativeResourceState(std::get<const VmObject *>(args[0]), arena);
  auto *viewportState = nativeResourceState(std::get<const VmObject *>(args[1]), arena);
  if (!self || !parentState || !viewportState)
    return std::string{"Camera.create: missing resource handle"};
  const auto bindingFor = [&](VmResourceState *state) -> ReceiverBinding * {
    const auto handle = static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
    const auto it = game.bindings.find(handle);
    return it == game.bindings.end() ? nullptr : it->second.get();
  };
  auto *parentBinding = bindingFor(parentState);
  auto *viewportBinding = bindingFor(viewportState);
  if (!parentBinding || !parentBinding->link.owner || !viewportBinding || !viewportBinding->link.owner)
    return std::string{"Camera.create: missing parent or viewport"};
  auto parent = game.resources->resolve(parentBinding->link.owner);
  auto *viewportNode = game.world->nodeFor(viewportBinding->link.owner);
  if (!parent || (!parent.worldNode && !parent.packNode) || !viewportNode ||
      viewportNode->type != 18 || !viewportNode->viewport)
    return std::string{"Camera.create: unsupported parent or viewport"};
  auto &viewport = *viewportNode->viewport;
  int width = 0, height = 0;
  std::string error;
  if (!game.renderer->outputSize(width, height, error))
    return "Camera.create: " + error;
  const float pixelWidth = viewport.width * static_cast<float>(width);
  const float pixelHeight = viewport.height * static_cast<float>(height);
  if (!(pixelWidth > 0) || !(pixelHeight > 0) || !std::isfinite(pixelWidth) || !std::isfinite(pixelHeight))
    return std::string{"Camera.create: invalid viewport dimensions"};
  if (std::get<int32_t>(args[9]) != 0)
    return std::string{"Camera.create: plane-test mode unsupported"};
  uint16_t slot = 0;
  if (!game.packs->registerPackName("system.rpk", slot, error))
    return "Camera.create: " + error;
  auto typeParent = game.resources->lookup((static_cast<uint32_t>(slot) << 16) | 12, 12);
  if (typeParent.status != LookupStatus::Found || !typeParent.node.packNode)
    return std::string{"Camera.create: system camera parent unavailable"};
  auto &typeRoot = *typeParent.node.packNode;
  auto *camera = game.world->addTypedChildUnderExternal(typeRoot.handle, typeRoot,
      typeRoot.type, typeRoot.sectorLink.owner, 12, "_rendertype_camera", 2);
  if (!camera) return std::string{"Camera.create: camera resource allocation failed"};
  camera->camera = makeCameraResource(pixelWidth / pixelHeight, std::get<float>(args[3]),
      std::get<float>(args[4]), std::get<float>(args[5]), std::get<float>(args[6]),
      std::get<float>(args[7]), std::get<int32_t>(args[8]), std::get<int32_t>(args[9]));
  game.world->markLoaded(*camera);
  camera->loadPriority = 1.0f;
  WorldNode *render = parent.worldNode
      ? game.world->addTypedChild(*parent.worldNode, 3, "s_renderref_camera", 0x201)
      : game.world->addTypedChildUnderExternal(parent.packNode->handle, *parent.packNode,
          parent.type, parent.packNode->sectorLink.owner, 3, "s_renderref_camera", 0x201);
  if (!render) {
    game.world->destroy(*camera);
    return std::string{"Camera.create: render instance allocation failed"};
  }
  // The newly constructed instance has no bones at the point 00539880 copies
  // its parent's bounds, before bone00 is resolved by the native.
  render->bounds = parent.worldNode ? parent.worldNode->bounds
                                   : parent.packNode->bounds.value_or(ResourceBounds{});
  render->field6c = parent.worldNode ? parent.worldNode->field6c : parent.packNode->field6c;
  render->field70 = parent.worldNode ? parent.worldNode->field70 : parent.packNode->field70;
  game.world->markLoaded(*render);
  render->loadPriority = 1.0f;
  const auto resolvedRender = game.resources->resolve(render);
  auto instance = std::make_shared<SceneRenderInstance>(camera);
  if (!resolvedRender || !game.resources->bindSceneRenderInstance(resolvedRender, instance)) {
    game.world->destroy(*render);
    game.world->destroy(*camera);
    return std::string{"Camera.create: render binding failed"};
  }
  if (!viewport.renderAttachments)
    viewport.renderAttachments = std::make_shared<ViewportRenderAttachments>();
  viewport.renderAttachments->prepare(*render, std::nullopt);
  if (!viewport.renderAttachments->activate(*render, std::get<int32_t>(args[2]))) {
    game.world->destroy(*render);
    game.world->destroy(*camera);
    return std::string{"Camera.create: viewport camera ordering unavailable"};
  }
  render->cameraFrame = std::make_shared<CameraFrame>(*game.resources, *game.renderer);
  setLinkOwner(render->cameraViewport, viewportNode);
  const auto selfHandle = static_cast<int32_t>(self - arena->resourceStates.data()) + 1;
  auto &binding = game.bindings[selfHandle];
  if (!binding) binding = std::make_unique<ReceiverBinding>();
  setLinkOwner(binding->link, render);
  self->words[2] = binding->link.cachedValue;
  return std::monostate{};
}

NativeResult cameraDestroy(const std::vector<VmValue> &args, const VmObject *receiver,
                           VmArena *arena, const std::vector<const TufaClass *> *) {
  if (!args.empty()) return std::string{"invalid Camera.destroy call"};
  auto state = resolve(receiver, arena);
  if (!state.game || !state.resource) return std::string{"Camera.destroy: native link unavailable"};
  if (!state.binding || !state.binding->link.owner) return std::monostate{};
  if (!state.node || state.node->type != 3 || !state.node->loaded || !state.game->resources)
    return std::string{"Camera.destroy: render instance unavailable"};
  auto instance = state.game->resources->sceneRenderInstance(state.game->resources->resolve(state.node));
  if (!instance) return std::string{"Camera.destroy: render payload unavailable"};
  WorldNode *camera = nullptr;
  if ((instance->typeId() & 0xffff0000u) == 0xffff0000u) {
    auto found = state.game->resources->lookup(instance->typeId(), 12);
    camera = found.node.worldNode;
    if (!camera || !camera->camera) return std::string{"Camera.destroy: camera resource unavailable"};
  }
  auto &world = *state.game->world;
  world.noteRecentUse(*state.node, state.node->type, state.node->stamp74, state.node->recentUseQueued);
  if (camera && !world.queueDestroy(*camera)) return std::string{"Camera.destroy: camera queue failed"};
  if (!world.queueDestroy(*state.node)) return std::string{"Camera.destroy: render queue failed"};
  return std::monostate{};
}

NativeResult viewportCreate(const std::vector<VmValue> &args,
                            const VmObject *receiver, VmArena *arena,
                            const std::vector<const TufaClass *> *) {
  if (!rectangleArgs(args, 1) || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid Viewport.create call"};
  auto state = resolve(receiver, arena);
  if (!state.game)
    return std::string{"Viewport.create: no game world state"};
  if (!state.resource)
    return std::string{
        "Viewport.create: stock error: receiver has no native link"};
  auto *parent = state.game->world->defaultParent();
  if (!parent)
    return std::string{"Viewport.create: world root unavailable"};
  auto *node = state.game->world->addTypedChild(*parent, 18, "_viewport", 0);
  if (!node)
    return std::string{"Viewport.create: viewport allocation failed"};
  state.game->world->markLoaded(*node);
  node->loadPriority = 1.0f;
  node->viewport =
      ViewportInstance{std::get<int32_t>(args[0]), std::get<float>(args[1]),
                       std::get<float>(args[2]), std::get<float>(args[3]),
                       std::get<float>(args[4]), ViewportList::Pending, {}};
  state.game->world->registerViewport(*node, std::get<int32_t>(args[0]));
  const auto handle =
      static_cast<int32_t>(state.resource - arena->resourceStates.data()) + 1;
  auto &binding = state.game->bindings[handle];
  if (!binding)
    binding = std::make_unique<ReceiverBinding>();
  // Stock 00538d60 detaches the previous node from the receiver's intrusive
  // links before linking the new type-0x12 node.
  setLinkOwner(binding->link, nullptr);
  setLinkOwner(binding->link, node);
  state.resource->words[2] = binding->link.cachedValue;
  return std::monostate{};
}

NativeResult viewportActivate(const std::vector<VmValue> &args,
                              const VmObject *receiver, VmArena *arena,
                              const std::vector<const TufaClass *> *) {
  if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid Viewport.activate call"};
  auto state = resolve(receiver, arena);
  if (auto *instance = viewport(state);
      instance && instance->list == ViewportList::Pending) {
    if (!state.game->world->activateViewport(*state.node))
      return std::string{"Viewport.activate: viewport ordering unavailable"};
    instance->list = ViewportList::Active;
  }
  return std::monostate{};
}

NativeResult viewportDeactivate(const std::vector<VmValue> &args,
                                const VmObject *receiver, VmArena *arena,
                                const std::vector<const TufaClass *> *) {
  if (!args.empty())
    return std::string{"invalid Viewport.deactivate call"};
  auto state = resolve(receiver, arena);
  if (auto *instance = viewport(state);
      instance && instance->list == ViewportList::Active) {
    state.game->world->deactivateViewport(*state.node);
    instance->list = ViewportList::Removed;
  }
  return std::monostate{};
}

NativeResult viewportDestroy(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *arena,
                             const std::vector<const TufaClass *> *classes) {
  if (!args.empty())
    return std::string{"invalid Viewport.destroy call"};
  auto state = resolve(receiver, arena);
  if (!state.resource)
    return std::string{
        "Viewport.destroy: stock error: receiver has no native link"};
  auto result = viewportDeactivate(args, receiver, arena, classes);
  if (std::holds_alternative<std::string>(result))
    return result;
  if (state.binding && state.binding->link.cachedValue &&
      state.binding->link.owner) {
    setLinkOwner(state.binding->link, nullptr);
    state.resource->words[2] = 0;
  }
  return std::monostate{};
}

NativeResult viewportResize(const std::vector<VmValue> &args,
                            const VmObject *receiver, VmArena *arena,
                            const std::vector<const TufaClass *> *) {
  if (!rectangleArgs(args, 0))
    return std::string{"invalid Viewport.resize call"};
  auto state = resolve(receiver, arena);
  if (auto *instance = viewport(state)) {
    instance->left = std::get<float>(args[0]);
    instance->top = std::get<float>(args[1]);
    instance->width = std::get<float>(args[2]);
    instance->height = std::get<float>(args[3]);
  }
  return std::monostate{};
}

NativeResult viewportGetLeft(const std::vector<VmValue> &a, const VmObject *r,
                             VmArena *v,
                             const std::vector<const TufaClass *> *) {
  return getCoordinate(a, r, v, &ViewportInstance::left);
}
NativeResult viewportGetTop(const std::vector<VmValue> &a, const VmObject *r,
                            VmArena *v,
                            const std::vector<const TufaClass *> *) {
  return getCoordinate(a, r, v, &ViewportInstance::top);
}
NativeResult viewportGetWidth(const std::vector<VmValue> &a, const VmObject *r,
                              VmArena *v,
                              const std::vector<const TufaClass *> *) {
  return getCoordinate(a, r, v, &ViewportInstance::width);
}
NativeResult viewportGetHeight(const std::vector<VmValue> &a, const VmObject *r,
                               VmArena *v,
                               const std::vector<const TufaClass *> *) {
  return getCoordinate(a, r, v, &ViewportInstance::height);
}

NativeResult viewportGetAspect(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!args.empty())
    return std::string{"invalid Viewport.getAspect call"};
  auto state = resolve(receiver, arena);
  auto *instance = viewport(state);
  if (!instance)
    return VmValue{1.0f};
  if (!arena->displaySize)
    return std::string{"Viewport.getAspect: output size unavailable"};
  const double width = instance->width;
  return VmValue{static_cast<float>((width * arena->displaySize->width) /
                                    (width * arena->displaySize->height))};
}
