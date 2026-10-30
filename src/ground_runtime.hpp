#pragma once

#include "physics_body.hpp"
#include "ground_files.hpp"
#include "owner_links.hpp"
#include "road_other_initial.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Owned physics portion of the ground factory. It does not register a world
// object or provide collision geometry.
struct GroundRuntime {
  ~GroundRuntime() {
    setLinkOwner(particleMaterialA, nullptr);
    setLinkOwner(particleMaterialB, nullptr);
    setLinkOwner(soundMaterial, nullptr);
  }
  GroundRuntime(const GroundRuntime&) = delete;
  GroundRuntime& operator=(const GroundRuntime&) = delete;

  physics::BodyProto prototype;
  physics::Body body;
  float materialField54 = 0.01f;
  OwnerLink particleMaterialA;
  OwnerLink particleMaterialB;
  OwnerLink soundMaterial;
  bool defaultMaterialsBound = false;

private:
  GroundRuntime() = default;
  friend std::unique_ptr<GroundRuntime> createGroundRuntime(
      const physics::Vec3&, float, float);
};

// 00463220 uses ground_friction unless it is <= 0, then uses 1.
std::unique_ptr<GroundRuntime> createGroundRuntime(
    const physics::Vec3& position, float groundFriction, float creationTime = 0);

struct ScxModel;
class ResourceWorld;
bool bindGroundDefaultMaterials(ResourceWorld& resources, GroundRuntime& ground,
                                std::string& error);
bool sceneLoadGroundMesh(ResourceWorld& world, uint32_t id,
                         const std::filesystem::path& root, ScxModel& model,
                         std::string& error);
class SceneRenderInstance;
class SceneMaterialResources;
// Prepared inputs only. This is not a registered ground or a factory result.
struct GroundFallbackMaterial {
  ~GroundFallbackMaterial() {
    setLinkOwner(material, nullptr);
    setLinkOwner(sound, nullptr);
  }
  OwnerLink material;
  OwnerLink sound;
  float field44 = 1.0f;
};

struct GroundPreparation {
  ~GroundPreparation() {
    setLinkOwner(roadOwner, nullptr);
    setLinkOwner(cityOwner, nullptr);
    setLinkOwner(renderResource, nullptr);
    setLinkOwner(bodyResource, nullptr);
  }
  GroundParameters parameters;
  float gravity = 0;
  GroundFiles files;
  std::unique_ptr<GroundFallbackMaterial> fallbackMaterial;
  std::shared_ptr<GroundRuntime> physics;
  OwnerLink bodyResource;
  OwnerLink renderResource;
  OwnerLink roadOwner;
  OwnerLink cityOwner;
  bool bindCityOwner(LinkOwner& owner);
  bool bindRoadOwner(LinkOwner& owner);
  bool initializeRoad(float time);
  std::optional<RoadInitialState> roadInitial;
  std::vector<RoadCrossingInitialState> firstCrossings;
  std::vector<RoadSecondCrossingInitialState> secondCrossings;
  std::vector<RoadPathTopology> pathTopology;
  std::optional<RoadPathInitialState> pathInitial;
  std::vector<RoadOtherInitialState> otherInitial;
  std::shared_ptr<SceneRenderInstance> pendingRender;
  std::shared_ptr<SceneMaterialResources> pendingMaterial;
  std::shared_ptr<ScxModel> pendingMesh;
};

// 00491d50 selects the feature word as an index in the other body's material
// array. Caller must establish that this ground owns the contacted body. The
// base-prototype fallback and contacts without a feature remain unsupported.
std::optional<GroundMaterial> groundContactMaterial(
    const GroundPreparation &ground, uint32_t materialWord, std::string &error);

class GameWorld;
// One car 00484050 keeps after 0057b420 accepts a placement. The three floats
// are the native arguments, stored on the stock vehicle at +0x14, +0x20, and
// +0x18. templateId is the GameRef argument's resource id.
struct TrafficCar {
  uint32_t templateId = 0;
  float along = 0;
  float across = 0;
  float gap = 0;
};

