#include "camera_device_factory.hpp"
#include "camera_follow_step.hpp"
#include "ground_runtime.hpp"
#include "pack_registry.hpp"
#include "resource_world.hpp"
#include "timers.hpp"
#include "world_model.hpp"
#include <cmath>
#include <memory>
#include <string>

int main() {
  std::string error;
  auto prototype = physics::createBodyProto(1, 1, 1, 0, 0, nullptr, error);
  if (!prototype)
    return 1;
  auto camera = physics::createBody(*prototype, {0, 0, 0});
  auto target = physics::createBody(*prototype, {10, 0, 0});
  camera.state[0].vel = {0, 1, 0};
  target.state[0].vel = {1, 0, 0};
  physics::World world;
  world.params.gravityEnabled = false;
  world.bodies = {&camera, &target};
  CameraFollowSmoothing smooth;
  CameraFollowStep cb;
  cb.camera = &camera;
  cb.look = &target;
  cb.move = &target;
  cb.smoothing = &smooth;
  cb.input.otherBranchesInactive = true;
  cb.input.maxForce = 3;
  cb.input.command.torque = 1;
  cb.input.command.force = 2;
  cb.input.command.damping = 1;
  cb.input.command.perpendicularDamping = -0.5f;
  cb.input.command.moveDistance = 2;
  const auto original = camera.state[0];
  if (stepCameraFollowWorld(world, 0.02f, {cb}, false, error) ||
      world.time != 0 || camera.state[0].pos != original.pos ||
      camera.state[0].force != original.force || !smooth.initializeLook ||
      !smooth.initializeMove)
    return 2;
  cb.input.otherBranchesInactive = false;
  if (stepCameraFollowWorld(world, 0.02f, {cb}, true, error) ||
      world.time != 0 || camera.state[0].pos != original.pos ||
      !smooth.initializeLook)
    return 3;
  cb.input.otherBranchesInactive = true;
  target.flags |= physics::kBodyHasChildren;
  if (stepCameraFollowWorld(world, 0.02f, {cb}, true, error) ||
      world.time != 0 || camera.state[0].force != original.force ||
      !smooth.initializeMove)
    return 4;
  target.flags &= ~physics::kBodyHasChildren;
  TimerState clock;
  LinkOwner owner;
  owner.value = 1;
  if (!clock.add(&owner, 0.01f, 17, error))
    return 5;
  std::string order;
  const auto physicsStep = [&](float dt, std::string &err) {
    order += 'p';
    return stepCameraFollowWorld(world, dt, {cb}, true, err);
  };
  const auto event = [&](LinkOwner &, int32_t id, uint32_t, int32_t,
                         std::string &) {
    order += 'e';
    return id == 17 && !smooth.initializeLook && world.time > 0 &&
           camera.state[camera.current].vel[0] > 0;
  };
  const auto drain = [&](std::string &) {
    order += 'd';
    return true;
  };
  if (!clock.advanceFrame(0.02f, 1, 1, 0.03f, physicsStep, event, drain,
                          error) ||
      order != "ped" || std::fabs(clock.now() - world.time) > 1e-6f)
    return 6;
  const auto first = smooth.move;
  if (!stepCameraFollowWorld(world, 0.02f, {cb}, true, error) ||
      smooth.move == first)
    return 7;

  GameWorld game;
  auto *root = game.addRoot();
  auto *host = root ? game.addChild(*root, "camera host", 0x21) : nullptr;
  if (!host)
    return 8;
  host->nativeDevice = "camera";
  host->cameraDevice = std::make_shared<CameraDeviceResources>(game);
  auto &device = *host->cameraDevice;
  device.fieldOfView = 55;
  device.maxForce = 20;
  device.commands.torque = 1;
  device.commands.force = 2;
  device.commands.damping = 1;
  device.commands.perpendicularDamping = -0.5f;
  device.commands.moveDistance = 2;
  auto cameraPrototype =
      physics::createBodyProto(0.05f, 1, 0.15f, 0, 0, nullptr, error);
  auto targetPrototype =
      physics::createBodyProto(1, 1, 1, 0, 0, nullptr, error);
  if (!cameraPrototype || !targetPrototype)
    return 8;
  auto cameraProto =
      std::make_shared<physics::BodyProto>(std::move(*cameraPrototype));
  auto targetProto =
      std::make_shared<physics::BodyProto>(std::move(*targetPrototype));
  auto cameraBody = std::make_shared<physics::Body>(
      physics::createBody(*cameraProto, {0, 0, 0}));
  cameraBody->flags |= 0x4002;
  auto targetBody = std::make_shared<physics::Body>(
      physics::createBody(*targetProto, {10, 0, 0}));
  auto *physicsNode = game.addTypedChild(*root, 2, "camera body", 0);
  auto *targetNode = game.addTypedChild(*root, 2, "follow target", 0);
  if (!physicsNode || !targetNode ||
      !game.registerDynamicBody(*physicsNode, cameraBody, cameraProto) ||
      !game.registerDynamicBody(*targetNode, targetBody, targetProto))
    return 8;
  setLinkOwner(device.physics, physicsNode);
  setLinkOwner(device.lookTarget, targetNode);
  setLinkOwner(device.moveTarget, targetNode);
  const bool stepped = game.stepEmptyPhysics(0.02f, error);
  const float speed = cameraBody->state[cameraBody->current].vel[0];
  if (!game.directCameraFollowReady(device) || stepped ||
      error.find("dynamic bodies") == std::string::npos || speed != 0.0f ||
      game.physicsTime() != 0.0f)
    return 9;

  GameWorld alongside;
  auto *alongsideRoot = alongside.addRoot();
  auto *alongsideHost =
      alongsideRoot ? alongside.addChild(*alongsideRoot, "camera host", 0x21)
                    : nullptr;
  if (!alongsideHost)
    return 10;
  alongsideHost->nativeDevice = "camera";
  alongsideHost->cameraDevice =
      std::make_shared<CameraDeviceResources>(alongside);
  auto &alongsideDevice = *alongsideHost->cameraDevice;
  alongsideDevice.fieldOfView = 55;
  alongsideDevice.maxForce = 20;
  alongsideDevice.commands.torque = 1;
  alongsideDevice.commands.force = 2;
  alongsideDevice.commands.damping = 1;
  alongsideDevice.commands.perpendicularDamping = -0.5f;
  alongsideDevice.commands.moveDistance = 2;
  auto alongsideCameraProto =
      std::make_shared<physics::BodyProto>(*cameraProto);
  auto alongsideTargetProto =
      std::make_shared<physics::BodyProto>(*targetProto);
  auto alongsideCamera = std::make_shared<physics::Body>(
      physics::createBody(*alongsideCameraProto, {0, 0, 0}));
  alongsideCamera->flags |= 0x4002;
  auto alongsideTarget = std::make_shared<physics::Body>(
      physics::createBody(*alongsideTargetProto, {10, 0, 0}));
  alongsideTarget->flags |= physics::kBodyHasChildren;
  const auto targetPosition =
      alongsideTarget->state[alongsideTarget->current].pos;
  auto *alongsidePhysics =
      alongside.addTypedChild(*alongsideRoot, 2, "camera body", 0);
  auto *alongsideCar =
      alongside.addTypedChild(*alongsideRoot, 2, "car root", 0);
  if (!alongsidePhysics || !alongsideCar ||
      !alongside.registerDynamicBody(*alongsidePhysics, alongsideCamera,
                                     alongsideCameraProto) ||
      !alongside.registerDynamicBody(*alongsideCar, alongsideTarget,
                                     alongsideTargetProto))
    return 10;
  setLinkOwner(alongsideDevice.physics, alongsidePhysics);
  setLinkOwner(alongsideDevice.lookTarget, alongsideCar);
  setLinkOwner(alongsideDevice.moveTarget, alongsideCar);
  const bool applied = alongside.stepArmedCameraFollow(0.02f, error);
  const auto &cameraState = alongsideCamera->state[alongsideCamera->current];
  const auto &carState = alongsideTarget->state[alongsideTarget->current];
  if (!applied || !(cameraState.vel[0] > 0.0f) ||
      carState.pos != targetPosition ||
      alongside.dynamicBody(*alongsideCar).expired() ||
      alongside.dynamicBodyCount() != 2)
    return 11;

  alongsideTarget->attachment = physics::BodyAttachment{};
  const auto held = cameraState.vel;
  if (alongside.stepArmedCameraFollow(0.02f, error) ||
      alongsideCamera->state[alongsideCamera->current].vel != held ||
      alongside.dynamicBody(*alongsideCar).expired())
    return 12;

  // A camera whose look/move targets name a resource-world pack node with a
  // prepared ground factory follows that ground's static body: the follow
  // binds through groundFactory->preparation->physics (the garage camera
  // looking at its map resource), the camera moves toward the ground body and
  // the shared preparation body itself is not stepped.
  PackRegistry groundPacks("ref/raw");
  GameWorld groundWorld;
  auto* groundRoot = groundWorld.addRoot();
  if (!groundRoot)
    return 13;
  ResourceWorld groundResources(groundWorld, groundPacks);
  uint16_t groundSlot = 0;
  if (!groundPacks.registerPackName("maps\\city.rpk", groundSlot, error))
    return 14;
  const auto city = groundResources.lookup((uint32_t(groundSlot) << 16) | 1, 1);
  if (!city.node.packNode)
    return 15;
  auto& cityNode = *city.node.packNode;
  auto groundRuntime = createGroundRuntime({10, 0, 0}, 1.0f);
  if (!groundRuntime)
    return 16;
  const auto& groundBody = groundRuntime->body;
  const auto groundPos = groundBody.state[groundBody.current].pos;
  auto groundPrep = std::make_shared<GroundPreparation>();
  groundPrep->physics =
      std::shared_ptr<GroundRuntime>(std::move(groundRuntime));
  auto groundFactory = std::make_shared<GroundFactoryResult>();
  groundFactory->preparation = groundPrep;
  groundFactory->roadWorld = &groundWorld;
  cityNode.groundFactory = groundFactory;
  auto* groundHost = groundWorld.addChild(*groundRoot, "garage camera", 0x21);
  if (!groundHost)
    return 17;
  groundHost->nativeDevice = "camera";
  groundHost->cameraDevice =
      std::make_shared<CameraDeviceResources>(groundWorld);
  auto& groundDevice = *groundHost->cameraDevice;
  groundDevice.fieldOfView = 55;
  groundDevice.maxForce = 20;
  groundDevice.commands.torque = 1;
  groundDevice.commands.force = 2;
  groundDevice.commands.damping = 1;
  groundDevice.commands.perpendicularDamping = -0.5f;
  groundDevice.commands.moveDistance = 2;
  auto groundCamProto = std::make_shared<physics::BodyProto>(*cameraProto);
  auto groundCam = std::make_shared<physics::Body>(
      physics::createBody(*groundCamProto, {0, 0, 0}));
  groundCam->flags |= 0x4002;
  auto* groundCamNode =
      groundWorld.addTypedChild(*groundRoot, 2, "camera body", 0);
  if (!groundCamNode ||
      !groundWorld.registerDynamicBody(*groundCamNode, groundCam,
                                       groundCamProto))
    return 18;
  setLinkOwner(groundDevice.physics, groundCamNode);
  setLinkOwner(groundDevice.lookTarget, &cityNode);
  setLinkOwner(groundDevice.moveTarget, &cityNode);
  if (!groundWorld.directCameraFollowReady(groundDevice))
    return 19;
  // The garage-camera scenario runs through the camera-only step: the ground
  // follow binds, the camera moves toward the static ground body and the
  // shared preparation body itself is not stepped.
  error.clear();
  if (!groundWorld.stepArmedCameraFollow(0.02f, error))
    return 20;
  const auto& groundCamState = groundCam->state[groundCam->current];
  if (!(groundCamState.vel[0] > 0.0f) ||
      groundBody.state[groundBody.current].pos != groundPos)
    return 21;
  // The follow smoothing initialized to the ground body's pose, proving the
  // target body is the preparation's shared static body.
  if (groundDevice.followSmoothing.look != groundPos ||
      groundDevice.followSmoothing.move != groundPos)
    return 22;

  // A follow whose look/move target is the car's type-1 node owns no dynamic
  // body itself: the follow resolves through carInstance->physicsRoot.owner
  // to the type-2 physics-root child and binds to its body (FUN_00545fc0
  // only stores the node link). With the root link broken the bind fails.
  GameWorld carFollow;
  auto* carFollowRoot = carFollow.addRoot();
  if (!carFollowRoot)
    return 23;
  auto* carNode = carFollow.addChild(*carFollowRoot, "car", 0x21);
  if (!carNode)
    return 24;
  carNode->carInstance = std::make_unique<CarRuntimeInstance>();
  carNode->carInstance->isCar = true;
  auto* carRootNode =
      carFollow.addTypedChild(*carNode, 2, "car_root", 0);
  if (!carRootNode)
    return 25;
  carFollow.markLoaded(*carRootNode);
  setLinkOwner(carNode->carInstance->physicsRoot, carRootNode);
  auto carBodyProto = std::make_shared<physics::BodyProto>(*cameraProto);
  auto carBody = std::make_shared<physics::Body>(
      physics::createBody(*carBodyProto, {6, 0, 0}));
  if (!carFollow.registerDynamicBody(*carRootNode, carBody, carBodyProto))
    return 26;
  auto* carCamHost =
      carFollow.addChild(*carFollowRoot, "chase camera", 0x21);
  if (!carCamHost)
    return 27;
  carCamHost->nativeDevice = "camera";
  carCamHost->cameraDevice =
      std::make_shared<CameraDeviceResources>(carFollow);
  auto& carCamDevice = *carCamHost->cameraDevice;
  carCamDevice.fieldOfView = 55;
  carCamDevice.maxForce = 20;
  carCamDevice.commands.torque = 1;
  carCamDevice.commands.force = 2;
  carCamDevice.commands.damping = 1;
  carCamDevice.commands.perpendicularDamping = -0.5f;
  carCamDevice.commands.moveDistance = 2;
  auto carCamProto = std::make_shared<physics::BodyProto>(*cameraProto);
  auto carCamBody = std::make_shared<physics::Body>(
      physics::createBody(*carCamProto, {0, 0, 0}));
  carCamBody->flags |= 0x4002;
  auto* carCamNode =
      carFollow.addTypedChild(*carFollowRoot, 2, "camera body", 0);
  if (!carCamNode ||
      !carFollow.registerDynamicBody(*carCamNode, carCamBody, carCamProto))
    return 28;
  setLinkOwner(carCamDevice.physics, carCamNode);
  setLinkOwner(carCamDevice.lookTarget, carNode);
  setLinkOwner(carCamDevice.moveTarget, carNode);
  if (!carFollow.directCameraFollowReady(carCamDevice))
    return 29;
  // The bound follow moves the camera toward the car's physics-root body.
  error.clear();
  if (!carFollow.stepArmedCameraFollow(0.02f, error))
    return 30;
  const auto& carCamState = carCamBody->state[carCamBody->current];
  if (!(carCamState.vel[0] > 0.0f))
    return 32;
  // Breaking the physics-root link makes the car target unresolvable: the
  // follow must not bind.
  setLinkOwner(carNode->carInstance->physicsRoot, nullptr);
  if (carFollow.directCameraFollowReady(carCamDevice))
    return 33;
  setLinkOwner(carNode->carInstance->physicsRoot, carRootNode);
  if (!carFollow.directCameraFollowReady(carCamDevice))
    return 34;

  // An untargeted camera (a registered camera body whose device has no
  // look/move targets) is skipped by the follow collection: the car step
  // succeeds and the body stays at its pose instead of failing with
  // "neither a car nor a camera follow".
  GameWorld dealer;
  auto* dealerRoot = dealer.addRoot();
  auto* dealerHost =
      dealerRoot ? dealer.addChild(*dealerRoot, "dealer camera", 0x21)
                 : nullptr;
  if (!dealerHost)
    return 23;
  dealerHost->nativeDevice = "camera";
  dealerHost->cameraDevice =
      std::make_shared<CameraDeviceResources>(dealer);
  auto& dealerDevice = *dealerHost->cameraDevice;
  dealerDevice.fieldOfView = 55;
  dealerDevice.maxForce = 20;
  dealerDevice.commands.torque = 1;
  dealerDevice.commands.force = 2;
  dealerDevice.commands.damping = 1;
  dealerDevice.commands.perpendicularDamping = -0.5f;
  dealerDevice.commands.moveDistance = 2;
  auto dealerProto = std::make_shared<physics::BodyProto>(*cameraProto);
  auto dealerBody = std::make_shared<physics::Body>(
      physics::createBody(*dealerProto, {3, 1, 0}));
  dealerBody->flags |= 0x4002;
  const auto dealerPos = dealerBody->state[dealerBody->current].pos;
  const auto dealerVel = dealerBody->state[dealerBody->current].vel;
  auto* dealerNode = dealer.addTypedChild(*dealerRoot, 2, "camera body", 0);
  if (!dealerNode ||
      !dealer.registerDynamicBody(*dealerNode, dealerBody, dealerProto))
    return 24;
  setLinkOwner(dealerDevice.physics, dealerNode);
  LiveCarStepHooks dealerHooks;
  dealerHooks.gatherContacts =
      [](std::vector<physics::DirectContact>&, std::string&) {
        return true;
      };
  error.clear();
  if (!dealer.stepRegisteredCars(0.02f, dealerHooks, error))
    return 25;
  if (dealerBody->state[dealerBody->current].pos != dealerPos ||
      dealerBody->state[dealerBody->current].vel != dealerVel)
    return 26;
  return 0;
}
