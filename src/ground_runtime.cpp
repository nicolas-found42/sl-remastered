#include "ground_runtime.hpp"

#include <cmath>
#include <limits>
#include "resource_world.hpp"
#include "world_model.hpp"

#include <utility>

std::unique_ptr<GroundRuntime> createGroundRuntime(
    const physics::Vec3& position, float groundFriction, float creationTime) {
  auto ground = std::unique_ptr<GroundRuntime>(new GroundRuntime);
  std::string error;
  auto prototype = physics::createBodyProto(0, 0, 0, 0, 0, nullptr, error);
  if (!prototype) return nullptr;
  ground->prototype = std::move(*prototype);
  ground->prototype.flags |= physics::kProtoStatic;
  ground->prototype.invMass = 0;
  ground->prototype.shape.friction = groundFriction <= 0 ? 1.0f : groundFriction;
  ground->prototype.shape.bounce = 0.2f;
  ground->body = physics::createBody(ground->prototype, position, nullptr, creationTime);
  return ground;
}

std::unique_ptr<GroundPreparation> prepareGround(
    std::string_view arguments, float gravity, float groundFriction, float creationTime, std::string& error) {
  auto result = std::make_unique<GroundPreparation>();
  result->gravity = gravity;
  if (!parseGroundParameters(arguments, result->parameters, error)) return nullptr;
  result->physics = createGroundRuntime(result->parameters.position, groundFriction, creationTime);
  if (!result->physics) {
    error = "ground physics preparation failed";
    return nullptr;
  }
  error.clear();
  return result;
}

std::optional<GroundMaterial> groundContactMaterial(
    const GroundPreparation &ground, uint32_t materialWord, std::string &error) {
  if (!ground.physics || !ground.files.materials) {
    error = "ground contact material table unavailable";
    return std::nullopt;
  }
  const auto &materials = ground.files.materials->materials;
  if (materialWord > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
      materialWord >= materials.size()) {
    error = "ground contact base-prototype fallback unsupported";
    return std::nullopt;
  }
  error.clear();
  return materials[materialWord];
}

bool GroundPreparation::bindRoadOwner(LinkOwner& owner) {
  if (!files.roads || (roadOwner.owner && roadOwner.owner != &owner)) return false;
  setLinkOwner(roadOwner, &owner);
  return true;
}

bool GroundPreparation::bindCityOwner(LinkOwner& owner) {
  if (cityOwner.owner && cityOwner.owner != &owner) return false;
  setLinkOwner(cityOwner, &owner);
  return true;
}

bool GroundPreparation::initializeRoad(float time) {
  if (!roadOwner.owner || !files.roads || roadInitial) return false;
  std::vector<RoadCrossingInitialState> crossings;
  crossings.reserve(files.roads->nodes[0].size());
  for (size_t index = 0; index < files.roads->nodes[0].size(); ++index) {
    auto crossing = roadFirstCrossingInitialState(*files.roads, index);
    if (!crossing) return false;
    crossings.push_back(std::move(*crossing));
  }
  std::vector<RoadSecondCrossingInitialState> second;
  second.reserve(files.roads->nodes[1].size());
  for (size_t index = 0; index < files.roads->nodes[1].size(); ++index) {
    auto crossing = roadSecondCrossingInitialState(*files.roads, index, time);
    if (!crossing) return false;
    second.push_back(std::move(*crossing));
  }
  std::vector<RoadPathTopology> paths;
  paths.reserve(files.roads->paths.size());
  for (size_t index = 0; index < files.roads->paths.size(); ++index) {
    auto path = roadPathTopology(*files.roads, index);
    if (!path) return false;
    paths.push_back(std::move(*path));
  }
  auto scaledPaths = roadPathInitialState(*files.roads);
  if (!scaledPaths) return false;
  std::vector<RoadOtherInitialState> others;
  others.reserve(files.roads->others.size());
  for (const auto& record : files.roads->others) {
    auto other = roadOtherInitialState(record, time);
    if (!other) return false;
    others.push_back(*other);
  }
  auto initial = roadInitialState(*files.roads, time);
  if (!initial) return false;
  pathInitial = std::move(scaledPaths);
  otherInitial = std::move(others);
  firstCrossings = std::move(crossings);
  secondCrossings = std::move(second);
  pathTopology = std::move(paths);
  roadInitial = std::move(initial);
  return true;
}

std::shared_ptr<GroundFactoryResult> finishGroundFactory(
    std::shared_ptr<GroundPreparation> preparation, GameWorld &world,
    std::string &error) {
  if (!preparation || !preparation->physics || !preparation->cityOwner.owner) {
    error = "ground factory inputs missing";
    return nullptr;
  }
  if (preparation->files.roads) {
    const auto &initial = preparation->roadInitial;
    if (!initial || initial->pathMask.empty() || initial->pathMask.front() != 0 ||
        preparation->firstCrossings.size() != preparation->files.roads->nodes[0].size() ||
        preparation->secondCrossings.size() != preparation->files.roads->nodes[1].size() ||
        preparation->pathTopology.size() != preparation->files.roads->paths.size() ||
        preparation->otherInitial.size() != preparation->files.roads->others.size() ||
        !preparation->pathInitial) {
      error = "ground road runtime update missing";
      return nullptr;
    }
  }
  auto result = std::make_shared<GroundFactoryResult>();
  result->roadWorld = preparation->files.roads ? &world : nullptr;
  result->preparation = std::move(preparation);
  error.clear();
  return result;
}

