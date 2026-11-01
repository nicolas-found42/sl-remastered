#include "camera_frame.hpp"
#include "camera_traversal.hpp"
#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "pack_registry.hpp"
#include "raster3d.hpp"
#include "render2d.hpp"
#include "resource_id.hpp"
#include "resource_material.hpp"
#include "resource_texture.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"
#include "scx.hpp"
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c);                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main() {
  {
    ScxMaterial material;
    CHECK(!sceneScyMaterialType(material));
    material.scyV3.emplace();
    auto &legacy = *material.scyV3;
    material.flags = 0x1840;
    legacy.textureIndices[0] = 0;
    legacy.textureIndices[4] = 7;
    legacy.specular = {0.9f, 0.9f, 0.9f};
    legacy.shininess = 5.4f;
    CHECK(sceneScyMaterialType(material) == 1u);
    legacy.specularStrength = 1.0f;
    legacy.shininess = 9.0f;
    CHECK(sceneScyMaterialType(material) == 5u);
    legacy.textureIndices[3] = 1;
    CHECK(sceneScyMaterialType(material) == 13u);
    legacy.textureIndices[2] = 2;
    CHECK(sceneScyMaterialType(material) == 9u);
    legacy.textureIndices[0] = legacy.textureIndices[4] = -1;
    CHECK(sceneScyMaterialType(material) == 0x32u);
  }
  {
    // FUN_004be950's blend/test branch, read from the stock executable at
    // 004be950: +0x28 bit 0 or diffuse alpha < 0.99 selects the transparent
    // branch; inside it +0x28 bit 0x800 selects the alpha test (reference 0x38)
    // instead of blending. Bit 0x1000 selects the VCA material type and does
    // not affect blending.
    ScxMaterial material;
    material.scyV3.emplace();
    material.diffuse = {1, 1, 1, 1};
    material.flags = 0x1840;
    CHECK(sceneScyBlendState(material) == SceneScyBlend::Opaque);
    material.flags = 0x1800;
    CHECK(sceneScyBlendState(material) == SceneScyBlend::Opaque);
    material.diffuse[3] = 0.5f;
    CHECK(sceneScyBlendState(material) == SceneScyBlend::AlphaTest);
    material.diffuse[3] = 1.0f;
    material.flags = 0x1000;
    CHECK(sceneScyBlendState(material) == SceneScyBlend::Opaque);
    material.flags = 0x1001;
    CHECK(sceneScyBlendState(material) == SceneScyBlend::AlphaBlend);
    material.flags = 0;
    material.diffuse[3] = 0.0f;
    CHECK(sceneScyBlendState(material) == SceneScyBlend::AlphaBlend);
    material.diffuse[3] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!sceneScyBlendState(material));
    material.diffuse[3] = 1.0f;
    material.scyV3.reset();
    CHECK(!sceneScyBlendState(material));
  }
  {
    const std::array<CameraClipVertex, 3> triangle{{{{0, 0, 0, 1}, {0, 0}},
                                                    {{0, 0, 0, 2}, {1, 0}},
                                                    {{0, 0, 0, 3}, {0, 1}}}};
    const auto bound = scenePerspectiveUvErrorBound(triangle);
    CHECK(bound);
    for (int i = 0; i <= 20; ++i)
      for (int j = 0; j <= 20 - i; ++j) {
        const float weights[] = {i / 20.0f, j / 20.0f, (20 - i - j) / 20.0f};
        for (size_t axis = 0; axis < 2; ++axis) {
          float affine = 0, weighted = 0, denominator = 0;
          for (size_t k = 0; k < 3; ++k) {
            affine += weights[k] * triangle[k].uv[axis];
            weighted +=
                weights[k] * triangle[k].uv[axis] / triangle[k].position[3];
            denominator += weights[k] / triangle[k].position[3];
          }
          CHECK(std::abs(affine - weighted / denominator) <= *bound);
        }
      }
  }
  {
    FrameVisibilityState visibility;
    std::string error;
    CHECK(!visibility.controls(error));
    TufaClass config;
    config.name = "java.util.Config";
    std::vector<VmStaticFields> states{
        {&config,
         {{"object_detail", 0.4f, "F"}, {"object_detail_amp", 2.0f, "F"}}}};
    CHECK(visibility.applyConfig(states, error));
    auto controls = visibility.controls(error);
    CHECK(controls && controls->worldScale == 2.0f &&
          controls->threshold == 0.2f && controls->frustumChecks);
    VmGameState game;
    game.visibility = &visibility;
    VmArena arena;
    arena.gameState = &game;
    CHECK(std::holds_alternative<std::monostate>(
        invokeNative("java.lang.System", "setLdPriority", "(I)V", {int32_t{1}},
                     nullptr, &arena, nullptr)));
    CHECK(visibility.loadFactor() == -1.0f);
    controls = visibility.controls(error);
    CHECK(controls && !controls->frustumChecks && controls->threshold == 0.2f);
    states[0].fields[0].value = -1.0f;
    CHECK(visibility.applyConfig(states, error));
    CHECK(std::holds_alternative<std::monostate>(
        invokeNative("java.lang.System", "setLdPriority", "(I)V", {int32_t{0}},
                     nullptr, &arena, nullptr)));
    controls = visibility.controls(error);
    CHECK(controls && controls->frustumChecks && controls->threshold == 0.2f &&
          visibility.loadFactor() == 0.1f);
  }
  {
    const std::array<float, 16> world{
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1}};
    ResourceBounds cameraBound;
    CHECK(sceneCameraBound(1000, world, cameraBound));
    CHECK(cameraBound.mode == 0 && cameraBound.values[0] == 1 &&
          cameraBound.values[1] == 2 && cameraBound.values[2] == -47 &&
          cameraBound.values[3] == 60 && cameraBound.values[5] == 60);
    CHECK(!sceneCameraBound(-1, world, cameraBound));
  }
  {
    ResourceBounds bound{1, {0, 0, 0, 5, 5, 5}};
    CHECK(sceneBoxRelation(bound, {7, 0, 0}, {1, 1, 1}) == 1);
    CHECK(sceneBoxRelation(bound, {6, 0, 0}, {1, 1, 1}) == 2);
    CHECK(sceneBoxRelation(bound, {0, 0, 0}, {6, 6, 6}) == 4);
    CHECK(sceneBoxRelation(bound, {0, 0, 0}, {1, 1, 1}) == 8);
    bound.mode = 0;
    CHECK(sceneBoxRelation(bound, {0, 0, 0}, {1, 1, 1}) == 8);
    CHECK(sceneBoxRelation(bound, {0, 0, 0}, {6, 6, 6}) == 4);
    CHECK(sceneBoundPlaneRelation(bound, {1, 0, 0, 6}) == 4);
    CHECK(sceneBoundPlaneRelation(bound, {1, 0, 0, -6}) == 1);
    CHECK(sceneBoundPlaneRelation(bound, {1, 0, 0, 5}) == 2);
    bound.mode = 1;
    CHECK(sceneBoundPlaneRelation(bound, {1, 0, 0, 6}) == 4);
    CHECK(sceneBoundPlaneRelation(bound, {1, 0, 0, -6}) == 1);
    CHECK(sceneBoundPlaneRelation(bound, {1, 0, 0, 5}) == 2);
  }
  {
    ResourceBounds bounds;
    auto value = sceneVisibilityInput(bounds, {0, 0, 0}, 1, 0.25f, 1, 1);
    CHECK(value && *value == 1.25f);
    bounds.mode = 0;
    bounds.values = {0, 0, 0, 1, 0, 0};
    value = sceneVisibilityInput(bounds, {10, 0, 0}, 1, 0.25f, 1, 1);
    CHECK(value && std::abs(*value - 0.35f) < 0.00001f);
    bounds.mode = 1;
    bounds.values = {0, 0, 0, 3, 4, 0};
    value = sceneVisibilityInput(bounds, {10, 0, 0}, 1, 0, 1, 1);
    CHECK(value && *value == 0.5f);
    value = sceneVisibilityInput(bounds, {0, 0, 0}, 1, 0, 1, 1);
    CHECK(value && *value == 1);
  }
  {
    SceneVisibilityFields fields;
    CHECK(!sceneUpdateType3Visibility(fields, 0, 0.5f, 0, 1));
    CHECK(sceneUpdateType3Visibility(fields, 1, 0.5f, 0, 1));
    CHECK(fields.field6c == 0.5f && fields.field70 == 0.5f &&
          fields.stamp78 == 1 && fields.stamp7c == 0);
    CHECK(sceneUpdateType3Visibility(fields, 1, 0.25f, 0, 1));
    CHECK(fields.field6c == 0.5f && fields.field70 == 0.25f);
    fields.stamp7c = 1;
    CHECK(sceneUpdateType3Visibility(fields, 1, 2, 0, 1));
    CHECK(fields.field6c == 1 && fields.field70 == 1);
  }
  {
    GameWorld runtime;
    auto *root = runtime.addRoot();
    CHECK(root);
    auto *node = runtime.addTypedChild(*root, 3, "rectangle", 0);
    CHECK(node);
    SceneCameraCandidates cameraA, cameraB;
    float sharedScore = 0.25f;
    node->field6c.reset();
    CHECK(cameraA.submit(*node, 0, sharedScore) ==
          SceneCandidateResult::Unsupported);
    node->field6c = 1;
    node->field70 = 0;
    const auto camera = makeCameraResource(1, 60, 0.5f, 100, 0, 1, 0, 0);
    const auto input = sceneVisibilityInput(
        node->bounds, {0, 0, 0}, 1, camera.lodBias, camera.lodAmplitude, 1);
    CHECK(input && sceneUpdateType3NodeVisibility(*node, runtime.frameCounter(),
                                                  *input));
    CHECK(cameraA.submit(*node, 0, sharedScore) ==
          SceneCandidateResult::Queued);
    CHECK(cameraB.submit(*node, 0, sharedScore) ==
          SceneCandidateResult::Queued);
    int drawnA = 0, drawnB = 0;
    cameraA.drain(0, false, [&](LinkOwner &owner) {
      drawnA += &owner == node;
      return 1;
    });
    CHECK(drawnA == 1 && drawnB == 0);
    CHECK(cameraA.markLightCreated(*node));
    CHECK(cameraA.submit(*node, 0, sharedScore) ==
          SceneCandidateResult::AlreadyPresent);
    CHECK(runtime.destroy(*node));
    cameraB.drain(0, false, [&](LinkOwner &) {
      ++drawnB;
      return 1;
    });
    CHECK(drawnB == 0);
  }
  CHECK(sceneSphereRelation({0, 0, 0}, 2, {4, 0, 0}, 1) == 1);
  CHECK(sceneSphereRelation({0, 0, 0}, 2, {3, 0, 0}, 1) == 2);
  CHECK(sceneSphereRelation({0, 0, 0}, 2, {0, 0, 0}, 3) == 4);
  CHECK(sceneSphereRelation({0, 0, 0}, 3, {0, 0, 0}, 1) == 8);
  CHECK(sceneSphereRelation({0, 0, 0}, -1, {0, 0, 0}, 1) == 0);
  {
    LinkOwner a, b, c;
    a.value = 10;
    b.value = 20;
    c.value = 30;
    SceneVisibleQueue queue;
    CHECK(queue.push(a, 3) && queue.push(b, 1) && queue.push(c, 2));
    CHECK(!queue.push(a, std::numeric_limits<float>::infinity()));
    std::vector<uint32_t> visited;
    queue.drain(0, false, [&](LinkOwner &owner) {
      visited.push_back(owner.value);
      return 1;
    });
    CHECK((visited == std::vector<uint32_t>{20}));
    CHECK(!a.head && !b.head && !c.head);
    CHECK(queue.push(a, 3) && queue.push(b, 1) && queue.push(c, 2));
    visited.clear();
    queue.drain(0, true, [&](LinkOwner &owner) {
      visited.push_back(owner.value);
      return 1;
    });
    CHECK((visited == std::vector<uint32_t>{20, 30, 10}));
    CHECK(!a.head && !b.head && !c.head);
    float sharedScore = 0.25f;
    uint32_t firstFlags = 0, secondFlags = 0, ignoredFlags = 0;
    CHECK(sceneQueueType3(queue, a, 0, 1, 0.8f, 0.5f, sharedScore,
                          firstFlags) == SceneCandidateResult::Queued);
    CHECK(sceneQueueType3(queue, b, 2, 1, 0, 0.5f, sharedScore, secondFlags) ==
          SceneCandidateResult::Queued);
    CHECK(sceneQueueType3(queue, c, 0, 0.1f, 0.5f, 0.5f, sharedScore,
                          ignoredFlags) ==
          SceneCandidateResult::BelowThreshold);
    CHECK(sharedScore == 0.5f && firstFlags == 4 && secondFlags == 4 &&
          ignoredFlags == 0);
    visited.clear();
    queue.drain(0, true, [&](LinkOwner &owner) {
      visited.push_back(owner.value);
      return 1;
    });
    CHECK((visited == std::vector<uint32_t>{10, 20}));
    firstFlags = 0x10000000;
    CHECK(sceneQueueType3(queue, a, 2, 1, 0, 0.5f, sharedScore, firstFlags) ==
          SceneCandidateResult::AlreadyPresent);
    CHECK(sceneQueueType3(queue, a, 1, 1, 0, 0.5f, sharedScore, firstFlags) ==
          SceneCandidateResult::AlreadyPresent);
    CHECK(sharedScore == 1.0f);
    CHECK(sceneQueueType3(queue, a, 3, 1, 0, 0.5f, sharedScore, firstFlags) ==
          SceneCandidateResult::Unsupported);
    firstFlags = 0;
    sharedScore = 0.8f;
    CHECK(sceneQueueType3(queue, a, 0, 1, 0.75f, 0.5f, sharedScore, firstFlags,
                          true) == SceneCandidateResult::Queued);
    CHECK(sharedScore == 0.8f);
    queue.drain(0, true, [&](LinkOwner &) { return 1; });
    firstFlags = secondFlags = 0;
    sharedScore = 0.8f;
    CHECK(sceneQueueType3(queue, a, 1, 1, 0.25f, 0.5f, sharedScore,
                          firstFlags) == SceneCandidateResult::Queued);
    CHECK(sharedScore == 0.75f);
    CHECK(sceneQueueType3(queue, b, 1, 1, 0.9f, 0.5f, sharedScore, secondFlags,
                          true) == SceneCandidateResult::Queued);
    CHECK(sharedScore == 0.75f);
    visited.clear();
    queue.drain(0, true, [&](LinkOwner &owner) {
      visited.push_back(owner.value);
      return 1;
    });
    CHECK((visited == std::vector<uint32_t>{20, 10}));
  }
  CHECK(sceneRenderKindFromFlags(0) == SceneRenderKind::Kind2);
  CHECK(sceneRenderKindFromFlags(0x800) == SceneRenderKind::Kind0);
  CHECK(sceneRenderKindFromFlags(0x100000 | 0x800) == SceneRenderKind::Kind6);
  CHECK(sceneRenderKindFromFlags(0x2000 | 0x100000 | 0x800) ==
        SceneRenderKind::Kind5);
  CHECK(sceneRenderKindFromFlags(0x100 | 0x2000 | 0x100000 | 0x800) ==
        SceneRenderKind::Kind4);
  CHECK(sceneRenderKindFromFlags(0x4 | 0x100 | 0x2000 | 0x100000 | 0x800) ==
        SceneRenderKind::Kind3);
  PackRegistry registry("ref/raw");
  std::string error;
  uint32_t id = 0;
  PackResource material{};
  CHECK(registry.resolveLiteral("frontend.rpk", 0xd7, id, error));
  CHECK(registry.lookup(id, material, error));
  const auto result = resourceMaterialParse(material);
  CHECK(result.ok && result.flags == 4096 && result.mesh == 0xd9 &&
        result.textures == std::vector<uint32_t>{0xaf});
  CHECK(sceneRenderKindFromFlags(result.flags) == SceneRenderKind::Kind2);
  auto pass = scenePasses(0, false, false, false);
  CHECK(pass.count == 1 && pass.passes[0] == ScenePass::Pass41c &&
        !pass.customCollection);
  pass = scenePasses(1, false, true, true);
  CHECK(pass.count == 1 && pass.passes[0] == ScenePass::Pass4cc &&
        pass.customCollection);
  pass = scenePasses(2, false, true, false);
  CHECK(pass.count == 2 && pass.passes[0] == ScenePass::Pass448 &&
        pass.passes[1] == ScenePass::Pass474 && !pass.customCollection);
  pass = scenePasses(3, false, false, true);
  CHECK(pass.count == 1 && pass.passes[0] == ScenePass::Pass50c &&
        !pass.customCollection);
  pass = scenePasses(0xffffffff, true, true, true);
  CHECK(pass.count == 1 && pass.passes[0] == ScenePass::Pass548 &&
        !pass.customCollection);
  CHECK(sceneType0EntryFlags(0) == 0 && sceneType0EntryFlags(0x40000000) == 1 &&
        sceneType0EntryFlags(0x80000000) == 2 &&
        sceneType0EntryFlags(0xc0000000) == 3);
  const auto model =
      scxLoadFile("ref/raw/frontend/meshes/etalon_negyzet_alpha.SCX");
  CHECK(model.ok && model.model.materials.size() == 1 &&
        model.model.meshes.size() == 1);
  const auto &meshMaterial = model.model.materials[0];
  CHECK(meshMaterial.flags == 0 && meshMaterial.functionWord == 0x40000000);
  pass = scenePasses(sceneType0EntryFlags(meshMaterial.functionWord), false,
                     false, false);
  CHECK(pass.count == 1 && pass.passes[0] == ScenePass::Pass4cc);
  auto scaled = model.model;
  const auto &original = model.model.meshes[0];
  CHECK(original.positions.size() ==
            static_cast<size_t>(original.vertexCount) * 3 &&
        !original.positions.empty());
  CHECK(sceneScaleMesh(scaled, 2.0f, 3.0f, 4.0f));
  const auto &mesh = scaled.meshes[0];
  for (size_t i = 0; i < original.positions.size(); i += 3) {
    CHECK(mesh.positions[i] == original.positions[i] * 2.0f);
    CHECK(mesh.positions[i + 1] == original.positions[i + 1] * 3.0f);
    CHECK(mesh.positions[i + 2] == original.positions[i + 2] * 4.0f);
  }
  CHECK(mesh.normals == original.normals && mesh.uv1 == original.uv1 &&
        mesh.indices == original.indices);
  auto malformed = model.model;
  malformed.meshes.push_back(original);
  malformed.meshes.back().positions.pop_back();
  CHECK(!sceneScaleMesh(malformed, 2.0f, 3.0f, 4.0f));
  CHECK(malformed.meshes[0].positions == original.positions);
  LinkOwner meshOwner, textureOwner, loadingOwner;
  meshOwner.value = result.mesh;
  textureOwner.value = result.textures[0];
  loadingOwner.value = 0xa8;
  int refreshes = 0;
  const auto refresh = [&] { ++refreshes; };
  {
    SceneMaterialResources links(&meshOwner, {&textureOwner});
    CHECK(textureOwner.head && meshOwner.head);
    CHECK(links.replace(0xaf, &loadingOwner, refresh));
    CHECK(links.meshId() == 0xd9 && links.textureId(0) == 0xa8 &&
          !textureOwner.head && loadingOwner.head);
    CHECK(links.replace(0xa8, &loadingOwner, refresh) && refreshes == 2);
    CHECK(!links.replace(0x1234, nullptr, refresh) && refreshes == 2);
    CHECK(links.meshId() == 0xd9 && links.textureId(0) == 0xa8);
    CHECK(links.replace(0xa8, nullptr, refresh) && links.textureId(0) == 0 &&
          !loadingOwner.head && refreshes == 3);
  }
  CHECK(!meshOwner.head && !textureOwner.head && !loadingOwner.head);
  LinkOwner five, nine;
  five.value = 5;
  nine.value = 9;
  {
    SceneMaterialResources duplicates(&five, {&five, &five});
    CHECK(duplicates.replace(5, &nine, refresh) && duplicates.meshId() == 9 &&
          duplicates.textureId(0) == 5);
    CHECK(duplicates.replace(5, nullptr, refresh) &&
          duplicates.textureId(0) == 0 && duplicates.textureId(1) == 5);
  }
  CHECK(!five.head && !nine.head);
  {
    SceneMaterialResources links(&five, {&nine, nullptr, &five}, 0x8280);
    links.setLod(0.25f, 5.0f);
    auto copy = links.clone();
    CHECK(copy->textureCount() == 3 && copy->textureId(0) == 9 &&
          copy->textureId(1) == 0 && copy->textureId(2) == 5 &&
          copy->flags() == 0x8280 && copy->defaults().lodBias == 0.25f &&
          copy->defaults().lodAmplitude == 5.0f);
  }
  GameWorld world;
  CHECK(world.addRoot());
  ResourceWorld resources(world, registry);
  {
    uint32_t cityId = 0;
    CHECK(registry.resolveLiteral("maps\\city.rpk", 0x10, cityId, error));
    PackResource cityPayload{};
    CHECK(registry.lookup(cityId, cityPayload, error));
    const auto city = resourceMaterialParse(cityPayload);
    CHECK(city.ok && city.textures.size() == 95);
    auto loaded = sceneLoadMaterial(resources, cityId);
    CHECK(loaded.material && loaded.flags == 2048 &&
          loaded.material->textureCount() == city.textures.size());
    PackView view;
    CHECK(registry.view(static_cast<uint16_t>(cityId >> 16), view, error) ==
          PackViewStatus::Ok);
    const ResourceIdMapping mapping{view.slot,
                                    static_cast<uint32_t>(registry.slotCount()),
                                    view.dependencies};
    CHECK(loaded.material->meshId() == remapResourceId(city.mesh, mapping));
    for (size_t i = 0; i < city.textures.size(); ++i)
      CHECK(loaded.material->textureId(i) ==
            remapResourceId(city.textures[i], mapping));
    auto clone = loaded.material->clone();
    loaded.material.reset();
    for (size_t i = 0; i < city.textures.size(); ++i)
      CHECK(clone->textureId(i) == remapResourceId(city.textures[i], mapping));
  }
  {
    auto loaded = sceneLoadMaterial(resources, id);
    CHECK(loaded.material && loaded.flags == 4096);
    uint32_t meshId = 0, textureId = 0, loadingId = 0;
    CHECK(registry.resolveLiteral("frontend.rpk", 0xd9, meshId, error));
    CHECK(registry.resolveLiteral("frontend.rpk", 0xaf, textureId, error));
    CHECK(registry.resolveLiteral("frontend.rpk", 0xa8, loadingId, error));
    CHECK(loaded.material->meshId() == meshId &&
          loaded.material->textureId(0) == textureId);
    const auto loadingNode = resources.lookup(loadingId, 7);
    CHECK(loadingNode.status == LookupStatus::Found && loadingNode.node.owner);
    const auto dynamicMesh = resources.lookup(meshId, 5);
    if (dynamicMesh.node.owner && loadingNode.node.owner) {
      SceneMaterialResources dynamicBatch(dynamicMesh.node.owner,
                                          {loadingNode.node.owner}, 0x8000);
      SceneMaterialCache unsupported;
      CHECK(!sceneRefreshMaterial(resources, dynamicBatch, "ref/raw",
                                  unsupported, error));
      CHECK(error.find("dynamic batch allocation unsupported") !=
            std::string::npos);
    }
    const auto materialNode = resources.lookup(id, 14);
    CHECK(materialNode.status == LookupStatus::Found);
    auto bound =
        std::shared_ptr<SceneMaterialResources>(std::move(loaded.material));
    CHECK(resources.bindSceneMaterial(materialNode.node, bound));
    CHECK(resources.sceneMaterial(materialNode.node) == bound);
    WorldNode *clonedNode =
        sceneCloneMaterial(resources, materialNode.node, error);
    CHECK(clonedNode && clonedNode->type == 14 && clonedNode->loaded &&
          (clonedNode->flags & 0x200));
    CHECK(world.parentOwner(clonedNode->handle) ==
          world.parentOwner(materialNode.node.packNode->handle));
    const auto clonedResource = resources.resolve(clonedNode);
    auto clone = resources.sceneMaterial(clonedResource);
    CHECK(clone && clone->meshId() == meshId &&
          clone->textureId(0) == textureId && clone->flags() == 4096);
    SceneMaterialCache cache;
    CHECK(sceneRefreshMaterial(resources, *clone, "ref/raw", cache, error));
    CHECK(cache.flags == 4096 && cache.meshId == meshId &&
          cache.textureId == textureId);
    CHECK(cache.texture.width > 0 && cache.texture.height > 0 &&
          !cache.texture.rgba.empty());
    int calls = 0;
    bool refreshed = false;
    CHECK(clone->replace(textureId, loadingNode.node.owner, [&] {
      ++calls;
      refreshed =
          sceneRefreshMaterial(resources, *clone, "ref/raw", cache, error);
    }));
    CHECK(calls == 1 && refreshed && clone->textureId(0) == loadingId);
    CHECK(bound->textureId(0) == textureId);
    resources.unbindSceneMaterial(materialNode.node);
    bound.reset();
    CHECK(!resources.sceneMaterial(materialNode.node));
    CHECK(clone->meshId() == meshId && clone->textureId(0) == loadingId);
    CHECK(sceneRefreshMaterial(resources, *clone, "ref/raw", cache, error));
    CHECK(cache.flags == 4096 && cache.meshId == meshId &&
          cache.textureId == loadingId);
    CHECK(cache.mesh.meshes.size() == 1 &&
          cache.mesh.meshes[0].positions == original.positions &&
          cache.mesh.meshes[0].uv1 == original.uv1 &&
          cache.mesh.meshes[0].indices == original.indices);
    const auto &texturePixels = cache.texture;
    CHECK(texturePixels.width == 1024 && texturePixels.height == 1024 &&
          texturePixels.rgba.size() == 1024u * 1024u * 4u);
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    Surface surface(SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_RGBA32),
                    SDL_DestroySurface);
    CHECK(surface);
    auto renderer = Render2D::forSurface(surface.get(), error);
    CHECK(renderer);
    // Resource texture handles call back into the renderer on destruction.
    // Release the fixture's cache before the renderer, including on CHECK
    // failure.
    struct TextureCacheGuard {
      ResourceNode *node;
      ~TextureCacheGuard() {
        if (node)
          node->texture.reset();
      }
    } textureCacheGuard{loadingNode.node.packNode};
    const auto texture = renderer->texture(
        texturePixels.width, texturePixels.height, texturePixels.rgba.data(),
        texturePixels.rgba.size(), error);
    const float projection[] = {1.28f, 0, 0, 64, 0, -1.28f, 0, 64};
    std::vector<RenderVertex> vertices;
    CHECK(sceneProjectMesh(cache.mesh.meshes[0], projection, vertices, error));
    CHECK(vertices.size() == 4 && original.indices.size() == 6);
    const auto initialVertices = vertices;
    SceneRenderInstance instance(nullptr);
    instance.setBoneTranslation(2, 0, 0);
    instance.processRootBoneUpdate();
    instance.refreshSingleBoneDrawMatrix();
    CHECK(sceneProjectTransformedMesh(cache.mesh.meshes[0],
                                      instance.drawMatrix(), projection,
                                      vertices, error));
    CHECK(vertices.size() == initialVertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
      CHECK(std::abs(vertices[i].x - initialVertices[i].x - 25.6f) < 0.001f);
      CHECK(vertices[i].y == initialVertices[i].y &&
            vertices[i].u == initialVertices[i].u &&
            vertices[i].v == initialVertices[i].v);
    }
    const uint8_t whitePixel[] = {255, 255, 255, 255};
    const auto whiteTexture =
        renderer->texture(1, 1, whitePixel, sizeof(whitePixel), error);
    SceneRenderInstance drawInstance(nullptr);
    drawInstance.setBoneTranslation(2, 0, 0);
    CHECK(whiteTexture && renderer->clear(0, 0, 0, 255, error));
    LinkOwner drawOwner;
    SceneVisibleQueue visible;
    float scoreState = 0;
    uint32_t recordFlags = 0;
    CHECK(sceneQueueType3(visible, drawOwner, 2, 1, 0, 0, scoreState,
                          recordFlags) == SceneCandidateResult::Queued);
    bool drawn = false;
    visible.drain(0, false, [&](LinkOwner &candidate) {
      if (&candidate == &drawOwner)
        drawn = sceneDrawSingleBoneMesh(*renderer, *whiteTexture,
                                        cache.mesh.meshes[0], drawInstance,
                                        projection, error);
      return 1;
    });
    CHECK(drawn && !drawOwner.head && renderer->present(error));
    CHECK(!drawInstance.rootBoneUpdatePending() &&
          drawInstance.drawMatrix()[12] == 20);
    CHECK(SDL_LockSurface(surface.get()));
    const auto *shiftedLeft = static_cast<const uint8_t *>(surface->pixels) +
                              64 * surface->pitch + 5 * 4;
    const auto *shiftedCenter = static_cast<const uint8_t *>(surface->pixels) +
                                64 * surface->pitch + 64 * 4;
    const bool shiftedPixels = shiftedLeft[0] == 0 && shiftedLeft[1] == 0 &&
                               shiftedLeft[2] == 0 &&
                               (shiftedCenter[0] != 0 ||
                                shiftedCenter[1] != 0 || shiftedCenter[2] != 0);
    SDL_UnlockSurface(surface.get());
    CHECK(shiftedPixels);
    const auto camera = makeCameraResource(1, 60, 0.5f, 100, 0, 1, 0, 0);
    const auto cameraProjection =
        cameraProjectionMatrix(updateCameraRendererFields(camera, 1));
    SceneRenderInstance cameraInstance(nullptr), perspectiveInstance(nullptr);
    const auto view = cameraViewMatrix(cameraInstance.boneMatrix());
    CHECK(view);
    auto depthMatrix = cameraInstance.boneMatrix();
    depthMatrix[14] = -400;
    const auto depth =
        sceneTransparentDepth(depthMatrix, cameraInstance.boneMatrix());
    CHECK(depth && *depth == -400);
    perspectiveInstance.setBoneTranslation(0, 0, -20);
    const ViewportInstance viewport{0, 0, 0, 1, 1, ViewportList::Active, {}};
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          sceneDrawCameraMesh(*renderer, *whiteTexture, cache.mesh.meshes[0],
                              perspectiveInstance, *view, cameraProjection,
                              viewport, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *cameraCenter = static_cast<const uint8_t *>(surface->pixels) +
                               64 * surface->pitch + 64 * 4;
    const bool cameraPixel = cameraCenter[0] == 255 && cameraCenter[1] == 255 &&
                             cameraCenter[2] == 255;
    SDL_UnlockSurface(surface.get());
    CHECK(cameraPixel);
    auto *frameRoot =
        world.addChild(*world.defaultParent(), "scene-fixture", 0);
    CHECK(frameRoot);
    world.markLoaded(*frameRoot);
    auto *runtimeNode = world.addTypedChild(*frameRoot, 3, "render-fixture", 0);
    CHECK(runtimeNode);
    auto liveInstance = std::make_shared<SceneRenderInstance>(clonedNode);
    liveInstance->setBoneTranslation(0, 0, -4);
    CHECK(resources.bindSceneRenderInstance(resources.resolve(runtimeNode),
                                            liveInstance));
    auto *runtimeNode2 =
        world.addTypedChild(*frameRoot, 3, "render-fixture-2", 0);
    CHECK(runtimeNode2);
    auto *clonedNode2 = sceneCloneMaterial(resources, clonedResource, error);
    CHECK(clonedNode2);
    auto liveInstance2 = std::make_shared<SceneRenderInstance>(clonedNode2);
    liveInstance2->setBoneTranslation(0.1f, 0, -5);
    CHECK(resources.bindSceneRenderInstance(resources.resolve(runtimeNode2),
                                            liveInstance2));
    auto liveRenderer = SceneMaterialRenderer::create(resources, *renderer,
                                                      *runtimeNode, error);
    CHECK(liveRenderer);
    if (liveRenderer) {
      SceneMaterialCache strictCache;
      CHECK(!sceneRefreshMaterial(resources, *clone, "ref/raw", strictCache,
                                  error, true));
      CHECK(error.find("mesh is not cached") != std::string::npos);
      const auto meshNode = resources.lookup(meshId, 5);
      CHECK(meshNode.status == LookupStatus::Found &&
            resources.bindSceneMesh(meshNode.node,
                                    std::make_shared<ScxModel>(cache.mesh)));
      CHECK(!sceneRefreshMaterial(resources, *clone, "ref/raw", strictCache,
                                  error, true));
      CHECK(error.find("texture is not cached") != std::string::npos);
      // Draw-time caching needs a new frame to establish resource demand.
      world.setFrameCounter(1);
      CHECK(liveRenderer->draw(*view, cameraProjection, viewport, error, true));
      CHECK(loadingNode.node.packNode->texture &&
            loadingNode.node.packNode->texture->handle);
      auto cachedRenderer = SceneMaterialRenderer::create(
          resources, *renderer, *runtimeNode2, error, true);
      CHECK(cachedRenderer);
    }
    {
      const auto sourceMesh = resources.lookup(meshId, 5);
      auto *spatialMeshNode = sceneCloneMesh(resources, sourceMesh.node, error);
      CHECK(spatialMeshNode);
      auto spatialMesh =
          resources.sceneMesh(resources.resolve(spatialMeshNode));
      CHECK(spatialMesh);
      ScxModel model3d;
      ScxMaterial spatialMaterial;
      spatialMaterial.diffuse = {1, 1, 1, 1};
      spatialMaterial.maps.push_back({});
      model3d.materials.push_back(spatialMaterial);
      ScxMesh triangle;
      triangle.materialIndex = 0;
      triangle.vertexCount = 3;
      triangle.positions = {-8, -8, 2.5f, 8, -8, 2.5f, 0, 8, 2.5f};
      triangle.uv1 = {0, 0, 1, 0, 0.5f, 1};
      triangle.indices = {0, 1, 2};
      model3d.meshes.push_back(triangle);
      *spatialMesh = model3d;
      auto *spatialType =
          world.addTypedChild(*frameRoot, 14, "spatial-type", 0);
      auto *nearNode = world.addTypedChild(*frameRoot, 3, "spatial-near", 0);
      auto *farNode = world.addTypedChild(*frameRoot, 3, "spatial-far", 0);
      CHECK(spatialType && nearNode && farNode);
      world.markLoaded(*spatialType);
      CHECK(resources.bindSceneMaterial(
          resources.resolve(spatialType),
          std::make_shared<SceneMaterialResources>(
              spatialMeshNode,
              std::vector<LinkOwner *>{loadingNode.node.owner})));
      auto nearInstance = std::make_shared<SceneRenderInstance>(spatialType);
      auto farInstance = std::make_shared<SceneRenderInstance>(spatialType);
      farInstance->setBoneTranslation(0, 0, 0.05f);
      CHECK(resources.bindSceneRenderInstance(resources.resolve(nearNode),
                                              nearInstance));
      CHECK(resources.bindSceneRenderInstance(resources.resolve(farNode),
                                              farInstance));
      auto nearRenderer =
          SceneMaterialRenderer::create(resources, *renderer, *nearNode, error);
      auto farRenderer =
          SceneMaterialRenderer::create(resources, *renderer, *farNode, error);
      CHECK(nearRenderer && farRenderer);
      const std::array<float, 16> identity{
          {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
      auto textureCache = loadingNode.node.packNode->texture;
      CHECK(textureCache);
      auto savedPixels = textureCache->pixels;
      textureCache->pixels = {1, 1, {255, 0, 0, 255}, {}, {}};
      Raster3D raster;
      SceneSpatialTextures spatialTextures;
      CHECK(raster.resize(128, 128, error));
      raster.clear(0, 0, 0, 0);
      CHECK(nearRenderer->drawSpatial(raster, spatialTextures, identity,
                                      identity, viewport, error));
      textureCache->pixels.rgba = {0, 0, 255, 255};
      CHECK(farRenderer->drawSpatial(raster, spatialTextures, identity,
                                     identity, viewport, error));
      const size_t center = (64 * 128 + 64) * 4;
      CHECK(raster.pixels()[center] == 255 && raster.pixels()[center + 2] == 0);
      spatialMesh->meshes[0].normals = {0, 0, 1};
      CHECK(!nearRenderer->drawSpatial(raster, spatialTextures, identity,
                                       identity, viewport, error));
      CHECK(error.find("lighting, skinning or extra streams unsupported") !=
            std::string::npos);
      spatialMesh->meshes[0].normals.clear();
      nearNode->field6c = farNode->field6c = 1;
      nearNode->field70 = farNode->field70 = 0;
      SceneCameraRenderer spatialFrame(resources, *renderer);
      float nearScore = 0, farScore = 0;
      CHECK(spatialFrame.candidates().submit(*nearNode, 0, nearScore, true) ==
            SceneCandidateResult::Queued);
      CHECK(spatialFrame.candidates().submit(*farNode, 0, farScore, true) ==
            SceneCandidateResult::Queued);
      CHECK(renderer->clear(30, 40, 50, 255, error) &&
            spatialFrame.draw(identity, identity, viewport, error) &&
            renderer->present(error));
      CHECK(SDL_LockSurface(surface.get()));
      const auto *spatialCenter =
          static_cast<const uint8_t *>(surface->pixels) + 64 * surface->pitch +
          64 * 4;
      const bool spatialPixel = spatialCenter[2] == 255;
      const auto *spatialCorner = static_cast<const uint8_t *>(surface->pixels);
      const bool preservedBackground = spatialCorner[0] == 30 &&
                                       spatialCorner[1] == 40 &&
                                       spatialCorner[2] == 50;
      SDL_UnlockSurface(surface.get());
      CHECK(spatialPixel && preservedBackground);
      textureCache->pixels.rgba = {255, 0, 0, 255};
      CHECK(renderer->clear(30, 40, 50, 255, error) &&
            spatialFrame.draw(identity, identity, viewport, error) &&
            renderer->present(error));
      CHECK(SDL_LockSurface(surface.get()));
      spatialCenter = static_cast<const uint8_t *>(surface->pixels) +
                      64 * surface->pitch + 64 * 4;
      const bool updatedPixel =
          spatialCenter[0] == 255 && spatialCenter[2] == 0;
      SDL_UnlockSurface(surface.get());
      CHECK(updatedPixel);
      textureCache->pixels = std::move(savedPixels);
      CHECK(world.destroy(*nearNode) && world.destroy(*farNode));
      CHECK(world.destroy(*spatialType) && world.destroy(*spatialMeshNode));
    }
    auto liveRenderer2 = SceneMaterialRenderer::create(resources, *renderer,
                                                       *runtimeNode2, error);
    CHECK(liveRenderer2);
    {
      uint32_t tileId = 0, tile2Id = 0;
      CHECK(
          registry.resolveLiteral("maps/city/smallmap.rpk", 1, tileId, error));
      CHECK(
          registry.resolveLiteral("maps/city/smallmap.rpk", 6, tile2Id, error));
      const auto tileNode = resources.lookup(tileId, 14);
      const auto tile2 = resources.lookup(tile2Id, 7);
      auto tileLoad = sceneLoadMaterial(resources, tileId);
      CHECK(tileLoad.material && tileLoad.flags == 0 &&
            tile2.status == LookupStatus::Found);
      CHECK(resources.bindSceneMaterial(tileNode.node,
                                        std::shared_ptr<SceneMaterialResources>(
                                            std::move(tileLoad.material))));
      auto *tileClone = sceneCloneMaterial(resources, tileNode.node, error);
      CHECK(tileClone);
      auto tile = resources.sceneMaterial(resources.resolve(tileClone));
      CHECK(tile);
      auto *tileRender = world.addTypedChild(*frameRoot, 3, "tile", 0);
      CHECK(tileRender);
      CHECK(resources.bindSceneRenderInstance(
          resources.resolve(tileRender),
          std::make_shared<SceneRenderInstance>(tileClone)));
      auto tileObject = SceneMaterialRenderer::create(resources, *renderer,
                                                      *tileRender, error);
      CHECK(tileObject);
      const std::array<float, 16> tileView{{0.0274f, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0.0274f, 0, 0, -0.8f, -0.8f, 0.5f,
                                            1}};
      const std::array<float, 16> tileProjection{
          {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
      const auto flatTile =
          tileObject->spatial(tileView, tileProjection, error);
      CHECK(flatTile && !*flatTile);
      const auto depthTile =
          tileObject->spatial(tileProjection, tileProjection, error);
      CHECK(depthTile && *depthTile);
      CHECK(renderer->clear(0, 0, 0, 255, error) &&
            tileObject->draw(tileView, tileProjection, viewport, error) &&
            renderer->present(error));
      CHECK(SDL_LockSurface(surface.get()));
      const auto *pixels = static_cast<const uint8_t *>(surface->pixels);
      const std::vector<uint8_t> firstTile(pixels, pixels + surface->pitch *
                                                                surface->h);
      SDL_UnlockSurface(surface.get());
      CHECK(std::any_of(firstTile.begin(), firstTile.end(), [](uint8_t value) {
        return value != 0 && value != 255;
      }));
      CHECK(tile->replace(tile->textureId(0), tile2.node.owner, [] {}));
      CHECK(renderer->clear(0, 0, 0, 255, error) &&
            tileObject->draw(tileView, tileProjection, viewport, error) &&
            renderer->present(error));
      CHECK(SDL_LockSurface(surface.get()));
      pixels = static_cast<const uint8_t *>(surface->pixels);
      const std::vector<uint8_t> secondTile(pixels, pixels + surface->pitch *
                                                                 surface->h);
      SDL_UnlockSurface(surface.get());
      CHECK(firstTile != secondTile);
      world.destroy(*tileRender);
    }
    SceneCameraRenderer frameRenderer(resources, *renderer);
    auto &liveCandidates = frameRenderer.candidates();
    const CameraTraversalParams cameraParams{
        {0, 0, 0}, 1, camera.lodBias, camera.lodAmplitude, 1, 0, 0, false, {}, {}};
    CameraType1Records type1Records;
    CHECK(traverseCameraCandidates(world, *frameRoot, liveCandidates,
                                   cameraParams, error, &type1Records));
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          frameRenderer.draw(*view, cameraProjection, viewport, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *resourceCenter = static_cast<const uint8_t *>(surface->pixels) +
                                 64 * surface->pitch + 64 * 4;
    const bool resourcePixel = resourceCenter[0] != 0 ||
                               resourceCenter[1] != 0 || resourceCenter[2] != 0;
    const auto *framePixels = static_cast<const uint8_t *>(surface->pixels);
    const std::vector<uint8_t> orderedPixels(
        framePixels, framePixels + surface->pitch * surface->h);
    SDL_UnlockSurface(surface.get());
    CHECK(resourcePixel);
    // A cached texture is shared by the material renderer: no second upload is
    // possible here because the retained CPU pixel buffer is deliberately
    // empty.
    if (texture && loadingNode.node.packNode &&
        loadingNode.node.packNode->texture) {
      auto sharedTexture = loadingNode.node.packNode->texture;
      auto savedPixels = std::move(sharedTexture->pixels.rgba);
      CHECK(
          renderer->clear(0, 0, 0, 255, error) &&
          frameRenderer.draw(*view, cameraProjection, viewport, error, true) &&
          renderer->present(error));
      CHECK(renderer->draw(*sharedTexture->handle, {0, 0, 8, 8}, error));
      sharedTexture->pixels.rgba = std::move(savedPixels);
    }
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          liveRenderer2->draw(*view, cameraProjection, viewport, error) &&
          liveRenderer->draw(*view, cameraProjection, viewport, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *expectedPixels = static_cast<const uint8_t *>(surface->pixels);
    const std::vector<uint8_t> expectedOrder(
        expectedPixels, expectedPixels + surface->pitch * surface->h);
    SDL_UnlockSurface(surface.get());
    CHECK(orderedPixels == expectedOrder);
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          liveRenderer->draw(*view, cameraProjection, viewport, error) &&
          liveRenderer2->draw(*view, cameraProjection, viewport, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *reversedPixels = static_cast<const uint8_t *>(surface->pixels);
    const std::vector<uint8_t> reversedOrder(
        reversedPixels, reversedPixels + surface->pitch * surface->h);
    SDL_UnlockSurface(surface.get());
    CHECK(expectedOrder != reversedOrder);
    uint32_t fontId = 0;
    CHECK(registry.resolveLiteral("frontend.rpk", 0x138, fontId, error));
    const auto fontNode = resources.lookup(fontId, 14);
    CHECK(fontNode.status == LookupStatus::Found);
    auto fontLoad = sceneLoadMaterial(resources, fontId);
    CHECK(fontLoad.material && fontLoad.flags == 4100);
    {
      const auto meshNode = resources.lookup(fontLoad.material->meshId(), 5);
      const auto textureNode =
          resources.lookup(fontLoad.material->textureId(0), 7);
      CHECK(meshNode.node.owner && textureNode.node.owner);
      SceneMaterialResources multiFont(
          meshNode.node.owner, {textureNode.node.owner, textureNode.node.owner},
          4100);
      SceneMaterialCache rejected;
      CHECK(!sceneRefreshMaterial(resources, multiFont, registry.root(),
                                  rejected, error));
      CHECK(error == "material texture count unsupported");
    }
    {
      auto &packed = *fontNode.node.packNode;
      const auto originalOffset = packed.payloadOffset;
      ++packed.payloadOffset;
      const auto embedded = sceneLoadMaterial(resources, fontId);
      packed.payloadOffset = originalOffset;
      CHECK(!embedded.material &&
            embedded.error == "embedded material payload unsupported");
    }
    CHECK(resources.bindSceneMaterial(
        fontNode.node,
        std::shared_ptr<SceneMaterialResources>(std::move(fontLoad.material))));
    auto *textNode = world.addTypedChild(*frameRoot, 3, "text-fixture", 0);
    CHECK(textNode);
    auto textInstance =
        std::make_shared<SceneRenderInstance>(fontNode.node.owner);
    textInstance->setTextState({"AB", 2, 0xffff0000u, 1});
    CHECK(resources.bindSceneRenderInstance(resources.resolve(textNode),
                                            textInstance));
    textNode->field6c = 1;
    textNode->field70 = 0;
    float textScore = 0;
    CHECK(liveCandidates.submit(*textNode, 0, textScore, true) ==
          SceneCandidateResult::Queued);
    auto textRenderer =
        SceneMaterialRenderer::create(resources, *renderer, *textNode, error);
    CHECK(textRenderer);
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          frameRenderer.draw(*view, cameraProjection, viewport, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *mixedPixels = static_cast<const uint8_t *>(surface->pixels);
    const std::vector<uint8_t> mixedOrder(
        mixedPixels, mixedPixels + surface->pitch * surface->h);
    SDL_UnlockSurface(surface.get());
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          liveRenderer2->draw(*view, cameraProjection, viewport, error) &&
          liveRenderer->draw(*view, cameraProjection, viewport, error) &&
          textRenderer->draw(*view, cameraProjection, viewport, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *explicitMixedPixels =
        static_cast<const uint8_t *>(surface->pixels);
    const std::vector<uint8_t> explicitMixed(
        explicitMixedPixels, explicitMixedPixels + surface->pitch * surface->h);
    SDL_UnlockSurface(surface.get());
    CHECK(mixedOrder == explicitMixed && mixedOrder != expectedOrder);
    std::vector<WorldNode *> extraText;
    std::vector<std::unique_ptr<SceneMaterialRenderer>> extraRenderers;
    for (uint32_t color : {0xff00ff00u, 0xff0000ffu}) {
      auto *item = world.addTypedChild(*frameRoot, 3, "text-extra", 0);
      CHECK(item);
      auto state = std::make_shared<SceneRenderInstance>(fontNode.node.owner);
      state->setTextState({"AB", 2, color, 1});
      CHECK(resources.bindSceneRenderInstance(resources.resolve(item), state));
      item->field6c = 1;
      item->field70 = 0;
      float score = 0;
      CHECK(liveCandidates.submit(*item, 0, score, true) ==
            SceneCandidateResult::Queued);
      auto object =
          SceneMaterialRenderer::create(resources, *renderer, *item, error);
      CHECK(object);
      extraText.push_back(item);
      extraRenderers.push_back(std::move(object));
    }
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          frameRenderer.draw(*view, cameraProjection, viewport, error));
    CHECK(frameRenderer.remove(*textNode));
    CHECK(world.destroy(*textNode));
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          frameRenderer.draw(*view, cameraProjection, viewport, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *swapPixels = static_cast<const uint8_t *>(surface->pixels);
    const std::vector<uint8_t> swapOrder(
        swapPixels, swapPixels + surface->pitch * surface->h);
    SDL_UnlockSurface(surface.get());
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          liveRenderer2->draw(*view, cameraProjection, viewport, error) &&
          liveRenderer->draw(*view, cameraProjection, viewport, error) &&
          extraRenderers[1]->draw(*view, cameraProjection, viewport, error) &&
          extraRenderers[0]->draw(*view, cameraProjection, viewport, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *explicitSwapPixels =
        static_cast<const uint8_t *>(surface->pixels);
    const std::vector<uint8_t> explicitSwap(
        explicitSwapPixels, explicitSwapPixels + surface->pitch * surface->h);
    SDL_UnlockSurface(surface.get());
    CHECK(swapOrder == explicitSwap);
    for (auto *item : extraText) {
      CHECK(frameRenderer.remove(*item));
      CHECK(world.destroy(*item));
    }
    CHECK(frameRenderer.remove(*runtimeNode));
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          frameRenderer.draw(*view, cameraProjection, viewport, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *singlePixels = static_cast<const uint8_t *>(surface->pixels);
    const std::vector<uint8_t> afterRemoval(
        singlePixels, singlePixels + surface->pitch * surface->h);
    SDL_UnlockSurface(surface.get());
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          liveRenderer2->draw(*view, cameraProjection, viewport, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *expectedSinglePixels =
        static_cast<const uint8_t *>(surface->pixels);
    const std::vector<uint8_t> expectedSingle(expectedSinglePixels,
                                              expectedSinglePixels +
                                                  surface->pitch * surface->h);
    SDL_UnlockSurface(surface.get());
    CHECK(afterRemoval == expectedSingle && afterRemoval != orderedPixels);
    CHECK(traverseCameraCandidates(world, *runtimeNode, liveCandidates,
                                   cameraParams, error));
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          frameRenderer.draw(*view, cameraProjection, viewport, error) &&
          renderer->present(error));
    auto *cameraType = world.addTypedChild(*world.defaultParent(), 12,
                                           "camera-type-fixture", 0);
    auto *cameraNode = world.addTypedChild(*frameRoot, 3, "camera-fixture", 0);
    CHECK(cameraType && cameraNode);
    cameraType->camera = camera;
    world.markLoaded(*cameraType);
    world.markLoaded(*cameraNode);
    auto cameraBinding = std::make_shared<SceneRenderInstance>(cameraType);
    CHECK(resources.bindSceneRenderInstance(resources.resolve(cameraNode),
                                            cameraBinding));
    auto attachments = std::make_shared<ViewportRenderAttachments>();
    attachments->prepare(*cameraNode, 0);
    CHECK(attachments->activate(*cameraNode, -1));
    ViewportInstance liveViewport{0,          0, 0, 1, 1, ViewportList::Active,
                                  attachments};
    CameraFrame frame(resources, *renderer);
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          frame.draw(*cameraNode, liveViewport, 1, 0, false, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *frameOutput = static_cast<const uint8_t *>(surface->pixels);
    const std::vector<uint8_t> liveFramePixels(
        frameOutput, frameOutput + surface->pitch * surface->h);
    SDL_UnlockSurface(surface.get());
    CHECK(liveFramePixels == orderedPixels);
    auto otherAttachments = std::make_shared<ViewportRenderAttachments>();
    otherAttachments->prepare(*cameraNode, 0);
    CHECK(otherAttachments->activate(*cameraNode, -1));
    ViewportInstance otherViewport{
        0, 0, 0, 1, 1, ViewportList::Active, otherAttachments};
    CHECK(!frame.draw(*cameraNode, otherViewport, 1, 0, false, error));
    CHECK(world.destroy(*cameraNode));
    CHECK(world.destroy(*cameraType));
    CHECK(clone->replace(loadingId, nullptr, [] {}));
    CHECK(!liveRenderer->draw(*view, cameraProjection, viewport, error));
    CHECK(traverseCameraCandidates(world, *runtimeNode, liveCandidates,
                                   cameraParams, error));
    CHECK(!frameRenderer.draw(*view, cameraProjection, viewport, error));
    CHECK(clone->replace(0, loadingNode.node.owner, [] {}));
    CHECK(renderer->clear(0, 0, 0, 255, error) &&
          frameRenderer.draw(*view, cameraProjection, viewport, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *restoredCenter = static_cast<const uint8_t *>(surface->pixels) +
                                 64 * surface->pitch + 64 * 4;
    const bool restoredPixel = restoredCenter[0] != 0 ||
                               restoredCenter[1] != 0 || restoredCenter[2] != 0;
    SDL_UnlockSurface(surface.get());
    CHECK(restoredPixel);
    CHECK(world.destroy(*runtimeNode2));
    CHECK(world.destroy(*runtimeNode));
    CHECK(!liveRenderer->draw(*view, cameraProjection, viewport, error));
    CHECK(frameRenderer.draw(*view, cameraProjection, viewport, error));
    liveRenderer.reset();
    if (loadingNode.node.packNode)
      loadingNode.node.packNode->texture.reset();
    perspectiveInstance.setBoneTranslation(0, 0, 20);
    CHECK(renderer->clear(0, 0, 0, 255, error));
    CHECK(sceneDrawCameraMesh(*renderer, *whiteTexture, cache.mesh.meshes[0],
                              perspectiveInstance, *view, cameraProjection,
                              viewport, error) &&
          renderer->present(error));
    Uint8 clippedR, clippedG, clippedB, clippedA;
    CHECK(SDL_ReadSurfacePixel(surface.get(), 64, 64, &clippedR, &clippedG,
                               &clippedB, &clippedA));
    CHECK(clippedR == 0 && clippedG == 0 && clippedB == 0);
    perspectiveInstance.setBoneTranslation(10, 0, -20);
    CHECK(renderer->clear(0, 0, 0, 255, error));
    CHECK(sceneDrawCameraMesh(*renderer, *whiteTexture, cache.mesh.meshes[0],
                              perspectiveInstance, *view, cameraProjection,
                              viewport, error) &&
          renderer->present(error));
    CHECK(SDL_ReadSurfacePixel(surface.get(), 120, 64, &clippedR, &clippedG,
                               &clippedB, &clippedA));
    CHECK(clippedR == 255 && clippedG == 255 && clippedB == 255);
    auto perspectiveMesh = cache.mesh.meshes[0];
    perspectiveMesh.positions[2] += 1;
    CHECK(sceneDrawCameraMesh(*renderer, *whiteTexture, perspectiveMesh,
                              perspectiveInstance, *view, cameraProjection,
                              viewport, error));
    ScxMesh importScaleMesh;
    importScaleMesh.vertexCount = 3;
    importScaleMesh.positions = {-5, -5, 5, 5, -5, 5, 0, 5, 5};
    importScaleMesh.uv1 = {0, 0, 1, 0, 0.5f, 1};
    importScaleMesh.indices = {0, 1, 2};
    SceneRenderInstance importScaleInstance(nullptr);
    const std::array<float, 16> identity{
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    CHECK(renderer->clear(0, 0, 0, 255, error));
    CHECK(sceneDrawCameraMesh(*renderer, *whiteTexture, importScaleMesh,
                              importScaleInstance, identity, identity, viewport,
                              error) &&
          renderer->present(error));
    CHECK(SDL_ReadSurfacePixel(surface.get(), 64, 64, &clippedR, &clippedG,
                               &clippedB, &clippedA) &&
          clippedR == 255);
    CHECK(SDL_ReadSurfacePixel(surface.get(), 10, 64, &clippedR, &clippedG,
                               &clippedB, &clippedA) &&
          clippedR == 0);
    // The software renderer exercises both passes with texture alpha. Vertex
    // fog is range-based; the selected stock device branch is not verified.
    const uint8_t fogTexel[] = {200, 40, 20, 128};
    ResourceTexturePixels fogPixels{1, 1, {200, 40, 20, 128}, {}, {}};
    const auto fogTexture =
        renderer->texture(1, 1, fogTexel, sizeof fogTexel, error);
    ScxMesh fogMesh = importScaleMesh;
    fogMesh.positions[0] = -20; // one vertex crosses the clip plane
    const auto fogSample = [&](const CameraFog &fog, uint8_t &r, uint8_t &g,
                               uint8_t &b) {
      uint8_t a = 0;
      return fogTexture && renderer->clear(20, 40, 60, 255, error) &&
             sceneDrawCameraMesh(*renderer, *fogTexture, fogMesh,
                                 importScaleInstance, identity, identity,
                                 viewport, error, &fog, &fogPixels) &&
             renderer->present(error) &&
             SDL_ReadSurfacePixel(surface.get(), 64, 64, &r, &g, &b, &a);
    };
    uint8_t fr, fg, fb;
    CHECK(fogSample({0x0014c864, 3, 4}, fr, fg, fb) &&
          std::abs(static_cast<int>(fr) - 110) <= 3 &&
          std::abs(static_cast<int>(fg) - 40) <= 3);
    CHECK(fogSample({0x0014c864, 0, 0.1f}, fr, fg, fb) &&
          std::abs(static_cast<int>(fr) - 20) <= 3 &&
          std::abs(static_cast<int>(fg) - 120) <= 3);
    CHECK(fogSample({0x0014c864, 0, 1}, fr, fg, fb) &&
          std::abs(static_cast<int>(fr) - 36) <= 3 &&
          std::abs(static_cast<int>(fg) - 102) <= 3 &&
          std::abs(static_cast<int>(fb) - 70) <= 3);
    if (fogTexture)
      CHECK(renderer->releaseTexture(*fogTexture, error));
    const uint8_t opaqueFogTexel[] = {200, 40, 20, 255};
    ResourceTexturePixels opaqueFogPixels{1, 1, {200, 40, 20, 255}, {}, {}};
    const auto opaqueFogTexture =
        renderer->texture(1, 1, opaqueFogTexel, sizeof opaqueFogTexel, error);
    const CameraFog farFog{0x0014c864, 0, 0.1f};
    CHECK(
        opaqueFogTexture && renderer->clear(20, 40, 60, 255, error) &&
        sceneDrawCameraMesh(*renderer, *opaqueFogTexture, fogMesh,
                            importScaleInstance, identity, identity, viewport,
                            error, &farFog, &opaqueFogPixels) &&
        renderer->present(error) &&
        SDL_ReadSurfacePixel(surface.get(), 64, 64, &fr, &fg, &fb, &clippedA) &&
        std::abs(static_cast<int>(fr) - 20) <= 3 &&
        std::abs(static_cast<int>(fg) - 200) <= 3);
    if (opaqueFogTexture)
      CHECK(renderer->releaseTexture(*opaqueFogTexture, error));
    CHECK(sceneProjectMesh(cache.mesh.meshes[0], projection, vertices, error));
    CHECK(texture && renderer->clear(0, 0, 0, 255, error) &&
          renderer->drawMesh(*texture, vertices, original.indices, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *center = static_cast<const uint8_t *>(surface->pixels) +
                         64 * surface->pitch + 64 * 4;
    const bool centerMatches =
        center[0] > 0 && center[1] > 0 && center[2] > 0 && center[3] == 255;
    SDL_UnlockSurface(surface.get());
    CHECK(centerMatches);
    CHECK(!renderer->drawMesh(*texture, vertices, {0, 1, 9}, error));
    const uint8_t blue[] = {0, 0, 255, 128};
    const auto translucent = renderer->texture(1, 1, blue, sizeof blue, error);
    const std::vector<RenderVertex> quad{
        {0, 0, 0, 0}, {0, 8, 0, 1}, {8, 0, 1, 0}, {8, 8, 1, 1}};
    CHECK(translucent && renderer->clear(255, 0, 0, 255, error) &&
          renderer->drawMesh(*translucent, quad, {0, 1, 2, 3, 2, 1}, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *blend = static_cast<const uint8_t *>(surface->pixels) +
                        2 * surface->pitch + 2 * 4;
    const bool blendOk = blend[0] >= 126 && blend[0] <= 128 && blend[1] == 0 &&
                         blend[2] >= 127 && blend[2] <= 129;
    SDL_UnlockSurface(surface.get());
    CHECK(blendOk);
    const uint8_t colors[] = {255, 0, 0,   255, 0,   255, 0,   255,
                              0,   0, 255, 255, 255, 255, 255, 255};
    const auto colorTexture =
        renderer->texture(2, 2, colors, sizeof colors, error);
    CHECK(colorTexture && renderer->clear(0, 0, 0, 255, error) &&
          renderer->drawMesh(*colorTexture, quad, {0, 1, 2, 3, 2, 1}, error) &&
          renderer->present(error));
    CHECK(SDL_LockSurface(surface.get()));
    const auto *upper =
        static_cast<const uint8_t *>(surface->pixels) + surface->pitch + 4;
    const bool uvOk = upper[0] == 255 && upper[1] == 0 && upper[2] == 0;
    SDL_UnlockSurface(surface.get());
    CHECK(uvOk);
    auto nonPlanar = original;
    nonPlanar.positions[2] = std::numeric_limits<float>::infinity();
    CHECK(!sceneProjectMesh(nonPlanar, projection, vertices, error) &&
          vertices.empty());
    CHECK(!sceneRefreshMaterial(resources, *clone, "ref/no-such-root", cache,
                                error));
    CHECK(cache.texture.rgba.empty() && cache.texture.width == 0 &&
          cache.meshId == 0 && cache.textureId == 0);
    CHECK(sceneRefreshMaterial(resources, *clone, "ref/raw", cache, error));
    CHECK(clone->replace(meshId, nullptr, [] {}));
    CHECK(!sceneRefreshMaterial(resources, *clone, "ref/raw", cache, error));
    CHECK(cache.meshId == 0 && cache.mesh.meshes.empty() &&
          cache.texture.rgba.empty());
    const uint32_t oldId = clonedNode->instanceId;
    std::weak_ptr<SceneMaterialResources> lifetime = clone;
    clone.reset();
    CHECK(!lifetime.expired());
    {
      ResourceWorld secondWorld(world, registry);
      CHECK(secondWorld.bindSceneMaterial(
          clonedResource, resources.sceneMaterial(clonedResource)));
      CHECK(secondWorld.sceneMaterial(clonedResource));
    }
    CHECK(world.destroy(*clonedNode));
    CHECK(lifetime.expired());
    CHECK(!resources.sceneMaterial(clonedResource));
    const auto parent = resources.resolve(
        world.parentOwner(materialNode.node.packNode->handle));
    CHECK(parent.packNode);
    auto *reused = world.addTypedChildUnderExternal(
        parent.packNode->handle, *parent.owner, parent.type,
        parent.packNode->sectorLink.owner, 14, "", 0);
    CHECK(reused && reused->instanceId == oldId);
    const auto reusedResource = resources.resolve(reused);
    CHECK(!resources.sceneMaterial(reusedResource));
    CHECK(world.destroy(*reused));
  }
  CHECK(!sceneLoadMaterial(resources, 0).material);
  uint32_t notMaterial = 0;
  CHECK(registry.resolveLiteral("frontend.rpk", 0xa8, notMaterial, error));
  CHECK(!sceneLoadMaterial(resources, notMaterial).material);
  {
    GameWorld teardownWorld;
    CHECK(teardownWorld.addRoot());
    std::weak_ptr<SceneMaterialResources> cached;
    {
      ResourceWorld teardownResources(teardownWorld, registry);
      const auto node = teardownResources.lookup(id, 14);
      auto loaded = sceneLoadMaterial(teardownResources, id);
      CHECK(node.status == LookupStatus::Found && loaded.material);
      auto owned =
          std::shared_ptr<SceneMaterialResources>(std::move(loaded.material));
      cached = owned;
      CHECK(teardownResources.bindSceneMaterial(node.node, owned));
      owned.reset();
      CHECK(!cached.expired());
    }
    CHECK(cached.expired());
  }
  // FUN_00541400/FUN_00531e90: humans.rpk materials declare a skeleton file;
  // loading parses shared bind bones and each render instance gets its own
  // pose, while unskinned materials keep a null pose (single-bone path).
  {
    GameWorld skeletonWorld;
    CHECK(skeletonWorld.addRoot());
    ResourceWorld skeletonResources(skeletonWorld, registry);
    uint32_t skeletonMaterialId = 0;
    uint32_t candidateCount = 0;
    {
      uint16_t humansSlot = 0;
      CHECK(registry.registerPackName("humans.rpk", humansSlot, error));
      PackView humansView;
      CHECK(registry.view(humansSlot, humansView, error) ==
            PackViewStatus::Ok);
      candidateCount =
          static_cast<uint32_t>(humansView.pack->entries.size());
    }
    for (uint32_t local = 1; local <= candidateCount && !skeletonMaterialId;
         ++local) {
      uint32_t resolved = 0;
      if (!registry.resolveLiteral("humans.rpk", local, resolved, error) ||
          !resolved)
        continue;
      PackResource resource;
      if (!registry.lookup(resolved, resource, error) || !resource.packBytes ||
          resource.entry.resType != 14)
        continue;
      const std::string payload(
          reinterpret_cast<const char*>(resource.packBytes->data()) +
              resource.entry.payloadOffset,
          resource.entry.payloadSize);
      if (payload.find("skeleton") != std::string::npos)
        skeletonMaterialId = resolved;
    }
    CHECK(skeletonMaterialId);
    auto loaded = sceneLoadMaterial(skeletonResources, skeletonMaterialId);
    CHECK(loaded.material && loaded.material->skeletonBoneCount() > 0);
    // Real skl records are not root-first (humans/drivers/vin.skl starts at
    // "Bip01 Spine" parent 3); only the hierarchy itself is a fixed invariant.
    const auto& bindBones = loaded.material->skeletonBones();
    CHECK(bindBones.size() > 1);
    CHECK(std::count_if(bindBones.begin(), bindBones.end(),
                        [](const SkeletonBone& bone) {
                          return bone.parent < 0;
                        }) == 1);
    CHECK(std::all_of(bindBones.begin(), bindBones.end(),
                      [&](const SkeletonBone& bone) {
                        return bone.parent < 0 ||
                               static_cast<size_t>(bone.parent) <
                                   bindBones.size();
                      }));
    const auto root = std::find_if(bindBones.begin(), bindBones.end(),
                                   [](const SkeletonBone& bone) {
                                     return bone.parent < 0;
                                   });
    CHECK(root != bindBones.end());
    const auto materialNode =
        skeletonResources.lookup(skeletonMaterialId, 14);
    CHECK(materialNode.status == LookupStatus::Found);
    auto owned =
        std::shared_ptr<SceneMaterialResources>(std::move(loaded.material));
    CHECK(skeletonResources.bindSceneMaterial(materialNode.node, owned));
    SceneRenderInstance instance(materialNode.node.owner);
    std::string skeletonError;
    CHECK(sceneEnsureSkeleton(skeletonResources, instance, skeletonError));
    const auto& pose = instance.skeletonPose();
    CHECK(pose && pose->palette().size() == owned->skeletonBoneCount());
    CHECK(pose && pose->findBone(root->name) ==
                      static_cast<int>(root - bindBones.begin()));
    CHECK(pose && pose->findBone(bindBones.front().name) == 0);
    // A second instance owns an independent pose over the same bind bones.
    SceneRenderInstance second(materialNode.node.owner);
    CHECK(sceneEnsureSkeleton(skeletonResources, second, skeletonError));
    CHECK(second.skeletonPose() && second.skeletonPose() != pose);
    // Unskinned frontend material: ensure succeeds with a null pose.
    uint32_t plainId = 0;
    CHECK(registry.resolveLiteral("frontend.rpk", 0xd7, plainId, error));
    auto plain = sceneLoadMaterial(skeletonResources, plainId);
    CHECK(plain.material && !plain.material->skeletonBoneCount());
    const auto plainNode = skeletonResources.lookup(plainId, 14);
    CHECK(plainNode.status == LookupStatus::Found);
    auto plainOwned =
        std::shared_ptr<SceneMaterialResources>(std::move(plain.material));
    CHECK(skeletonResources.bindSceneMaterial(plainNode.node, plainOwned));
    SceneRenderInstance plainInstance(plainNode.node.owner);
    CHECK(sceneEnsureSkeleton(skeletonResources, plainInstance, skeletonError));
    CHECK(!plainInstance.skeletonPose());
  }
  std::puts("render kind, pass, mesh scaling and resource replacement: ok");
}