// Object 00463220 returns and 0053e860 stores on the city. It owns the
// prepared inputs. roadWorld is the pointer 00580cc0 writes at road+0x84.
// addTrafficN reads the road through getInfo(0x39), which returns factory+0x88.
// The 00588080 object at +0x8c is a different slot (getInfo 0x3d) and is not
// read by 00484050.
struct GroundFactoryResult {
  ~GroundFactoryResult() {
    for (auto &observer : observers) setLinkOwner(*observer, nullptr);
    setLinkOwner(city, nullptr);
  }
  GroundFactoryResult(const GroundFactoryResult &) = delete;
  GroundFactoryResult &operator=(const GroundFactoryResult &) = delete;
  GroundFactoryResult() = default;

  std::shared_ptr<GroundPreparation> preparation;
  GameWorld *roadWorld = nullptr;
  OwnerLink city;
  std::vector<std::unique_ptr<OwnerLink>> observers;
  std::vector<TrafficCar> traffic;
  // 00589b80 stores these when getInfo(0x3d) is non-null (factory+0x8c). Stock
  // reads them from 00620070 and 00620074 when spawning pedestrians.
  float pedestrianDensity = 0.f;
  float pedestrianDensityHi = 0.f;
  // Set only when setPedestrianDensityN stores. A zero density is a real
  // write, so the snapshot cannot treat 0 as "never set".
  bool pedestrianDensitySet = false;
  // 00589940 keeps up to 32 type ids at pedestrian-world+0x33c8, keyed by
  // GameRef+8. The city-owned factory is the port's registry for those ids.
  std::vector<uint32_t> pedestrianTypes;
  // 004867b0 / FUN_0049b440 on the global water object: position +0x1b8,
  // normalized direction +0x1c4, extents +0x1e4/+0x1e8, enable flag +0x80.
  bool waterActive = false;
  float waterExtentA = 0.f;
  float waterExtentB = 0.f;
  float waterPosX = 0.f;
  float waterPosY = 0.f;
  float waterPosZ = 0.f;
  float waterDirX = 0.f;
  float waterDirY = 0.f;
  float waterDirZ = 0.f;
  // 00486920 / FUN_0049b530: pairs of Vector3 appended on the global water
  // object (+0x1d0/+0x1d4). Each entry is 0x18 bytes (two xyz triples).
  struct WaterLimit {
    float ax = 0.f;
    float ay = 0.f;
    float az = 0.f;
    float bx = 0.f;
    float by = 0.f;
    float bz = 0.f;
  };
  std::vector<WaterLimit> waterLimits;
  // 00486a20: fog packet 0x4a with int color and near/far scaled by _DAT_005e7334
  // (10.0 in stock).
  struct FogSetting {
    int32_t color = 0;
    float nearDist = 0.f;
    float farDist = 0.f;
  };
  std::optional<FogSetting> fogSetting;
};

// True when stock would return non-null for GroundRef getInfo(0x3d).
bool groundRoadTablePublished(const GroundFactoryResult &result);
bool groundPedestrianSlotReady(const GroundFactoryResult &result);
void setGroundPedestrianDensity(GroundFactoryResult &result, float density);
void addGroundPedestrianType(GroundFactoryResult &result, uint32_t templateId);
void setGroundWater(GroundFactoryResult &result, float posX, float posY, float posZ,
                    float dirX, float dirY, float dirZ, float extentA, float extentB);
void addGroundWaterLimit(GroundFactoryResult &result, float ax, float ay, float az, float bx,
                         float by, float bz);
void addGroundFog(GroundFactoryResult &result, int32_t color, float nearDist, float farDist);

// 00484050's return: how many placements were kept. Zero when count is not
// positive or the road table 00580910 publishes is absent. Lane-capacity
// rejection inside 0057b420 is not modeled, so a published table keeps every
// attempt.
int placeGroundTraffic(GroundFactoryResult &result, uint32_t templateId,
                       int32_t count, float along, float across, float gap);

std::shared_ptr<GroundFactoryResult> finishGroundFactory(
    std::shared_ptr<GroundPreparation> preparation, GameWorld &world,
    std::string &error);

bool bindGroundFallbackMaterial(ResourceWorld& resources, GroundPreparation& ground,
                                std::string& error);
std::unique_ptr<GroundPreparation> prepareGround(
    std::string_view arguments, float gravity, float groundFriction, float creationTime, std::string& error);
