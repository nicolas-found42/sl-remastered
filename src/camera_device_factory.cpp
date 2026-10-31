#include "camera_device_factory.hpp"
#include "camera_device_body.hpp"
#include "camera_frame.hpp"
#include "ground_runtime.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"
#include "world_model.hpp"

void applyCameraSectorEnvironment(CameraResource &camera,
                                  const GroundFactoryResult &ground,
                                  float farDistance) {
  camera.farDistance = farDistance * 10.0f;
  if (ground.fogSetting)
    camera.fog =
        CameraFog{ground.fogSetting->color, ground.fogSetting->nearDist,
                  ground.fogSetting->farDist};
}

WorldNode *CameraDeviceResources::physicsNode() const {
  return physics.owner ? world_->nodeFor(physics.owner) : nullptr;
}
WorldNode *CameraDeviceResources::cameraNode() const {
  return camera.owner ? world_->nodeFor(camera.owner) : nullptr;
}
WorldNode *CameraDeviceResources::renderNode() const {
  return render.owner ? world_->nodeFor(render.owner) : nullptr;
}
bool CameraDeviceResources::applySeatFollow(WorldNode &car,
                                            const CarDriver &driver,
                                            const CarCameraSettings &settings) {
  if (world_->nodeFor(&car) != &car || car.type != 2 || !driver.hasExtCamera)
    return false;
  const auto emitted = seatCameraCommands(car.instanceId, driver, settings);
  CameraFollowCommand follow;
  if (!parseCameraFollowCommand(emitted[0], commands.torque, follow))
    return false;
  setLinkOwner(lookTarget, &car);
  applyCameraFollowCommand(commands, follow);
  if (!parseCameraFollowCommand(emitted[1], commands.torque, follow))
    return false;
  setLinkOwner(moveTarget, &car);
  applyCameraFollowCommand(commands, follow);
  if (!emitted[2].empty())
    applyCameraCommand(commands, emitted[2]);
  return true;
}
bool CameraDeviceResources::attachViewport(ResourceWorld &resources,
                                           Render2D &renderer,
                                           WorldNode &viewport,
                                           int32_t priority, float farDistance,
                                           std::string &error) {
  auto *renderNodePtr = renderNode();
  auto *cameraNodePtr = cameraNode();
  if (!renderNodePtr || !cameraNodePtr || !cameraNodePtr->camera ||
      world_->nodeFor(&viewport) != &viewport || viewport.type != 18 ||
      !viewport.viewport) {
    error = "camera render: camera or viewport unavailable";
    return false;
  }
  if (renderNodePtr->cameraViewport.owner &&
      renderNodePtr->cameraViewport.owner != &viewport) {
    error = "camera render: multiple viewports unsupported";
    return false;
  }
  auto &attachments = viewport.viewport->renderAttachments;
  if (!attachments)
    attachments = std::make_shared<ViewportRenderAttachments>();
  const bool alreadyAttached = renderNodePtr->cameraViewport.owner == &viewport;
  attachments->prepare(*renderNodePtr, std::nullopt);
  if (!attachments->activate(*renderNodePtr, priority)) {
    if (!alreadyAttached)
      attachments->remove(*renderNodePtr);
    error = "camera render: viewport priority unavailable";
    return false;
  }
  if (!alreadyAttached || !renderNodePtr->cameraFrame)
    renderNodePtr->cameraFrame =
        std::make_shared<CameraFrame>(resources, renderer);
  setLinkOwner(renderNodePtr->cameraViewport, &viewport);
  if (audioListenerEnabled)
    if (auto *body = physicsNode())
      world_->registerAudioListener(*body);
  cameraNodePtr->camera->farDistance = farDistance * 10.0f;
  error.clear();
  return true;
}
bool CameraDeviceResources::detachViewport(WorldNode &viewport,
                                           std::string &error) {
  auto *renderNodePtr = renderNode();
  if (!renderNodePtr || world_->nodeFor(&viewport) != &viewport ||
      viewport.type != 18 || !viewport.viewport) {
    error = "camera hide: camera or viewport unavailable";
    return false;
  }
  if (renderNodePtr->cameraViewport.owner &&
      renderNodePtr->cameraViewport.owner != &viewport) {
    error = "camera hide: different viewport unsupported";
    return false;
  }
  if (viewport.viewport->renderAttachments)
    viewport.viewport->renderAttachments->remove(*renderNodePtr);
  setLinkOwner(renderNodePtr->cameraViewport, nullptr);
  if (audioListenerEnabled)
    if (auto *body = physicsNode())
      world_->unregisterAudioListener(*body);
  error.clear();
  return true;
}
CameraDeviceResources::~CameraDeviceResources() {
  if (auto *body = physicsNode())
    world_->unregisterAudioListener(*body);
  setLinkOwner(lookTarget, nullptr);
  setLinkOwner(moveTarget, nullptr);
  setLinkOwner(controller, nullptr);
  if (auto *node = renderNode())
    world_->destroy(*node);
  if (auto *node = cameraNode())
    world_->destroy(*node);
  if (auto *node = physicsNode())
    world_->destroy(*node);
  setLinkOwner(render, nullptr);
  setLinkOwner(camera, nullptr);
  setLinkOwner(physics, nullptr);
}

