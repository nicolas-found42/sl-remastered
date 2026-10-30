#include "camera_follow_step.hpp"
#include "camera_device_factory.hpp"
#include "timers.hpp"
#include "world_model.hpp"
#include <cmath>
#include <memory>
#include <string>

int main() {
  std::string error;
  auto prototype = physics::createBodyProto(1, 1, 1, 0, 0, nullptr, error);
  if (!prototype) return 1;
  auto camera=physics::createBody(*prototype,{0,0,0});
  auto target=physics::createBody(*prototype,{10,0,0});
  camera.state[0].vel={0,1,0}; target.state[0].vel={1,0,0};
  physics::World world; world.params.gravityEnabled=false;
  world.bodies={&camera,&target};
  CameraFollowSmoothing smooth;
  CameraFollowStep cb; cb.camera=&camera; cb.look=&target; cb.move=&target; cb.smoothing=&smooth;
  cb.input.otherBranchesInactive=true; cb.input.maxForce=3;
  cb.input.command.torque=1; cb.input.command.force=2; cb.input.command.damping=1;
  cb.input.command.perpendicularDamping=-0.5f; cb.input.command.moveDistance=2;
  const auto original=camera.state[0];
  if (stepCameraFollowWorld(world,0.02f,{cb},false,error) || world.time!=0 ||
      camera.state[0].pos!=original.pos || camera.state[0].force!=original.force ||
      !smooth.initializeLook || !smooth.initializeMove) return 2;
  cb.input.otherBranchesInactive=false;
  if (stepCameraFollowWorld(world,0.02f,{cb},true,error) || world.time!=0 ||
      camera.state[0].pos!=original.pos || !smooth.initializeLook) return 3;
  cb.input.otherBranchesInactive=true;
  target.flags |= physics::kBodyHasChildren;
  if (stepCameraFollowWorld(world,0.02f,{cb},true,error) || world.time!=0 ||
      camera.state[0].force!=original.force || !smooth.initializeMove) return 4;
  target.flags &= ~physics::kBodyHasChildren;
  TimerState clock; LinkOwner owner; owner.value=1;
  if (!clock.add(&owner,0.01f,17,error)) return 5;
  std::string order;
  const auto physicsStep=[&](float dt,std::string &err) {
    order+='p'; return stepCameraFollowWorld(world,dt,{cb},true,err);
  };
  const auto event=[&](LinkOwner &,int32_t id,std::string &) {
    order+='e'; return id==17 && !smooth.initializeLook && world.time>0 &&
        camera.state[camera.current].vel[0]>0;
  };
  const auto drain=[&](std::string &) { order+='d'; return true; };
  if (!clock.advanceFrame(0.02f,1,1,0.03f,physicsStep,event,drain,error) ||
      order!="ped" || std::fabs(clock.now()-world.time)>1e-6f) return 6;
  const auto first=smooth.move;
  if (!stepCameraFollowWorld(world,0.02f,{cb},true,error) || smooth.move==first) return 7;

  GameWorld game;
  auto *root = game.addRoot();
  auto *host = root ? game.addChild(*root, "camera host", 0x21) : nullptr;
  if (!host) return 8;
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
  auto cameraPrototype = physics::createBodyProto(0.05f, 1, 0.15f, 0, 0, nullptr, error);
  auto targetPrototype = physics::createBodyProto(1, 1, 1, 0, 0, nullptr, error);
  if (!cameraPrototype || !targetPrototype) return 8;
  auto cameraProto = std::make_shared<physics::BodyProto>(std::move(*cameraPrototype));
  auto targetProto = std::make_shared<physics::BodyProto>(std::move(*targetPrototype));
  auto cameraBody = std::make_shared<physics::Body>(physics::createBody(*cameraProto, {0, 0, 0}));
  cameraBody->flags |= 0x4002;
  auto targetBody = std::make_shared<physics::Body>(physics::createBody(*targetProto, {10, 0, 0}));
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
      error.find("dynamic bodies") == std::string::npos || !(speed > 0.0f) ||
      game.physicsTime() <= 0.0f)
    return 9;

  GameWorld alongside;
  auto *alongsideRoot = alongside.addRoot();
  auto *alongsideHost = alongsideRoot ? alongside.addChild(*alongsideRoot, "camera host", 0x21) : nullptr;
  if (!alongsideHost) return 10;
  alongsideHost->nativeDevice = "camera";
  alongsideHost->cameraDevice = std::make_shared<CameraDeviceResources>(alongside);
  auto &alongsideDevice = *alongsideHost->cameraDevice;
  alongsideDevice.fieldOfView = 55;
  alongsideDevice.maxForce = 20;
  alongsideDevice.commands.torque = 1;
  alongsideDevice.commands.force = 2;
  alongsideDevice.commands.damping = 1;
  alongsideDevice.commands.perpendicularDamping = -0.5f;
  alongsideDevice.commands.moveDistance = 2;
  auto alongsideCameraProto = std::make_shared<physics::BodyProto>(*cameraProto);
  auto alongsideTargetProto = std::make_shared<physics::BodyProto>(*targetProto);
  auto alongsideCamera = std::make_shared<physics::Body>(physics::createBody(*alongsideCameraProto, {0, 0, 0}));
  alongsideCamera->flags |= 0x4002;
  auto alongsideTarget = std::make_shared<physics::Body>(physics::createBody(*alongsideTargetProto, {10, 0, 0}));
  alongsideTarget->flags |= physics::kBodyHasChildren;
  const auto targetPosition = alongsideTarget->state[alongsideTarget->current].pos;
  auto *alongsidePhysics = alongside.addTypedChild(*alongsideRoot, 2, "camera body", 0);
  auto *alongsideCar = alongside.addTypedChild(*alongsideRoot, 2, "car root", 0);
  if (!alongsidePhysics || !alongsideCar ||
      !alongside.registerDynamicBody(*alongsidePhysics, alongsideCamera, alongsideCameraProto) ||
      !alongside.registerDynamicBody(*alongsideCar, alongsideTarget, alongsideTargetProto))
    return 10;
  setLinkOwner(alongsideDevice.physics, alongsidePhysics);
  setLinkOwner(alongsideDevice.lookTarget, alongsideCar);
  setLinkOwner(alongsideDevice.moveTarget, alongsideCar);
  const bool applied = alongside.stepArmedCameraFollow(0.02f, error);
  const auto &cameraState = alongsideCamera->state[alongsideCamera->current];
  const auto &carState = alongsideTarget->state[alongsideTarget->current];
  if (!applied || !(cameraState.vel[0] > 0.0f) || carState.pos != targetPosition ||
      alongside.dynamicBody(*alongsideCar).expired() || alongside.dynamicBodyCount() != 2)
    return 11;

  alongsideTarget->attachment = physics::BodyAttachment{};
  const auto held = cameraState.vel;
  if (alongside.stepArmedCameraFollow(0.02f, error) ||
      alongsideCamera->state[alongsideCamera->current].vel != held ||
      alongside.dynamicBody(*alongsideCar).expired())
    return 12;
  return 0;
}
