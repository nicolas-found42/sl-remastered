#pragma once

#include "camera_device_args.hpp"
#include "camera_device_command.hpp"
#include "camera_device_config.hpp"
#include "camera_follow_force.hpp"
#include "owner_links.hpp"
#include "physics_body.hpp"
#include <memory>
#include <string>

class GameWorld;
class ResourceWorld;
class Render2D;
struct WorldNode;
struct GroundFactoryResult;
struct CameraResource;
struct CarDriver;
struct CarCameraSettings;

void applyCameraSectorEnvironment(CameraResource &camera,
                                  const GroundFactoryResult &ground,
                                  float farDistance);

// The links are detached by GameWorld teardown, so the owner may outlive it.
class CameraDeviceResources {
public:
  explicit CameraDeviceResources(GameWorld &world) : world_(&world) {}
  ~CameraDeviceResources();
  CameraDeviceResources(const CameraDeviceResources &) = delete;
  CameraDeviceResources &operator=(const CameraDeviceResources &) = delete;
  WorldNode *physicsNode() const;
  WorldNode *cameraNode() const;
  WorldNode *renderNode() const;
  bool attachViewport(ResourceWorld &resources, Render2D &renderer,
                      WorldNode &viewport, int32_t priority, float farDistance,
                      std::string &error);
  bool detachViewport(WorldNode &viewport, std::string &error);
  bool applySeatFollow(WorldNode &car, const CarDriver &driver,
                       const CarCameraSettings &settings);
  CameraCommandState commands{};
  CameraFollowSmoothing followSmoothing{};
  float fieldOfView = 0;
  float maxForce = 1;
  bool audioListenerEnabled = false; // constructor flags & 2, 00437ea0
  OwnerLink physics, camera, render, controller, lookTarget, moveTarget;

private:
  GameWorld *world_;
};

std::unique_ptr<CameraDeviceResources> prepareCameraDeviceResources(
    GameWorld &world, ResourceWorld &resources, WorldNode &gameNode,
    const CameraDeviceConfig &config, const CameraDeviceArgs &args,
    std::shared_ptr<const physics::BodyProto> prototype, std::string &error);
