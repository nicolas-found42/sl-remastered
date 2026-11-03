#include "camera_device_body.hpp"
#include "camera_device_factory.hpp"
#include "render2d.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"
#include "world_model.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cmath>

int main() {
  GameWorld world;
  auto *root = world.addRoot();
  if (!root)
    return 1;
  PackRegistry packs(
      ".scratch/2005-extract/Program Files/Street Legal Racing - Redline");
  ResourceWorld resources(world, packs);
  auto *game = world.addChild(*root, "camera", 0x21);
  if (!game)
    return 2;
  world.markLoaded(*game);
  const auto initialNodes = world.nodeCount();
  std::string error;
  auto prototype = makeCameraDevicePrototype(error);
  CameraDeviceConfig config{55, 0.02f, 2, 1, -0.7f, 0.2f, 200, 20, true};
  const auto args = parseCameraDeviceArgs("1,2,3,0,0,0,2", 0.2f, 200);
  auto prepared = prepareCameraDeviceResources(world, resources, *game, config,
                                               args, prototype, error);
  if (!prototype || !prepared || !error.empty() || !prepared->physicsNode() ||
      !prepared->cameraNode() || !prepared->renderNode())
    return 3;
  auto scene =
      resources.sceneRenderInstance(resources.resolve(prepared->renderNode()));
  auto body = world.dynamicBody(*prepared->physicsNode()).lock();
  if (!scene || !body || !scene->hasPhysicsBoneAttachment() ||
      !scene->refreshPhysicsBone())
    return 4;
  body->state[body->current].pos = {4, 5, 6};
  if (!scene->refreshPhysicsBone())
    return 5;
  scene->processRootBoneUpdate();
  if (scene->derivedRootBoneMatrix()[12] != 40 ||
      scene->derivedRootBoneMatrix()[13] != 50 ||
      scene->derivedRootBoneMatrix()[14] != 60)
    return 6;
  {
    auto surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
        SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    auto renderer =
        surface ? Render2D::forSurface(surface.get(), error) : nullptr;
    auto *viewport = world.addTypedChild(*root, 18, "viewport", 0);
    auto *otherViewport = world.addTypedChild(*root, 18, "other viewport", 0);
    auto *otherRender = world.addTypedChild(*root, 3, "other render", 0);
    if (!renderer || !viewport || !otherViewport || !otherRender)
      return 11;
    viewport->viewport =
        ViewportInstance{0, 0, 0, 1, 1, ViewportList::Pending, {}};
    otherViewport->viewport =
        ViewportInstance{0, 0, 0, 1, 1, ViewportList::Pending, {}};
    viewport->viewport->renderAttachments =
        std::make_shared<ViewportRenderAttachments>();
    auto &attachments = *viewport->viewport->renderAttachments;
    attachments.prepare(*otherRender, 5);
    if (!attachments.activate(*otherRender, 5) ||
        !prepared->attachViewport(resources, *renderer, *viewport, 1, 250.0f,
                                  error) ||
        prepared->cameraNode()->camera->farDistance != 2500.0f)
      return 12;
    physics::Vec3 listenerPosition, listenerForward, listenerUp;
    if (!world.audioListener(listenerPosition, listenerForward, listenerUp) ||
        listenerPosition != physics::Vec3{4, 5, 6})
      return 20;
    auto active = attachments.active();
    if (active.size() != 2 || active[0] != prepared->renderNode() ||
        active[1] != otherRender)
      return 13;
    if (!prepared->attachViewport(resources, *renderer, *viewport, 9, 300.0f,
                                  error) ||
        attachments.active() != active ||
        prepared->cameraNode()->camera->farDistance != 3000.0f)
      return 14;
    if (!prepared->detachViewport(*viewport, error) ||
        !world.audioListener(listenerPosition, listenerForward, listenerUp) ||
        listenerPosition != physics::Vec3{0, 0, 0})
      return 21;
    if (!prepared->attachViewport(resources, *renderer, *viewport, 9, 300.0f,
                                  error))
      return 22;
    if (prepared->attachViewport(resources, *renderer, *otherViewport, 0,
                                 200.0f, error) ||
        error != "camera render: multiple viewports unsupported")
      return 15;
    if (!world.destroy(*viewport) ||
        prepared->renderNode()->cameraViewport.owner)
      return 16;
    if (!prepared->attachViewport(resources, *renderer, *otherViewport, 0,
                                  200.0f, error))
      return 17;
    prepared.reset();
    if (!otherViewport->viewport->renderAttachments->active().empty())
      return 18;
    if (!world.destroy(*otherViewport) || !world.destroy(*otherRender))
      return 19;
  }
  prepared.reset();
  if (!scene->hasPhysicsBoneAttachment() || scene->refreshPhysicsBone())
    return 7;
  if (world.nodeCount() != initialNodes || world.dynamicBodyCount() != 0 ||
      !world.children(*game) || !world.children(*game)->empty())
    return 8;
  if (prepareCameraDeviceResources(world, resources, *game, config, args, {},
                                   error) ||
      error != "missing camera body prototype" ||
      world.nodeCount() != initialNodes || world.dynamicBodyCount() != 0)
    return 9;
  {
    auto temporaryWorld = std::make_unique<GameWorld>();
    auto *temporaryRoot = temporaryWorld->addRoot();
    auto *temporaryNode =
        temporaryWorld->addTypedChild(*temporaryRoot, 2, "body", 0);
    auto survivingOwner =
        std::make_unique<CameraDeviceResources>(*temporaryWorld);
    setLinkOwner(survivingOwner->physics, temporaryNode);
    temporaryWorld.reset();
    if (survivingOwner->physicsNode())
      return 10;
    survivingOwner.reset();
  }
  {
    auto* zoomHost = world.addChild(*root, "zoom camera", 0x21);
    world.markLoaded(*zoomHost);
    auto zoomDevice = prepareCameraDeviceResources(world, resources, *zoomHost,
                                                   config, args, makeCameraDevicePrototype(error), error);
    if (!zoomDevice)
      return 23;
    zoomHost->cameraDevice = std::shared_ptr<CameraDeviceResources>(std::move(zoomDevice));
    auto& device = *zoomHost->cameraDevice;
    if (!applyCameraCommand(device.commands, "zoom 100"))
      return 24;
    if (!world.advanceCameraControls(0.02f, error))
      return 29;
    if (device.fieldOfView != 57.7f ||
        device.cameraNode()->camera->fieldOfView != 57.7f)
      return 25;
    auto* player = world.addChild(*root, "camera input controller", 0x21);
    if (!player)
      return 26;
    world.markLoaded(*player);
    player->nativeDevice = "player";
    player->player = std::make_unique<PlayerInstance>();
    player->playerInput = std::make_unique<PlayerInputState>();
    auto& input = *player->playerInput;
    const std::array<float, 12> axes{0, 0, 0, 0, 0, 0, 0, 0.025f, 0.025f, 0, 0.5f, 0};
    input.sampleInput = [](int32_t, int32_t) { return std::optional<float>{1}; };
    input.inputType = [](int32_t, int32_t) { return std::optional<int32_t>{0}; };
    for (int32_t axis : {7, 8, 10})
      if (!input.add(axis, 0, axis, 0, {0, 1, 0, axes[static_cast<size_t>(axis)]}, 0))
        return 27;
    input.beginFrame(0.02f);
    setLinkOwner(device.controller, player);
    device.commands.moveDistance = 4;
    device.commands.zoomCurrent = 55;
    if (!world.advanceCameraControls(0.02f, error))
      return 29;
    if (device.commands.moveDistance != 8 ||
        std::fabs(device.control.localForce[0] + 7.071068f) > 0.00001f ||
        std::fabs(device.control.localForce[1] - 7.071068f) > 0.00001f)
      return 28;
    const float heldZoom = device.commands.zoomCurrent;
    const auto heldForce = device.control.localForce;
    input.sampleInput = {};
    if (world.advanceCameraControls(0.02f, error) || error.find("axis 7") == std::string::npos ||
        device.commands.zoomCurrent != heldZoom || device.control.localForce != heldForce)
      return 30;
    setLinkOwner(device.controller, nullptr);
    device.control.freeCamera = true;
    device.control.localForce = {2, 0, 0};
    device.control.localTorque = {};
    const auto freeBody = world.dynamicBody(*device.physicsNode()).lock();
    if (!freeBody || device.lookTarget.owner || device.moveTarget.owner ||
        !world.directCameraFollowReady(device))
      return 31;
    const auto beforeFree = freeBody->state[freeBody->current].vel[0];
    if (!world.stepArmedCameraFollow(0.02f, error) ||
        world.dynamicBody(*device.physicsNode()).expired() ||
        freeBody->state[freeBody->current].vel[0] <= beforeFree) {
      std::fprintf(stderr, "free camera world step: %s, vx=%g before=%g\n", error.c_str(), double(freeBody->state[freeBody->current].vel[0]), double(beforeFree));
      return 32;
    }
  }
  std::puts("camera_device_factory_test: ok");
}
