#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "natives/resource_ref.hpp"
#include "render2d.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"
#include "vm_test_harness.hpp"

#include <SDL3/SDL.h>

#include <memory>

using namespace vm_test;

int main() {
  Cb mainClass("test.Main");
  Cb resourceClass("java.util.resource.ResourceRef");
  Cb renderClass("java.util.resource.RenderRef",
                 "java.util.resource.ResourceRef");
  Cb vectorClass("java.lang.Vector3");
  Cb orientationClass("java.lang.Ypr");
  const std::string descriptor =
      "(Ljava.util.resource.ResourceRef;Ljava.util.resource.ResourceRef;)V";
  renderClass.method("changeResource", descriptor, 0x40, {});
  resourceClass.method("scaleMesh", "(FFF)V", 0x40, {});
  const std::string createDescriptor =
      "(Ljava.util.resource.ResourceRef;Ljava.util.resource.RenderRef;Ljava."
      "lang.String;)V";
  const std::string matrixDescriptor = "(Ljava.lang.Vector3;Ljava.lang.Ypr;)V";
  renderClass.method("create", createDescriptor, 0x40, {});
  renderClass.method("setMatrix", matrixDescriptor, 0x40, {});
  renderClass.method("getTypeID", "()I", 0x40, {});
  renderClass.method("setLight", "(III)V", 0x40, {});
  const auto typeMethod =
      mainClass.ref("java.util.resource.RenderRef", "getTypeID", "()I");
  const auto createMethod =
      mainClass.ref("java.util.resource.RenderRef", "create", createDescriptor);
  const auto matrixMethod = mainClass.ref("java.util.resource.RenderRef",
                                          "setMatrix", matrixDescriptor);
  const auto scaleMethod =
      mainClass.ref("java.util.resource.ResourceRef", "scaleMesh", "(FFF)V");
  const auto method = mainClass.ref("java.util.resource.RenderRef",
                                    "changeResource", descriptor);
  std::vector<const TufaClass *> classes{&mainClass.c, &resourceClass.c,
                                         &renderClass.c, &vectorClass.c,
                                         &orientationClass.c};
  PackRegistry packs("ref/raw");
  GameWorld world;
  CHECK(world.addRoot());
  ResourceWorld resources(world, packs);
  VmGameState game{&packs, &world, {}, {}, &resources};
  VmArena arena;
  arena.gameState = &game;
  const auto object = [&](const TufaClass *cls) {
    auto value = std::make_unique<VmObject>();
    value->runtimeClass = cls;
    value->fields.push_back({"ptr", int32_t{0}, "I"});
    arena.objects.push_back(std::move(value));
    return arena.objects.back().get();
  };
  const auto init = [&](const VmObject *value, uint32_t id) {
    const auto made =
        invokeNative("java.util.resource.ResourceRef", "newNative", "()V", {},
                     value, &arena, &classes);
    if (!std::holds_alternative<std::monostate>(made))
      return false;
    int32_t raw;
    std::memcpy(&raw, &id, sizeof raw);
    const auto set =
        invokeNative("java.util.resource.ResourceRef", "set", "(I)V",
                     {VmValue{raw}}, value, &arena, &classes);
    return std::holds_alternative<std::monostate>(set);
  };
  std::string error;
  uint32_t templateId = 0, oldId = 0, newId = 0;
  CHECK(packs.resolveLiteral("frontend.rpk", 0xd7, templateId, error));
  CHECK(packs.resolveLiteral("frontend.rpk", 0xaf, oldId, error));
  CHECK(packs.resolveLiteral("frontend.rpk", 0xa8, newId, error));
  const VmObject *source = object(&resourceClass.c);
  const VmObject *target = object(&renderClass.c);
  const VmObject *oldRef = object(&resourceClass.c);
  const VmObject *newRef = object(&resourceClass.c);
  CHECK(init(source, templateId) && init(target, 0) && init(oldRef, oldId) &&
        init(newRef, newId));
  const auto clone = invokeNative("java.util.resource.ResourceRef", "duplicate",
                                  "(Ljava.util.resource.ResourceRef;)V",
                                  {VmValue{source}}, target, &arena, &classes);
  CHECK(std::holds_alternative<std::monostate>(clone));
  const auto id = nativeResourceState(target, &arena)->words[2];
  const auto nodeRef = resources.lookup(id, 14);
  CHECK(nodeRef.status == LookupStatus::Found);
  auto material = resources.sceneMaterial(nodeRef.node);
  CHECK(material && material->textureId(0) == oldId);
  int refreshed = 0;
  CHECK(resources.setSceneMaterialRefresh(nodeRef.node, [&] { ++refreshed; }));
  const std::vector<TufaNode> tree{node(0x01, true, 1),
                                   node(0x01, true, 2),
                                   literal(2),
                                   node(0x24),
                                   node(0x11, true, 2),
                                   node(0x01, true, 0),
                                   node(0x1a, true, method),
                                   retVoid};
  const auto result = vmExecute(mainClass.c, tree, {target, oldRef, newRef},
                                &nativeRegistry(), &classes, nullptr, &arena);
  if (!result.ok)
    std::fprintf(stderr, "VM: %s\n", result.error.c_str());
  CHECK(result.ok && result.voidReturn);
  CHECK(material && material->textureId(0) == newId && refreshed == 1);
  CHECK(resources.setSceneMaterialRefresh(nodeRef.node, {}));
  uint32_t meshId = 0;
  CHECK(packs.resolveLiteral("frontend.rpk", 0xd9, meshId, error));
  const VmObject *meshSource = object(&resourceClass.c);
  const VmObject *meshTarget = object(&resourceClass.c);
  CHECK(init(meshSource, meshId) && init(meshTarget, 0));
  const auto meshClone =
      invokeNative("java.util.resource.ResourceRef", "duplicate",
                   "(Ljava.util.resource.ResourceRef;)V", {VmValue{meshSource}},
                   meshTarget, &arena, &classes);
  CHECK(std::holds_alternative<std::monostate>(meshClone));
  const auto cloneId = nativeResourceState(meshTarget, &arena)->words[2];
  const auto meshNode = resources.lookup(cloneId, 5);
  CHECK(meshNode.status == LookupStatus::Found && cloneId != meshId);
  auto clonedMesh = resources.sceneMesh(meshNode.node);
  ScxModel sourceMesh;
  CHECK(sceneLoadMesh(resources, meshId, packs.root(), sourceMesh, error));
  const auto floatNode = [](float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return node(0x0a, true, bits);
  };
  const std::vector<TufaNode> scaleTree{node(0x07, true, 28),
                                        floatNode(2),
                                        node(0x07, true, 28),
                                        floatNode(3),
                                        node(0x07, true, 28),
                                        floatNode(1),
                                        literal(3),
                                        node(0x24),
                                        node(0x11, true, 2),
                                        node(0x01, true, 0),
                                        node(0x1a, true, scaleMethod),
                                        retVoid};
  const auto scaled = vmExecute(mainClass.c, scaleTree, {meshTarget},
                                &nativeRegistry(), &classes, nullptr, &arena);
  if (!scaled.ok)
    std::fprintf(stderr, "VM scale: %s\n", scaled.error.c_str());
  CHECK(scaled.ok && scaled.voidReturn);
  CHECK(clonedMesh && !clonedMesh->meshes.empty() &&
        !sourceMesh.meshes.empty());
  if (clonedMesh && !clonedMesh->meshes.empty() && !sourceMesh.meshes.empty()) {
    const auto &original = sourceMesh.meshes[0];
    const auto &scaledMesh = clonedMesh->meshes[0];
    CHECK(scaledMesh.positions[0] == original.positions[0] * 2 &&
          scaledMesh.positions[1] == original.positions[1] * 3 &&
          scaledMesh.normals == original.normals &&
          scaledMesh.uv1 == original.uv1);
  }
  SceneMaterialCache cache;
  bool cacheOk = false;
  CHECK(resources.setSceneMaterialRefresh(nodeRef.node, [&] {
    cacheOk =
        sceneRefreshMaterial(resources, *material, packs.root(), cache, error);
  }));
  const auto replaceMesh =
      vmExecute(mainClass.c, tree, {target, meshSource, meshTarget},
                &nativeRegistry(), &classes, nullptr, &arena);
  if (!replaceMesh.ok)
    std::fprintf(stderr, "VM replace mesh: %s\n", replaceMesh.error.c_str());
  CHECK(replaceMesh.ok && replaceMesh.voidReturn && cacheOk);
  CHECK(material->meshId() == cloneId && cache.meshId == cloneId &&
        !cache.mesh.meshes.empty());
  if (!cache.mesh.meshes.empty() && !sourceMesh.meshes.empty())
    CHECK(cache.mesh.meshes[0].positions[0] ==
              sourceMesh.meshes[0].positions[0] * 2 &&
          cache.mesh.meshes[0].positions[1] ==
              sourceMesh.meshes[0].positions[1] * 3);
  uint32_t parentId = 0;
  CHECK(packs.resolveLiteral("system.rpk", 4, parentId, error));
  const VmObject *parentRef = object(&resourceClass.c);
  const VmObject *renderTarget = object(&renderClass.c);
  CHECK(init(parentRef, parentId) && init(renderTarget, 0));
  const std::vector<TufaNode> createTree{node(0x01, true, 1),
                                         node(0x01, true, 2),
                                         node(0x01, true, 3),
                                         literal(3),
                                         node(0x24),
                                         node(0x11, true, 2),
                                         node(0x01, true, 0),
                                         node(0x1a, true, createMethod),
                                         retVoid};
  const auto created = vmExecute(
      mainClass.c, createTree,
      {renderTarget, parentRef, target, static_cast<const VmObject *>(nullptr)},
      &nativeRegistry(), &classes, nullptr, &arena);
  CHECK(!created.ok &&
        created.error.find("renderer object creation unsupported") !=
            std::string::npos);
  CHECK(nativeResourceState(renderTarget, &arena)->words[2] == 0);
  const auto parentNode = resources.lookup(parentId, 0);
  CHECK(parentNode.node.packNode && parentNode.node.owner);
  CHECK(resources.loadEmptyGameParent(*parentNode.node.packNode, error));
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
      SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  CHECK(surface);
  auto renderer = Render2D::forSurface(surface.get(), error);
  CHECK(renderer);
  game.renderer = renderer.get();
  world.setFrameCounter(1);
  CHECK(std::holds_alternative<std::monostate>(
      cacheDrawMaterial(resources, *renderer, nodeRef.node)));
  const auto cachedTexture = resources.lookup(newId, 7);
  CHECK(cachedTexture.node.packNode && cachedTexture.node.packNode->texture);
  const auto createdWithRenderer = vmExecute(
      mainClass.c, createTree,
      {renderTarget, parentRef, target, static_cast<const VmObject *>(nullptr)},
      &nativeRegistry(), &classes, nullptr, &arena);
  if (!createdWithRenderer.ok)
    std::fprintf(stderr, "VM render create: %s\n",
                 createdWithRenderer.error.c_str());
  CHECK(createdWithRenderer.ok && createdWithRenderer.voidReturn);
  const auto *renderState = nativeResourceState(renderTarget, &arena);
  const auto renderHandle =
      static_cast<int32_t>(renderState - arena.resourceStates.data()) + 1;
  auto &renderBinding = game.bindings[renderHandle];
  CHECK(renderBinding && renderBinding->link.owner);
  const auto renderNode = resources.resolve(renderBinding->link.owner);
  auto instance = resources.sceneRenderInstance(renderNode);
  CHECK(renderNode.worldNode && instance && instance->typeId() == id &&
        world.parentOwner(renderNode.worldNode->handle) ==
            parentNode.node.owner);
  const VmObject *setOnlyType = object(&renderClass.c);
  const VmObject *setOnlyRender = object(&renderClass.c);
  CHECK(init(setOnlyType, 0) && init(setOnlyRender, 0));
  int32_t rawTemplateId = 0;
  std::memcpy(&rawTemplateId, &templateId, sizeof rawTemplateId);
  const auto linkedType =
      invokeNative("java.util.resource.ResourceRef", "set", "(I)V",
                   {VmValue{rawTemplateId}}, setOnlyType, &arena, &classes);
  CHECK(std::holds_alternative<std::monostate>(linkedType));
  const auto setOnlyCreate =
      vmExecute(mainClass.c, createTree,
                {setOnlyRender, parentRef, setOnlyType,
                 static_cast<const VmObject *>(nullptr)},
                &nativeRegistry(), &classes, nullptr, &arena);
  if (!setOnlyCreate.ok)
    std::fprintf(stderr, "VM set-only render create: %s\n",
                 setOnlyCreate.error.c_str());
  CHECK(setOnlyCreate.ok && setOnlyCreate.voidReturn);
  const std::vector<TufaNode> typeTree{literal(0),
                                       node(0x24),
                                       node(0x11, true, 2),
                                       node(0x01, true, 0),
                                       node(0x1a, true, typeMethod),
                                       ret};
  const auto typeId = vmExecute(mainClass.c, typeTree, {renderTarget},
                                &nativeRegistry(), &classes, nullptr, &arena);
  CHECK(typeId.ok &&
        static_cast<uint32_t>(std::get<int32_t>(typeId.value)) == id);
  const auto materialTypeId =
      vmExecute(mainClass.c, typeTree, {target}, &nativeRegistry(), &classes,
                nullptr, &arena);
  CHECK(materialTypeId.ok && std::get<int32_t>(materialTypeId.value) == 0);
  const VmObject *emptyType = object(&renderClass.c);
  const VmObject *emptyTarget = object(&renderClass.c);
  const VmObject *emptyParentRef = object(&resourceClass.c);
  auto *emptyParent = world.addChild(*world.defaultParent(), "empty-parent", 0);
  CHECK(emptyParent);
  world.markLoaded(*emptyParent);
  CHECK(init(emptyType, 0) && init(emptyTarget, 0) &&
        init(emptyParentRef, emptyParent->value));
  const auto emptyCreate =
      vmExecute(mainClass.c, createTree,
                {emptyTarget, emptyParentRef, emptyType,
                 static_cast<const VmObject *>(nullptr)},
                &nativeRegistry(), &classes, nullptr, &arena);
  if (!emptyCreate.ok)
    std::fprintf(stderr, "VM empty render create: %s\n",
                 emptyCreate.error.c_str());
  CHECK(emptyCreate.ok && emptyCreate.voidReturn);
  const auto emptyId = nativeResourceState(emptyTarget, &arena)->words[2];
  const auto emptyNode = resources.lookup(emptyId, 3);
  CHECK(emptyNode.status == LookupStatus::Found && emptyNode.node.worldNode);
  CHECK(resources.sceneRenderInstance(emptyNode.node) &&
        resources.sceneRenderInstance(emptyNode.node)->typeId() == 0);
  CHECK(emptyNode.node.worldNode->bounds.mode == emptyParent->bounds.mode);
  SceneCameraRenderer emptyCamera(resources, *renderer);
  emptyNode.node.worldNode->field6c = 1;
  emptyNode.node.worldNode->field70 = 0;
  float emptyScore = 0;
  CHECK(emptyCamera.candidates().submit(*emptyNode.node.worldNode, 0,
                                        emptyScore,
                                        true) == SceneCandidateResult::Queued);
  const std::array<float, 16> identity{
      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  const ViewportInstance emptyViewport{0, 0, 0, 1, 1, ViewportList::Active, {}};
  CHECK(renderer->clear(0, 0, 0, 255, error));
  CHECK(emptyCamera.draw(identity, identity, emptyViewport, error) &&
        renderer->present(error));
  Uint8 er, eg, eb, ea;
  CHECK(SDL_ReadSurfacePixel(surface.get(), 64, 64, &er, &eg, &eb, &ea) &&
        er == 0 && eg == 0 && eb == 0);
  CHECK(emptyCamera.candidates().submit(*emptyNode.node.worldNode, 0,
                                        emptyScore, true) ==
        SceneCandidateResult::AlreadyPresent);
  auto *vector = object(&vectorClass.c);
  vector->fields.push_back({"x", 1.0f, "F"});
  vector->fields.push_back({"y", -2.0f, "F"});
  vector->fields.push_back({"z", 3.0f, "F"});
  auto *rotation = object(&orientationClass.c);
  rotation->fields.push_back({"y", 0.0f, "F"});
  rotation->fields.push_back({"p", 0.0f, "F"});
  rotation->fields.push_back({"r", 0.0f, "F"});
  const std::vector<TufaNode> matrixTree{node(0x01, true, 1),
                                         node(0x01, true, 2),
                                         literal(2),
                                         node(0x24),
                                         node(0x11, true, 2),
                                         node(0x01, true, 0),
                                         node(0x1a, true, matrixMethod),
                                         retVoid};
  const auto matrixResult =
      vmExecute(mainClass.c, matrixTree, {renderTarget, vector, rotation},
                &nativeRegistry(), &classes, nullptr, &arena);
  if (!matrixResult.ok)
    std::fprintf(stderr, "VM matrix: %s\n", matrixResult.error.c_str());
  CHECK(matrixResult.ok && matrixResult.voidReturn);
  CHECK(instance && instance->boneMatrix()[12] == 10 &&
        instance->boneMatrix()[13] == -20 && instance->boneMatrix()[14] == 30);
  CHECK(instance->rootBoneUpdatePending() &&
        instance->derivedRootBoneMatrix()[12] == 0);
  instance->refreshSingleBoneDrawMatrix();
  CHECK(instance->drawMatrix()[12] == 0);
  instance->processRootBoneUpdate();
  CHECK(!instance->rootBoneUpdatePending() &&
        instance->derivedRootBoneMatrix()[12] == 10 &&
        instance->derivedRootBoneMatrix()[13] == -20 &&
        instance->derivedRootBoneMatrix()[14] == 30);
  CHECK(instance->drawMatrix()[12] == 0);
  instance->refreshSingleBoneDrawMatrix();
  CHECK(instance->drawMatrix()[12] == 10 && instance->drawMatrix()[13] == -20 &&
        instance->drawMatrix()[14] == 30);
  instance->processRootBoneUpdate();
  CHECK(instance->derivedRootBoneMatrix()[12] == 10);
  // FUN_0048bf50: a nonzero Ypr builds the stock 0054ecd0 basis (yaw=0,
  // pitch=1, roll=0 here) into the bone-local matrix with position*10.
  rotation->fields[1].value = 1.0f;
  const auto rotated =
      vmExecute(mainClass.c, matrixTree, {renderTarget, vector, rotation},
                &nativeRegistry(), &classes, nullptr, &arena);
  CHECK(rotated.ok && rotated.voidReturn);
  const float s1 = static_cast<float>(std::sin(1.0)),
              c1 = static_cast<float>(std::cos(1.0));
  // fields[1] is "y": yaw=1, pitch=0, roll=0 gives the 0054ecd0 basis
  // columns (cy,0,-sy), (0,1,0), (sy,0,cy) with position*10.
  CHECK(instance && instance->boneMatrix()[0] == c1 &&
        instance->boneMatrix()[2] == -s1 && instance->boneMatrix()[5] == 1.0f &&
        instance->boneMatrix()[8] == s1 && instance->boneMatrix()[10] == c1 &&
        instance->boneMatrix()[12] == 10 && instance->boneMatrix()[13] == -20 &&
        instance->boneMatrix()[14] == 30);
  CHECK(instance->rootBoneUpdatePending());
  // Bone selection and parenting are not modeled; do not alter the matrix.
  const auto boneMatrix = invokeNative(
      "java.util.resource.RenderRef", "setMatrix",
      "(ILjava.util.resource.ResourceRef;Ljava.lang.Vector3;Ljava.lang.Ypr;)V",
      {VmValue{int32_t{0}}, VmValue{static_cast<const VmObject *>(nullptr)},
       VmValue{vector}, VmValue{rotation}},
      renderTarget, &arena, &classes);
  CHECK(std::holds_alternative<std::string>(boneMatrix));
  CHECK(instance->boneMatrix()[5] == 1.0f && instance->boneMatrix()[0] == c1 &&
        instance->boneMatrix()[12] == 10);
  rotation->fields[1].value = 0.0f;
  vector->fields[1].value = 0.0f;
  vector->fields[2].value = 0.0f;
  vector->fields[3].value = -100.0f;
  const auto visibleMatrix =
      vmExecute(mainClass.c, matrixTree, {renderTarget, vector, rotation},
                &nativeRegistry(), &classes, nullptr, &arena);
  CHECK(visibleMatrix.ok);
  auto drawObject = SceneMaterialRenderer::create(resources, *renderer,
                                                  *renderNode.worldNode, error);
  CHECK(drawObject);
  const auto camera = makeCameraResource(1, 60, 0.5f, 200, 0, 1, 0, 0);
  const auto cameraProjection =
      cameraProjectionMatrix(updateCameraRendererFields(camera, 1));
  const std::array<float, 16> cameraWorld{
      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  const auto view = cameraViewMatrix(cameraWorld);
  const ViewportInstance viewport{0, 0, 0, 1, 1, ViewportList::Active, {}};
  const bool cameraDraw =
      view && renderer->clear(0, 0, 0, 255, error) &&
      drawObject->draw(*view, cameraProjection, viewport, error) &&
      renderer->present(error);
  if (!cameraDraw)
    std::fprintf(stderr, "VM render draw: %s\n", error.c_str());
  CHECK(cameraDraw);
  CHECK(SDL_LockSurface(surface.get()));
  const auto *center = static_cast<const uint8_t *>(surface->pixels) +
                       64 * surface->pitch + 64 * 4;
  const bool pixel = center[0] != 0 || center[1] != 0 || center[2] != 0;
  SDL_UnlockSurface(surface.get());
  CHECK(pixel);
  drawObject.reset();
  std::weak_ptr<SceneRenderInstance> renderLifetime = instance;
  instance.reset();
  CHECK(!renderLifetime.expired());
  CHECK(world.destroy(*renderNode.worldNode));
  CHECK(renderLifetime.expired() && !resources.sceneRenderInstance(renderNode));

  const auto liveScale =
      vmExecute(mainClass.c, scaleTree, {meshTarget}, &nativeRegistry(),
                &classes, nullptr, &arena);
  CHECK(!liveScale.ok &&
        liveScale.error.find("live renderer update unsupported") !=
            std::string::npos);
  if (clonedMesh && !clonedMesh->meshes.empty() && !sourceMesh.meshes.empty())
    CHECK(clonedMesh->meshes[0].positions[0] ==
          sourceMesh.meshes[0].positions[0] * 2);
  CHECK(resources.setSceneMaterialRefresh(nodeRef.node, {}));
  std::weak_ptr<ScxModel> meshLifetime = clonedMesh;
  clonedMesh.reset();
  CHECK(!meshLifetime.expired() && meshNode.node.worldNode);
  CHECK(world.destroy(*meshNode.node.worldNode));
  CHECK(meshLifetime.expired() && !resources.sceneMesh(meshNode.node));
  CHECK(material->meshId() == 0);
  if (cachedTexture.node.packNode)
    cachedTexture.node.packNode->texture.reset();
  WorldNode *lightNode =
      world.addTypedChild(*world.defaultParent(), 13, "scene-light", 0);
  CHECK(lightNode);
  world.markLoaded(*lightNode);
  const auto resolvedLight = resources.resolve(lightNode);
  CHECK(resources.bindSceneLight(resolvedLight,
                                 SceneRenderLight::stockDefaults()));
  const VmObject *lightRef = object(&renderClass.c);
  CHECK(init(lightRef, lightNode->instanceId));
  const auto setLight =
      invokeNative("java.util.resource.RenderRef", "setLight", "(III)V",
                   {int32_t{0xff0000}, int32_t{0x00ff00}, int32_t{0x0000ff}},
                   lightRef, &arena, &classes);
  CHECK(std::holds_alternative<std::monostate>(setLight));
  const SceneRenderLight *stored = resources.sceneLight(resolvedLight);
  CHECK(stored && stored->at10[0] > 0.9f && stored->at30[1] > 0.9f &&
        stored->at20[2] > 0.9f);
  {
    const std::string payload = "type spot\n"
                                "diffuse 0.5 0.1 0.2 0.3\n"
                                "specular 0.4 0.5 0.6 0.7\n"
                                "ambient 0.8 0.9 1 0.25\n"
                                "position 1 2 3\n"
                                "direction 4 5 6\n"
                                "range 50\nfalloff 2\nattenuation 1 0.5 0.25\n"
                                "theta 0.1\nphi 0.2\nlit_dynamic 0\n"
                                "attH_texture 42\nattV_texture -7\n"
                                "lf_glow_size 4\nlf_glow_color 1 0 0.5 1\n";
    std::vector<uint8_t> data(payload.begin(), payload.end());
    PackResource resource;
    resource.entry.resType = 13;
    resource.entry.payloadSize = data.size();
    resource.packBytes = &data;
    auto parsed = sceneParseLight(resource);
    CHECK(parsed.ok);
    CHECK(parsed.light.sourceKind == 2 && parsed.light.position[2] == 3 &&
          parsed.light.direction[1] == 5 && parsed.light.range == 50);
    CHECK(parsed.light.at10[0] == 0.1f && parsed.light.diffuseAlpha == 0.5f &&
          parsed.light.at20[2] == 0.7f && parsed.light.at30[1] == 1);
    CHECK(parsed.light.falloff == 2 && parsed.light.attenuation[2] == 0.25f &&
          parsed.light.theta == 0.1f && parsed.light.phi == 0.2f &&
          !parsed.light.litDynamic);
    CHECK(parsed.light.attHTextureId == 42 && parsed.light.attVTextureId == -7);
    CHECK(parsed.light.flare && parsed.light.flare->atD8 == 4 &&
          static_cast<uint32_t>(parsed.light.flare->wordE0) == 0xff007fffu);
    const std::string ids =
        "attH_texture 0xffffffff\nattV_texture -2147483648\n"
        "lf_texture -1\n";
    data.assign(ids.begin(), ids.end());
    resource.entry.payloadSize = data.size();
    parsed = sceneParseLight(resource);
    CHECK(parsed.ok && parsed.light.attHTextureId &&
          static_cast<uint32_t>(*parsed.light.attHTextureId) == 0xffffffffu &&
          parsed.light.attVTextureId == INT32_MIN && parsed.light.flare &&
          parsed.light.flare->textureId == -1);
    data.assign({'u', 'n', 'k', 'n', 'o', 'w', 'n', '\n'});
    resource.entry.payloadSize = data.size();
    parsed = sceneParseLight(resource);
    CHECK(!parsed.ok && parsed.error.find("unsupported light directive") !=
                            std::string::npos);
    data.clear();
    resource.entry.payloadSize = 0;
    parsed = sceneParseLight(resource);
    CHECK(parsed.ok && parsed.light.sourceKind == 0 &&
          parsed.light.at20[0] == 1 && parsed.light.attenuation[0] == 1 &&
          parsed.light.falloff == 0 && parsed.light.litDynamic);
  }
  if (failures)
    return 1;
  std::puts("VM render resource change: ok");
}