std::unique_ptr<CameraDeviceResources> prepareCameraDeviceResources(
    GameWorld &world, ResourceWorld &resources, WorldNode &gameNode,
    const CameraDeviceConfig &config, const CameraDeviceArgs &args,
    std::shared_ptr<const physics::BodyProto> prototype, std::string &error) {
  error.clear();
  if (world.nodeFor(&gameNode) != &gameNode || gameNode.type != 1 ||
      !gameNode.loaded || !gameNode.sectorLink.owner) {
    error = "camera device: game node or sector unavailable";
    return {};
  }
  auto sector = resources.resolve(gameNode.sectorLink.owner);
  uint32_t typeRootId = 0;
  if (!sector) {
    error = "camera device: sector unavailable";
    return {};
  }
  if (!resources.packs().resolveLiteral("system.rpk", 12, typeRootId, error))
    return {};
  auto typeRoot = resources.lookup(typeRootId, 12);
  if (!typeRoot.node.packNode) {
    error = "camera device: camera type root unavailable";
    return {};
  }
  auto body = makeCameraDeviceBody(std::move(prototype), args,
                                   world.physicsTime(), error);
  if (!body)
    return {};
  std::shared_ptr<CameraDeviceBody> owner(std::move(body));
  auto bodyRef = std::shared_ptr<physics::Body>(owner, &owner->body);
  WorldNode *physics = nullptr, *camera = nullptr, *render = nullptr;
  auto result = std::make_unique<CameraDeviceResources>(world);
  result->commands = cameraCommandState(config);
  result->fieldOfView = config.fieldOfView;
  result->maxForce = config.maxForce;
  result->audioListenerEnabled = (args.flags & 2) != 0;
  physics = world.addTypedChild(gameNode, 2, "_physinst", 0x201);
  if (physics)
    setLinkOwner(result->physics, physics);
  if (!physics ||
      !world.registerDynamicBody(*physics, bodyRef, owner->prototype)) {
    error = "camera device: physics resource unavailable";
    return {};
  }
  world.markLoaded(*physics);
  physics->loadPriority = 1.0f;
  auto &root = *typeRoot.node.packNode;
  camera = world.addTypedChildUnderExternal(root.handle, root, root.type,
                                            root.sectorLink.owner, 12,
                                            "_rendertype_camera", 2);
  if (camera)
    setLinkOwner(result->camera, camera);
  if (!camera) {
    error = "camera device: camera resource unavailable";
    return {};
  }
  camera->camera = makeCameraResource(1.0f / 0.75f, config.fieldOfView,
                                      args.nearDistance, args.farDistance,
                                      args.lodBias, args.lodAmplitude, 1, 0);
  world.markLoaded(*camera);
  camera->loadPriority = 1.0f;
  if (sector.worldNode)
    render = world.addTypedChild(*sector.worldNode, 3, "r_camera", 0x201);
  else if (sector.packNode)
    render = world.addTypedChildUnderExternal(
        sector.packNode->handle, *sector.packNode, sector.type,
        sector.packNode->sectorLink.owner, 3, "r_camera", 0x201);
  if (render)
    setLinkOwner(result->render, render);
  if (!render) {
    error = "camera device: render resource unavailable";
    return {};
  }
  render->bounds = sector.worldNode
                       ? sector.worldNode->bounds
                       : sector.packNode->bounds.value_or(ResourceBounds{});
  render->field6c =
      sector.worldNode ? sector.worldNode->field6c : sector.packNode->field6c;
  render->field70 =
      sector.worldNode ? sector.worldNode->field70 : sector.packNode->field70;
  world.markLoaded(*render);
  render->loadPriority = 1.0f;
  auto scene = std::make_shared<SceneRenderInstance>(camera);
  if (!resources.bindSceneRenderInstance(resources.resolve(render), scene)) {
    error = "camera device: render binding unavailable";
    return {};
  }
  if (!scene->attachPhysicsBone(world, *physics) ||
      !scene->refreshPhysicsBone()) {
    error = "camera device: physics-to-bone attachment failed";
    return {};
  }
  return result;
}
