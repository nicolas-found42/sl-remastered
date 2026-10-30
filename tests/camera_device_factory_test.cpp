#include "camera_device_factory.hpp"
#include "camera_device_body.hpp"
#include "resource_world.hpp"
#include "render2d.hpp"
#include "scene_2d.hpp"
#include "world_model.hpp"
#include <cstdio>
#include <SDL3/SDL.h>

int main() {
  GameWorld world;
  auto *root = world.addRoot();
  if (!root) return 1;
  PackRegistry packs(".scratch/2005-extract/Program Files/Street Legal Racing - Redline");
  ResourceWorld resources(world, packs);
  auto *game = world.addChild(*root, "camera", 0x21);
  if (!game) return 2;
  world.markLoaded(*game);
  const auto initialNodes = world.nodeCount();
  std::string error;
  auto prototype = makeCameraDevicePrototype(error);
  CameraDeviceConfig config{55, 0.02f, 2, 1, -0.7f, 0.2f, 200, 20, true};
  const auto args = parseCameraDeviceArgs("1,2,3,0,0,0,0", 0.2f, 200);
  auto prepared = prepareCameraDeviceResources(world, resources, *game, config,
                                                args, prototype, error);
  if (!prototype || !prepared || !error.empty() || !prepared->physicsNode() ||
      !prepared->cameraNode() || !prepared->renderNode()) return 3;
  auto scene = resources.sceneRenderInstance(resources.resolve(prepared->renderNode()));
  auto body = world.dynamicBody(*prepared->physicsNode()).lock();
  if (!scene || !body || !scene->hasPhysicsBoneAttachment() ||
      !scene->refreshPhysicsBone()) return 4;
  body->state[body->current].pos = {4, 5, 6};
  if (!scene->refreshPhysicsBone()) return 5;
  scene->processRootBoneUpdate();
  if (scene->derivedRootBoneMatrix()[12] != 40 ||
      scene->derivedRootBoneMatrix()[13] != 50 ||
      scene->derivedRootBoneMatrix()[14] != 60) return 6;
  {
    auto surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
        SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    auto renderer = surface ? Render2D::forSurface(surface.get(), error) : nullptr;
    auto *viewport = world.addTypedChild(*root, 18, "viewport", 0);
    auto *otherViewport = world.addTypedChild(*root, 18, "other viewport", 0);
    auto *otherRender = world.addTypedChild(*root, 3, "other render", 0);
    if (!renderer || !viewport || !otherViewport || !otherRender) return 11;
    viewport->viewport = ViewportInstance{0, 0, 0, 1, 1, ViewportList::Pending, {}};
    otherViewport->viewport = ViewportInstance{0, 0, 0, 1, 1, ViewportList::Pending, {}};
    viewport->viewport->renderAttachments = std::make_shared<ViewportRenderAttachments>();
    auto &attachments = *viewport->viewport->renderAttachments;
    attachments.prepare(*otherRender, 5);
    if (!attachments.activate(*otherRender, 5) ||
        !prepared->attachViewport(resources, *renderer, *viewport, 1, 250.0f, error) ||
        prepared->cameraNode()->camera->farDistance != 2500.0f) return 12;
    auto active = attachments.active();
    if (active.size() != 2 || active[0] != prepared->renderNode() || active[1] != otherRender) return 13;
    if (!prepared->attachViewport(resources, *renderer, *viewport, 9, 300.0f, error) ||
        attachments.active() != active || prepared->cameraNode()->camera->farDistance != 3000.0f) return 14;
    if (prepared->attachViewport(resources, *renderer, *otherViewport, 0, 200.0f, error) ||
        error != "camera render: multiple viewports unsupported") return 15;
    if (!world.destroy(*viewport) || prepared->renderNode()->cameraViewport.owner) return 16;
    if (!prepared->attachViewport(resources, *renderer, *otherViewport, 0, 200.0f, error)) return 17;
    prepared.reset();
    if (!otherViewport->viewport->renderAttachments->active().empty()) return 18;
    if (!world.destroy(*otherViewport) || !world.destroy(*otherRender)) return 19;
  }
  prepared.reset();
  if (!scene->hasPhysicsBoneAttachment() || scene->refreshPhysicsBone()) return 7;
  if (world.nodeCount() != initialNodes || world.dynamicBodyCount() != 0 ||
      !world.children(*game) || !world.children(*game)->empty()) return 8;
  if (prepareCameraDeviceResources(world, resources, *game, config, args, {}, error) ||
      error != "missing camera body prototype" ||
      world.nodeCount() != initialNodes || world.dynamicBodyCount() != 0) return 9;
  {
    auto temporaryWorld = std::make_unique<GameWorld>();
    auto *temporaryRoot = temporaryWorld->addRoot();
    auto *temporaryNode = temporaryWorld->addTypedChild(*temporaryRoot, 2, "body", 0);
    auto survivingOwner = std::make_unique<CameraDeviceResources>(*temporaryWorld);
    setLinkOwner(survivingOwner->physics, temporaryNode);
    temporaryWorld.reset();
    if (survivingOwner->physicsNode()) return 10;
    survivingOwner.reset();
  }
  std::puts("camera_device_factory_test: ok");
}