bool bindGroundDefaultMaterials(ResourceWorld& resources, GroundRuntime& ground,
                                std::string& error) {
  auto& packs = resources.packs();
  uint32_t id = 0;
  if (!packs.resolveLiteral("particles.rpk", 2, id, error)) return false;
  const auto a = resources.lookup(id, 14);
  if (a.status != LookupStatus::Found || !a.node.owner) {
    error = "ground first particle material unavailable";
    return false;
  }
  if (!packs.resolveLiteral("particles.rpk", 10, id, error)) return false;
  const auto b = resources.lookup(id, 14);
  if (b.status != LookupStatus::Found || !b.node.owner) {
    error = "ground second particle material unavailable";
    return false;
  }
  if (!packs.resolveLiteral("sound.rpk", 6, id, error)) return false;
  const auto c = resources.lookup(id, 6);
  if (c.status != LookupStatus::Found || !c.node.owner) {
    error = "ground sound material unavailable";
    return false;
  }
  if (ground.defaultMaterialsBound) {
    if (ground.particleMaterialA.owner != a.node.owner ||
        ground.particleMaterialB.owner != b.node.owner ||
        ground.soundMaterial.owner != c.node.owner) {
      error = "ground default material resource identity changed";
      return false;
    }
  } else {
    setLinkOwner(ground.particleMaterialA, a.node.owner);
    setLinkOwner(ground.particleMaterialB, b.node.owner);
    setLinkOwner(ground.soundMaterial, c.node.owner);
    ground.defaultMaterialsBound = true;
  }
  error.clear();
  return true;
}

bool bindGroundFallbackMaterial(ResourceWorld& resources, GroundPreparation& ground,
                                std::string& error) {
  auto& packs = resources.packs();
  uint32_t id = 0;
  if (!packs.resolveLiteral("system.rpk", 3, id, error)) return false;
  const auto materialNode = resources.lookup(id, 14);
  if (materialNode.status != LookupStatus::Found || !materialNode.node.owner) {
    error = "ground fallback render material unavailable";
    return false;
  }
  if (!packs.resolveLiteral("system.rpk", 9, id, error)) return false;
  const auto soundNode = resources.lookup(id, 6);
  if (soundNode.status != LookupStatus::Found || !soundNode.node.owner) {
    error = "ground fallback sound material unavailable";
    return false;
  }
  if (ground.fallbackMaterial) {
    if (ground.fallbackMaterial->material.owner != materialNode.node.owner ||
        ground.fallbackMaterial->sound.owner != soundNode.node.owner) {
      error = "ground fallback material resource identity changed";
      return false;
    }
  } else {
    auto fallback = std::make_unique<GroundFallbackMaterial>();
    setLinkOwner(fallback->material, materialNode.node.owner);
    setLinkOwner(fallback->sound, soundNode.node.owner);
    ground.fallbackMaterial = std::move(fallback);
  }
  error.clear();
  return true;
}

bool groundRoadTablePublished(const GroundFactoryResult &result) {
  if (!result.roadWorld || !result.preparation || !result.preparation->files.roads)
    return false;
  const auto &roads = *result.preparation->files.roads;
  return !roads.nodes[0].empty() || !roads.nodes[1].empty() || !roads.paths.empty() ||
         !roads.others.empty();
}

int placeGroundTraffic(GroundFactoryResult &result, uint32_t templateId, int32_t count,
                       float along, float across, float gap) {
  if (count <= 0 || !groundRoadTablePublished(result))
    return 0;
  result.traffic.reserve(result.traffic.size() + static_cast<size_t>(count));
  for (int32_t i = 0; i < count; ++i)
    result.traffic.push_back(TrafficCar{templateId, along, across, gap});
  return count;
}

bool groundPedestrianSlotReady(const GroundFactoryResult &result) {
  return groundRoadTablePublished(result);
}

void setGroundPedestrianDensity(GroundFactoryResult &result, float density) {
  if (!groundPedestrianSlotReady(result))
    return;
  result.pedestrianDensity = density;
  result.pedestrianDensityHi = density * 1.1f;
  result.pedestrianDensitySet = true;
}

void addGroundPedestrianType(GroundFactoryResult &result, uint32_t templateId) {
  if (!groundPedestrianSlotReady(result))
    return;
  if (result.pedestrianTypes.size() >= 32)
    return;
  for (uint32_t existing : result.pedestrianTypes) {
    if (existing == templateId)
      return;
  }
  result.pedestrianTypes.push_back(templateId);
}

void setGroundWater(GroundFactoryResult &result, float posX, float posY, float posZ,
                    float dirX, float dirY, float dirZ, float extentA, float extentB) {
  if (extentA <= 0.f || extentB <= 0.f) {
    result.waterActive = false;
    return;
  }
  const float len =
      std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
  if (len != 0.f) {
    const float inv = 1.f / len;
    dirX *= inv;
    dirY *= inv;
    dirZ *= inv;
  }
  result.waterExtentA = extentA;
  result.waterExtentB = extentB;
  result.waterPosX = posX;
  result.waterPosY = posY;
  result.waterPosZ = posZ;
  result.waterDirX = dirX;
  result.waterDirY = dirY;
  result.waterDirZ = dirZ;
  result.waterActive = true;
}

void addGroundWaterLimit(GroundFactoryResult &result, float ax, float ay, float az, float bx,
                         float by, float bz) {
  result.waterLimits.push_back(GroundFactoryResult::WaterLimit{ax, ay, az, bx, by, bz});
}

namespace {
constexpr float kStockFogDistanceScale = 10.f;
} // namespace

void addGroundFog(GroundFactoryResult &result, int32_t color, float nearDist, float farDist) {
  result.fogSetting = GroundFactoryResult::FogSetting{
      color, nearDist * kStockFogDistanceScale, farDist * kStockFogDistanceScale};
}
