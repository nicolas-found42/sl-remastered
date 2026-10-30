#include "ground_files.hpp"
#include "ground_runtime.hpp"
#include "road_other_initial.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"
#include "scx.hpp"

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

int main() {
  SceneRenderInstance pendingRender(nullptr);
  assert(!pendingRender.groundBone());
  pendingRender.prepareUnattachedGroundBone();
  assert(pendingRender.groundBone() && pendingRender.groundBone()->name == "bone00" &&
         pendingRender.groundBone()->id == 0x8000);
  assert((pendingRender.groundBone()->matrix == std::array<float, 16>{
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}));
  pendingRender.prepareUnattachedGroundBone();
  assert(pendingRender.groundBone()->id == 0x8000);
  PackRegistry registry("ref/raw");
  GameWorld world;
  assert(world.addRoot());
  ResourceWorld resources(world, registry);
  uint16_t slot = 0;
  std::string error;
  assert(registry.registerPackName("maps\\city.rpk", slot, error));
  auto found = resources.lookup((uint32_t(slot) << 16) | 1, 1);
  assert(found.node.packNode);
  GameResourcePayload payload;
  assert(resources.inspectGamePayload(*found.node.packNode, payload, error));
  assert(payload.parameters);
  GameResourcePayload preparedPayload;
  assert(resources.prepareGamePayload(*found.node.packNode, preparedPayload, error));
  auto cityMaterial = sceneLoadMaterial(resources, (uint32_t(slot) << 16) | 0x10);
  assert(cityMaterial.material);
  ScxModel cityMesh;
  assert(!sceneLoadMesh(resources, cityMaterial.material->meshId(), registry.root(), cityMesh, error));
  assert(sceneLoadGroundMesh(resources, cityMaterial.material->meshId(), registry.root(), cityMesh, error));
  assert(cityMesh.version == 3 && cityMesh.materials.size() == 108);
  for (const auto& mesh : cityMesh.meshes)
    assert(mesh.vertexCount == 0 && mesh.indices.empty());
  GroundParameters parameters;
  assert(parseGroundParameters(*payload.parameters, parameters, error));
  GroundFiles files;
  assert(prepareGroundFiles(registry.root(), parameters, 1.0f, files, error));
  assert(files.materials && files.materials->materials.size() == 108);
  assert(files.materials->materials.front().name == "RBegin1");
  assert(files.roads && !files.roads->nodes[0].empty());
  auto prepared = prepareGround(*payload.parameters, 9.81f, 0.0f, 4.0f, error);
  assert(prepared && !prepared->files.materials && !prepared->files.roads);
  assert(prepared->gravity == 9.81f);
  assert(bindGroundDefaultMaterials(resources, *prepared->physics, error));
  uint32_t particleA = 0, particleB = 0, sound = 0;
  assert(registry.resolveLiteral("particles.rpk", 2, particleA, error));
  assert(registry.resolveLiteral("particles.rpk", 10, particleB, error));
  assert(registry.resolveLiteral("sound.rpk", 6, sound, error));
  assert(prepared->physics->particleMaterialA.cachedValue == particleA &&
         prepared->physics->particleMaterialB.cachedValue == particleB &&
         prepared->physics->soundMaterial.cachedValue == sound);
  assert(bindGroundDefaultMaterials(resources, *prepared->physics, error));
  {
    LinkOwner* materialOwner = nullptr;
    const OwnerLink* materialLink = nullptr;
    auto contains = [](const LinkOwner& owner, const OwnerLink* target) {
      for (auto* link = owner.head; link; link = link->next)
        if (link == target) return true;
      return false;
    };
    {
    GroundPreparation fallback;
    assert(bindGroundFallbackMaterial(resources, fallback, error));
    uint32_t fallbackMaterial = 0, fallbackSound = 0;
    assert(registry.resolveLiteral("system.rpk", 3, fallbackMaterial, error));
    assert(registry.resolveLiteral("system.rpk", 9, fallbackSound, error));
    assert(fallback.fallbackMaterial && fallback.fallbackMaterial->field44 == 1.0f &&
           fallback.fallbackMaterial->material.cachedValue == fallbackMaterial &&
           fallback.fallbackMaterial->sound.cachedValue == fallbackSound);
    materialOwner = fallback.fallbackMaterial->material.owner;
    materialLink = &fallback.fallbackMaterial->material;
    assert(materialOwner && contains(*materialOwner, materialLink));
    }
    assert(materialOwner && !contains(*materialOwner, materialLink));
  }
  {
    GroundPreparation fallback;
    assert(bindGroundFallbackMaterial(resources, fallback, error));
    GameWorld otherWorld;
    assert(otherWorld.addRoot());
    ResourceWorld otherResources(otherWorld, registry);
    assert(!bindGroundFallbackMaterial(otherResources, fallback, error));
    assert(fallback.fallbackMaterial->material.owner);
    setLinkOwner(fallback.fallbackMaterial->material, nullptr);
    assert(!bindGroundFallbackMaterial(resources, fallback, error));
    assert(!fallback.fallbackMaterial->material.owner);
  }
  {
    PackRegistry missingRegistry("/nonexistent-slrr-test-root");
    GameWorld missingWorld;
    assert(missingWorld.addRoot());
    ResourceWorld missingResources(missingWorld, missingRegistry);
    auto missingBody = createGroundRuntime({}, 1.0f);
    assert(missingBody && !bindGroundDefaultMaterials(missingResources, *missingBody, error));
    assert(!missingBody->particleMaterialA.owner && !missingBody->particleMaterialB.owner &&
           !missingBody->soundMaterial.owner);
    GroundPreparation fallback;
    assert(!bindGroundFallbackMaterial(missingResources, fallback, error) && !fallback.fallbackMaterial);
  }
  {
    char pattern[] = "/tmp/slrr-ground-wrong-type-XXXXXX";
    const char* dir = mkdtemp(pattern);
    assert(dir);
    std::vector<uint8_t> bytes{'R', 'P', 'A', 'K'};
    auto put32 = [&](uint32_t value) {
      for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
    };
    put32(0x200); put32(0); put32(0);
    put32(46); put32(2); put32(0); put32(0);
    for (uint32_t id : {3u, 9u}) {
      put32(0); put32(id); bytes.push_back(6); bytes.push_back(0);
      put32(0); put32(0); put32(0); bytes.push_back(0);
    }
    {
      std::ofstream file(std::filesystem::path(dir) / "system.rpk", std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      assert(file);
    }
    {
      PackRegistry wrongRegistry(dir);
      GameWorld wrongWorld;
      assert(wrongWorld.addRoot());
      ResourceWorld wrongResources(wrongWorld, wrongRegistry);
      GroundPreparation fallback;
      assert(!bindGroundFallbackMaterial(wrongResources, fallback, error));
      assert(!fallback.fallbackMaterial);
    }
    std::filesystem::remove_all(dir);
  }
  assert(prepareGroundMaterialFile(registry.root(), prepared->parameters,
      prepared->physics->prototype.shape.friction, prepared->files.materials, error));
  assert(prepared->files.materials && prepared->files.materials->materials.size() == 108);
  assert(prepareGroundRoadFile(registry.root(), prepared->parameters, prepared->files.roads, error));
  assert(prepared->files.roads);
  assert(prepared->bindCityOwner(*found.node.packNode));
  assert(prepared->cityOwner.owner == found.node.owner);
  assert(prepared->bindRoadOwner(*found.node.packNode));
  assert(prepared->roadOwner.owner == found.node.owner);
  assert(prepared->initializeRoad(12.5f));
  assert(prepared->roadInitial && prepared->roadInitial->time == 12.5f &&
         !prepared->roadInitial->pathMask.empty() && prepared->roadInitial->pathMask[0] == 0);
  RoadOtherRecord otherFixture;
  assert(!roadOtherInitialState(otherFixture, 12.5f));
  otherFixture.segments.resize(2);
  float segmentLength = 1.5f;
  std::memcpy(&otherFixture.segments[0].words[12], &segmentLength, sizeof(segmentLength));
  segmentLength = 2.25f;
  std::memcpy(&otherFixture.segments[1].words[12], &segmentLength, sizeof(segmentLength));
  const auto otherState = roadOtherInitialState(otherFixture, 12.5f);
  assert(otherState && otherState->curveLength == 3.75f && otherState->time == 12.5f &&
         otherState->fieldC4 == 0);
  assert(!prepared->files.roads->others.empty());
  assert(roadOtherInitialState(prepared->files.roads->others[0], 12.5f));
  assert(prepared->firstCrossings.size() == prepared->files.roads->nodes[0].size());
  assert(!prepared->firstCrossings.empty() && prepared->firstCrossings[0].lanePairs.size() == 3 &&
         prepared->firstCrossings[0].lowerBucket == 1 && prepared->firstCrossings[0].upperBound == 20);
  assert(prepared->secondCrossings.size() == prepared->files.roads->nodes[1].size());
  assert(!prepared->secondCrossings.empty() && prepared->secondCrossings[0].time == 12.5f &&
         prepared->secondCrossings[0].groupCount == 3);
  assert(prepared->pathInitial &&
         prepared->pathInitial->pathValues.size() == prepared->files.roads->paths.size());
  assert(prepared->otherInitial.size() == prepared->files.roads->others.size());
  assert(!prepared->otherInitial.empty() && prepared->otherInitial[0].time == 12.5f &&
         prepared->otherInitial[0].fieldC4 == 0);
  assert(prepared->pathTopology.size() == prepared->files.roads->paths.size());
  assert(!prepared->pathTopology.empty() && prepared->pathTopology[0].adjustedCount == 2 &&
         (prepared->pathTopology[0].others == std::vector<size_t>{0, 1}));
  assert(!prepared->initializeRoad(13.0f) && prepared->roadInitial->time == 12.5f);
  LinkOwner otherOwner;
  GroundPreparation missingRoad;
  assert(!missingRoad.bindRoadOwner(otherOwner) && !otherOwner.head);
  assert(!missingRoad.initializeRoad(0));
  assert(!prepared->bindCityOwner(otherOwner));
  assert(prepared->cityOwner.owner == found.node.owner);
  assert(!prepared->bindRoadOwner(otherOwner));
  assert(prepared->roadOwner.owner == found.node.owner);
  GroundPreparation invalidRoad;
  invalidRoad.files.roads.emplace();
  invalidRoad.files.roads->nodes[0].resize(1);
  assert(invalidRoad.bindRoadOwner(otherOwner));
  assert(!invalidRoad.initializeRoad(1.0f) && !invalidRoad.roadInitial &&
         invalidRoad.firstCrossings.empty() && invalidRoad.secondCrossings.empty() &&
         invalidRoad.pathTopology.empty() && !invalidRoad.pathInitial && invalidRoad.otherInitial.empty());
  invalidRoad.files.roads->nodes[0][0].lanes.resize(1);
  invalidRoad.files.roads->nodes[1].resize(1);
  assert(!invalidRoad.initializeRoad(1.0f) && !invalidRoad.roadInitial &&
         invalidRoad.firstCrossings.empty() && invalidRoad.secondCrossings.empty() &&
         invalidRoad.pathTopology.empty() && !invalidRoad.pathInitial && invalidRoad.otherInitial.empty());
  invalidRoad.files.roads->nodes[1].clear();
  invalidRoad.files.roads->paths.resize(1);
  invalidRoad.files.roads->paths[0].refs[0] = 1;
  assert(!invalidRoad.initializeRoad(1.0f) && !invalidRoad.roadInitial &&
         invalidRoad.firstCrossings.empty() && invalidRoad.pathTopology.empty() && !invalidRoad.pathInitial && invalidRoad.otherInitial.empty());
  assert(prepared->physics->prototype.shape.friction == 1.0f);
  assert(prepared->physics->body.proto == &prepared->physics->prototype);
  assert(prepared->physics->body.state[0].time == 4.0f);
  auto bodyOwner = prepared->physics;
  std::weak_ptr<GroundRuntime> bodyLifetime = bodyOwner;
  prepared.reset();
  assert(!bodyLifetime.expired() && bodyOwner->body.proto == &bodyOwner->prototype);
  bodyOwner.reset();
  assert(bodyLifetime.expired());
  assert(!prepareGround("invalid", 9.81f, 1.0f, 0.0f, error));
  auto retry = prepareGround(*payload.parameters, 4.0f, 0.6f, 8.0f, error);
  assert(retry && retry->gravity == 4.0f);
  assert(retry->physics->prototype.shape.friction == 0.6f);
  assert(retry->physics->body.state[0].time == 8.0f);
  parameters.materialPath = "missing.scm";
  parameters.roadPath.clear();
  assert(prepareGroundFiles(registry.root(), parameters, 1.0f, files, error));
  assert(!files.materials && !files.roads);
  parameters.materialPath = "../outside.scm";
  assert(!prepareGroundFiles(registry.root(), parameters, 1.0f, files, error));
  assert(!files.materials && !files.roads);
  parameters.materialPath.clear();
  parameters.roadPath = "maps\\city\\meshes\\city.scm";
  assert(!prepareGroundFiles(registry.root(), parameters, 1.0f, files, error));
  assert(!files.materials && !files.roads);
}
