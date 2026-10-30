#include "camera_frame.hpp"
#include "force_render.hpp"
#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "natives/physics.hpp"
#include "resource_world.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cmath>
#include <memory>

int main() {
  using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
  Surface surface(SDL_CreateSurface(128, 96, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  std::string error;
  auto renderer = surface ? Render2D::forSurface(surface.get(), error) : nullptr;
  if (!renderer) return 1;
  GameWorld world;
  if (!world.addRoot()) return 1;
  {
    auto *root = world.defaultParent();
    auto *physics = world.addTypedChild(*root, 2, "bone physics", 0);
    auto prototype = std::make_shared<physics::BodyProto>();
    auto body = std::make_shared<physics::Body>(physics::createBody(*prototype, {}));
    if (!physics || !world.registerDynamicBody(*physics, body, prototype)) return 1;
    SceneRenderInstance scene(nullptr);
    if (!scene.attachPhysicsBone(world, *physics)) return 1;
    body->state[0].pos = {2, -3, 4};
    body->state[0].rot = {1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0};
    if (!scene.refreshPhysicsBone()) return 1;
    scene.processRootBoneUpdate();
    const std::array<float, 16> expected{1, 4, 7, 0, 2, 5, 8, 0,
                                         3, 6, 9, 0, 20, -30, 40, 1};
    if (scene.derivedRootBoneMatrix() != expected) return 1;
    body->state[1].pos = {5, 6, 7};
    body->current = 1;
    if (!scene.refreshPhysicsBone()) return 1;
    scene.processRootBoneUpdate();
    if (scene.derivedRootBoneMatrix()[12] != 50 || scene.derivedRootBoneMatrix()[13] != 60) return 1;
    scene.detachPhysicsBone();
    if (scene.refreshPhysicsBone() || !scene.attachPhysicsBone(world, *physics)) return 1;
    body->current = 2;
    if (scene.refreshPhysicsBone()) return 1;
    body->current = 1;
    if (!world.unregisterDynamicBody(*physics) || scene.refreshPhysicsBone()) return 1;
    if (!world.registerDynamicBody(*physics, body, prototype) || !scene.attachPhysicsBone(world, *physics)) return 1;
    if (!world.destroy(*physics) || scene.refreshPhysicsBone()) return 1;
    scene.detachPhysicsBone();
    if (scene.refreshPhysicsBone()) return 1;
  }
  PackRegistry packs("ref/raw");
  uint16_t slot = 0xffff;
  if (!packs.registerPackName("system.rpk", slot, error)) return 1;
  ResourceWorld resources(world, packs);
  auto parentRoot = resources.lookup(4, 1);
  if (!parentRoot.node.packNode || !resources.loadEmptyGameParent(*parentRoot.node.packNode, error)) return 1;
  auto *parent = resources.addGameChild(*parentRoot.node.packNode, "camera-parent", 0);
  if (!parent) return 1;
  world.markLoaded(*parent);
  VmGameState game;
  game.world = &world;
  game.resources = &resources;
  game.packs = &packs;
  game.renderer = renderer.get();
  VmArena arena;
  arena.gameState = &game;
  TufaClass cls;
  cls.name = "java.util.resource.ResourceRef";
  cls.hasClassHeader = true;
  std::vector<const TufaClass *> classes{&cls};
  const auto makeObject = [&]() -> const VmObject * {
    auto object = std::make_unique<VmObject>();
    object->runtimeClass = &cls;
    object->fields.push_back({"ptr", int32_t{0}, "I"});
    const auto *result = object.get();
    arena.objects.push_back(std::move(object));
    auto call = invokeNative("java.util.resource.ResourceRef", "newNative", "()V", {}, result, &arena, &classes);
    return std::holds_alternative<std::monostate>(call) ? result : nullptr;
  };
  const auto *parentObject = makeObject();
  const auto *viewportObject = makeObject();
  const auto *cameraObject = makeObject();
  const auto *boxObject = makeObject();
  if (!parentObject || !viewportObject || !cameraObject || !boxObject) return 1;
  auto *parentState = nativeResourceState(parentObject, &arena);
  const auto parentHandle = static_cast<int32_t>(parentState - arena.resourceStates.data()) + 1;
  game.bindings[parentHandle] = std::make_unique<ReceiverBinding>();
  setLinkOwner(game.bindings[parentHandle]->link, parent);
  auto boxResult = physicsCreateBox({parentObject, 10.0f, 20.0f, 2.0f, static_cast<const VmObject *>(nullptr)}, boxObject, &arena, &classes);
  if (!std::holds_alternative<std::monostate>(boxResult)) return 1;
  auto *boxState = nativeResourceState(boxObject, &arena);
  const auto boxHandle = static_cast<int32_t>(boxState - arena.resourceStates.data()) + 1;
  auto *box = world.nodeFor(game.bindings.at(boxHandle)->link.owner);
  if (!box || box->type != 2 || !box->loaded || !(box->flags & 0x100) || !box->physicsBox ||
      box->physicsBox->halfExtents != std::array<float, 3>{5.0f, 10.0f, 1.0f} || box->bounds.mode != 0 ||
      box->physicsBox->geometryFlags != 5 || world.staticBoxGeometryCount() != 1 || world.staticBoxBodyCount() != 1) return 1;
  VmObject position; position.fields = {{"x", 2.0f, "F"}, {"y", 3.0f, "F"}, {"z", 4.0f, "F"}};
  if (!std::holds_alternative<std::monostate>(physicsSetMatrix({&position, static_cast<const VmObject *>(nullptr)}, boxObject, &arena, &classes)) ||
      box->physicsBox->position[0] != std::array<float, 3>{2.0f, 3.0f, 4.0f} || box->physicsBox->position[1] != box->physicsBox->position[0] ||
      box->bounds.mode != 1 || box->bounds.values[0] != 2 || box->bounds.values[3] != box->physicsBox->radius) return 1;
  if (!world.stepEmptyPhysics(0.02f, error)) return 1;
  if (!world.destroy(*box) || game.bindings.at(boxHandle)->link.owner || world.staticBoxGeometryCount() || world.staticBoxBodyCount()) return 1;
  if (!std::holds_alternative<std::monostate>(invokeNative("java.util.resource.GameRef", "setFlags", "(I)V",
      {int32_t{0x10}}, parentObject, &arena, &classes))) return 1;
  auto result = invokeNative("java.render.Viewport", "create", "(IFFFF)V",
      {int32_t{0}, 0.0f, 0.0f, 1.0f, 1.0f}, viewportObject, &arena, &classes);
  if (!std::holds_alternative<std::monostate>(result)) return 1;
  auto *viewportState = nativeResourceState(viewportObject, &arena);
  const auto viewportHandle = static_cast<int32_t>(viewportState - arena.resourceStates.data()) + 1;
  auto *viewport = world.nodeFor(game.bindings.at(viewportHandle)->link.owner);
  game.renderer = nullptr;
  const std::vector<VmValue> args{parentObject, viewportObject, int32_t{0}, 90.0f, 0.1f, 100.0f,
                                  1.0f, 1.0f, int32_t{1}, int32_t{0}};
  result = invokeNative("java.render.Camera", "create", "(Ljava.util.resource.ResourceRef;Ljava.render.Viewport;IFFFFFII)V",
                        args, cameraObject, &arena, &classes);
  if (!std::holds_alternative<std::string>(result)) return 1;
  game.renderer = renderer.get();
  result = invokeNative("java.render.Camera", "create", "(Ljava.util.resource.ResourceRef;Ljava.render.Viewport;IFFFFFII)V",
                        args, cameraObject, &arena, &classes);
  if (const auto *failure = std::get_if<std::string>(&result)) {
    std::printf("FAIL Camera.create: %s\n", failure->c_str()); return 1;
  }
  auto *cameraState = nativeResourceState(cameraObject, &arena);
  const auto cameraHandle = static_cast<int32_t>(cameraState - arena.resourceStates.data()) + 1;
  auto *render = world.nodeFor(game.bindings.at(cameraHandle)->link.owner);
  if (!render || !render->cameraFrame || render->cameraViewport.owner != viewport ||
      !viewport || !viewport->viewport || !viewport->viewport->renderAttachments ||
      viewport->viewport->renderAttachments->active().size() != 1) return 1;
  const auto instance = resources.sceneRenderInstance(resources.resolve(render));
  auto cameraNode = instance ? resources.lookup(instance->typeId(), 12) : LookupResult{};
  if (!cameraNode.node.worldNode || !cameraNode.node.worldNode->camera ||
      cameraNode.node.worldNode->camera->nearDistance != 1.0f ||
      cameraNode.node.worldNode->camera->viewportAspect != 4.0f / 3.0f) return 1;
  if (renderActiveViewport(world, *viewport, 1, 0, false, error)) return 1;
  if (!std::holds_alternative<std::monostate>(invokeNative("java.render.Viewport", "activate", "(I)V",
      {int32_t{0}}, viewportObject, &arena, &classes))) return 1;
  if (!renderActiveViewport(world, *viewport, 1, 0, false, error)) {
    std::printf("FAIL native viewport frame: %s\n", error.c_str()); return 1;
  }
  {
    auto *physicsNode = world.addTypedChild(*parent, 2, "camera frame body", 0);
    auto prototype = std::make_shared<physics::BodyProto>();
    auto body = std::make_shared<physics::Body>(physics::createBody(*prototype, {}));
    if (!physicsNode || !world.registerDynamicBody(*physicsNode, body, prototype) ||
        !instance->attachPhysicsBone(world, *physicsNode)) return 1;
    body->state[body->current].pos = {2, 3, 4};
    if (!render->cameraFrame->draw(*render, *viewport->viewport, 1, 0, false, error)) {
      std::printf("FAIL camera frame with physics bone: %s\n", error.c_str()); return 1;
    }
    const auto &matrix = instance->derivedRootBoneMatrix();
    if (matrix[12] != 20 || matrix[13] != 30 || matrix[14] != 40) return 1;
    if (!world.unregisterDynamicBody(*physicsNode)) return 1;
    if (render->cameraFrame->draw(*render, *viewport->viewport, 1, 0, false, error) ||
        error != "camera frame: physics bone unavailable") return 1;
    instance->detachPhysicsBone();
    if (!world.destroy(*physicsNode) ||
        !render->cameraFrame->draw(*render, *viewport->viewport, 1, 0, false, error)) return 1;
  }
  TufaClass config; config.name = "java.util.Config";
  std::vector<VmStaticFields> configFields{{&config, {{"object_detail", 0.4f, "F"}, {"object_detail_amp", 2.0f, "F"}}}};
  FrameVisibilityState visibility;
  if (!visibility.applyConfig(configFields, error)) return 1;
  visibility.setPriority(1);
  const auto priorFrame = world.frameCounter();
  if (!forceRenderFrames(world, resources, *renderer, false, 1000000, 1000000, 1000000, 1000000, 2, &visibility, error)) {
    std::printf("FAIL forced camera frames: %s\n", error.c_str()); return 1;
  }
  if (world.frameCounter() != priorFrame + 7) return 1;
  parent->cursor = std::make_unique<CursorInstance>();
  parent->nativeDevice = "cursor";
  parent->eventMask = 0x10;
  parent->eventSubscriptionsKnownEmpty = true;
  const auto sendCursor = [&](const std::string &text) {
    auto value = std::make_unique<VmObject>();
    auto *object = value.get();
    arena.objects.push_back(std::move(value));
    arena.strings.emplace_back(text, object);
    return invokeNative("java.util.resource.GameRef", "queueEvent",
        "(Ljava.util.resource.ResourceRef;ILjava.lang.String;)V",
        {static_cast<const VmObject *>(nullptr), int32_t{0x10}, static_cast<const VmObject *>(object)},
        parentObject, &arena, &classes);
  };
  const std::string command = "cursor " + std::to_string(static_cast<int32_t>(viewport->instanceId)) + " " +
      std::to_string(static_cast<int32_t>(render->instanceId)) + " " +
      std::to_string(static_cast<int32_t>(parent->instanceId));
  if (!std::holds_alternative<std::monostate>(sendCursor(command)) ||
      parent->cursor->viewport.owner != viewport || parent->cursor->camera.owner != render ||
      parent->cursor->target.owner != parent) return 1;
  if (!std::holds_alternative<std::monostate>(sendCursor("activate " + std::to_string(static_cast<int32_t>(parent->instanceId)))) ||
      parent->cursor->controller.owner != parent || !parent->cursor->active) return 1;
  if (!std::holds_alternative<std::monostate>(sendCursor("deactivate")) || parent->cursor->controller.owner || parent->cursor->active) return 1;
  if (!std::holds_alternative<std::monostate>(sendCursor("enable")) || !(parent->cursor->flags & 1)) return 1;
  if (!std::holds_alternative<std::monostate>(sendCursor("move 2, -0.5")) ||
      parent->cursor->x != 1.0f || parent->cursor->y != -0.5f) return 1;
  if (!std::holds_alternative<std::monostate>(sendCursor("relmove -0.5, 2")) ||
      parent->cursor->x != 0.5f || parent->cursor->y != 1.0f) return 1;
  setLinkOwner(parent->cursor->pointer, render);
  if (!std::holds_alternative<std::string>(sendCursor("move 0,0")) ||
      parent->cursor->x != 0.5f) return 1;
  setLinkOwner(parent->cursor->pointer, nullptr);
  if (!std::holds_alternative<std::string>(sendCursor("mode 0 J 0"))) return 1;
  int cursorSelections = 0;
  world.setCursorConfiguration(0, 1);
  world.setCursorSelector([&](int32_t index, bool visible, std::string &) {
    if (index == 0 && visible) ++cursorSelections;
    return true;
  });
  if (!std::holds_alternative<std::monostate>(sendCursor("mode 0 J 0")) ||
      cursorSelections != 1 || parent->cursor->glyph != 'J') return 1;
  const std::array<float, 16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  const auto projection = cameraProjectionMatrix(makeCameraRendererFields(*cameraNode.node.worldNode->camera));
  const auto projected = projectCameraPoint({1.0f, 2.0f, -10.0f}, identity, projection);
  if (!projected) return 1;
  const auto restored = unprojectCameraPoint(*projected, identity, projection);
  if (!restored || std::abs((*restored)[0] - 1.0f) > 0.001f || std::abs((*restored)[1] - 2.0f) > 0.001f || std::abs((*restored)[2] + 10.0f) > 0.001f) return 1;
  if (!std::holds_alternative<std::monostate>(invokeNative("java.render.Camera", "destroy", "()V", {}, cameraObject, &arena, &classes))) return 1;
  if (!game.bindings.at(cameraHandle)->link.owner || viewport->viewport->renderAttachments->active().size() != 1) return 1;
  if (!world.drainDeferredDestroy(error) || game.bindings.at(cameraHandle)->link.owner ||
      parent->cursor->camera.owner || !viewport->viewport->renderAttachments->active().empty()) return 1;
  if (resources.lookup(cameraNode.node.id, 12).status == LookupStatus::Found) return 1;
  std::puts("native_camera_test: ok");
  return 0;
}
