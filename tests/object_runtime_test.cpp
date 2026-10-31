#include "camera_traversal.hpp"
#include "car_geometry.hpp"
#include "object_config.hpp"
#include "object_physics.hpp"
#include "object_runtime.hpp"
#include "pack_registry.hpp"
#include "scene_2d.hpp"

#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

static int failures = 0;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::printf("FAIL line %d: %s\n", __LINE__, #c);                         \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

int main() {
  std::string error;
  object_config::InstanceHeader header;
  CHECK(object_config::parseInstanceHeader(
      "-172.736,10.208,755.170,-2.414,0.000,0.000", header, error));
  CHECK(header.id == 592 && header.position.x == -172.736f);
  CHECK(object_config::parseInstanceHeader("1,2,3,4,5,6,-1", header, error));
  CHECK(header.id == 6);
  CHECK(object_config::parseInstanceHeader("-4.9,1,1,0,0,0", header, error));
  CHECK(header.id == 0x7ffffffe);
  CHECK(object_config::parseInstanceHeader("1,2,3,4,5,6,7", header, error));
  CHECK(header.id == 7);
  CHECK(!object_config::parseInstanceHeader("1 2 3 4 5 6", header, error));

  std::vector<object_config::ObjectConfigLine> lines;
  CHECK(object_config::parseObjectConfig(
      "bounds -2 -1 -3 2 6 3\nrender 0x1 0 0 0 0 0 0\n"
      "rendertypes 2 0x1 0xa\nlod 2 2\n"
      "sfx_random 0x55e 10 2 32 4 21\neof\n",
      lines, error));
  object_config::ObjectConfig config;
  ResourceIdMapping mapping{3, 4, {}};
  for (const auto &line : lines)
    CHECK(object_config::applyObjectConfigLine(line, 0, mapping, 0, config,
                                               error));
  CHECK(config.hasBounds && config.bounds.flag == 0);
  CHECK(config.slots.size() == 1);
  if (config.slots.size() == 1) {
    CHECK(config.slots[0].modifiers.hasRendertypes);
    CHECK(config.slots[0].modifiers.rendertypes.size() == 2);
    if (config.slots[0].modifiers.rendertypes.size() == 2)
      CHECK((config.slots[0].modifiers.rendertypes[1] & 0xffffu) == 0xau);
    CHECK(config.slots[0].modifiers.hasLod);
  }
  {
    object_config::ObjectConfig shapes;
    CHECK(object_config::applyObjectConfigLine(
        {"body", "1 2 3 0 0 0 200 objects\\meshes\\kuka_a\\phys_01.scx"}, 0,
        mapping, 0, shapes, error));
    CHECK(object_config::applyObjectConfigLine({"body", "0 0 0 0 0 0 3 0x123"},
                                               0, mapping, 0, shapes, error));
    CHECK(shapes.slots.size() == 2);
    if (shapes.slots.size() == 2) {
      CHECK(shapes.slots[0].shape.kind == object_config::ShapeKind::Mesh);
      CHECK(shapes.slots[0].shape.name ==
            "objects\\meshes\\kuka_a\\phys_01.scx");
      CHECK(shapes.slots[1].shape.kind ==
            object_config::ShapeKind::ReuseRegistry);
      CHECK(shapes.slots[1].shape.meshId == remapResourceId(0x123, mapping));
    }
  }
  {
    object_config::ObjectConfig variants;
    CHECK(object_config::applyObjectConfigLine({"render", "0x1"}, 0, mapping, 0,
                                               variants, error));
    CHECK(object_config::applyObjectConfigLine({"flags", "4"}, 0, mapping, 0,
                                               variants, error));
    CHECK(object_config::applyObjectConfigLine({"flags", "64"}, 0, mapping, 0,
                                               variants, error));
    CHECK(variants.slots[0].modifiers.flags == 0x44);
    CHECK(object_config::applyObjectConfigLine(
        {"rendertypes", "4 10 0x11 bad 0x22"}, 0, mapping, 0, variants, error));
    const auto &values = variants.slots[0].modifiers.rendertypes;
    CHECK(values.size() == 4 && (values[0] & 0xffffu) == 10 &&
          (values[1] & 0xffffu) == 17 && values[2] == values[1] &&
          values[3] == values[1]);
    CHECK(object_config::applyObjectConfigLine({"rendertypes", "3 nonsense"}, 0,
                                               mapping, 0, variants, error));
    CHECK(variants.slots[0].modifiers.rendertypes ==
          std::vector<uint32_t>({0, 0, 0}));
    std::string text = "rendertypes 30";
    for (int i = 0; i < 6; ++i)
      text += " 0x000000ab";
    for (int i = 0; i < 24; ++i)
      text += " 0x00000000";
    text += "\n";
    std::vector<object_config::ObjectConfigLine> truncated;
    CHECK(object_config::parseObjectConfig(text, truncated, error));
    CHECK(truncated.size() == 1);
    if (!truncated.empty())
      CHECK(object_config::applyObjectConfigLine(truncated[0], 0, mapping, 0,
                                                 variants, error));
    const auto &longValues = variants.slots[0].modifiers.rendertypes;
    CHECK(longValues.size() == 30);
    if (longValues.size() == 30) {
      CHECK((longValues[0] & 0xffffu) == 0xabu);
      for (size_t i = 6; i < longValues.size(); ++i)
        CHECK(longValues[i] == 0);
    }
  }
  {
    object_config::ObjectConfig palette;
    CHECK(object_config::applyObjectConfigLine({"render", "0x1"}, 0, mapping, 0,
                                               palette, error));
    std::vector<object_config::ObjectConfigLine> parsed;
    const std::string line =
        "colors 29 0xdbc4c9 0xc2c2d8 0xbbd9bc 0xdecfb8 0xc7dcb2 "
        "0xdcdda6 0xbac2a8 0xb6a299 0xb09a9b 0xb4b3c2 0x9daba4 "
        "0xb9cab4 0xaeaea5 0x8b7575 0x808184 0x8e8681 0x7b7a8a "
        "0x7a8f76 0x8f9683 0x929989 0x646483 0x696b83 0x4a4d7f "
        "0x8e906e 0x6e743b 0x8e836c 0x8a6756 0x7d5938 0x8b3b34\n";
    CHECK(object_config::parseObjectConfig(line, parsed, error));
    if (!parsed.empty())
      CHECK(object_config::applyObjectConfigLine(parsed[0], 0, mapping, 0,
                                                 palette, error));
    const auto &colors = palette.slots[0].modifiers.colors;
    CHECK(colors.size() == 29);
    if (colors.size() == 29)
      CHECK(colors[21] == 0x696b83u && colors[26] == 0x8a6756u && !colors[27] &&
            !colors[28]);
    CHECK(object_config::applyObjectConfigLine({"colors", "3 10 0x20 bogus"}, 0,
                                               mapping, 0, palette, error));
    const auto &shortColors = palette.slots[0].modifiers.colors;
    CHECK(shortColors.size() == 3 && shortColors[0] == 10u &&
          shortColors[1] == 0x20u && !shortColors[2]);
  }
  {
    object_config::ObjectConfig materials;
    CHECK(object_config::applyObjectConfigLine(
        {"material", "0 0 0x0000056b 500"}, 0, mapping, 3, materials, error));
    CHECK(materials.materials.size() == 1);
    if (!materials.materials.empty()) {
      const auto &m = materials.materials[0];
      CHECK(m.rpkId == 0 && m.materialId == 0 && m.detailId == 0x0003056bu &&
            m.value == 500.0f);
    }
    CHECK(object_config::applyObjectConfigLine({"material", ""}, 0, mapping, 3,
                                               materials, error));
    CHECK(materials.materials.size() == 2 &&
          materials.materials[1].rpkId == 0x00030003u &&
          materials.materials[1].detailId == 0);
    CHECK(object_config::applyObjectConfigLine(
        {"material", "10 bad 0x56b 500"}, 0, mapping, 3, materials, error));
    CHECK(materials.materials.size() == 3 &&
          materials.materials[2].rpkId == 0x0003000au &&
          materials.materials[2].materialId == 0 &&
          materials.materials[2].detailId == 0 &&
          materials.materials[2].value == 0);
  }
  {
    PackRegistry packs("ref/raw");
    GameWorld world;
    auto *root = world.addRoot();
    ResourceWorld resources(world, packs);
    auto *generic = world.addTypedChild(*root, 14, "generic", 0);
    uint16_t frontend = 0;
    CHECK(packs.registerPackName("frontend.rpk", frontend, error));
    const auto firstResult =
        resources.lookup((uint32_t(frontend) << 16) | 0x79, 6);
    const auto impactResult =
        resources.lookup((uint32_t(frontend) << 16) | 0x7a, 6);
    auto *first = firstResult.node.packNode;
    auto *impact = impactResult.node.packNode;
    CHECK(generic && first && impact);
    if (generic && first && impact) {
      auto *genericHead = generic->head;
      auto *firstHead = first->head;
      auto *impactHead = impact->head;
      object::ObjectRuntime runtime({}, object::ObjectPhysics::create());
      object_config::MaterialDirective material{generic->value, first->value,
                                                impact->value, 500.0f};
      CHECK(object::bindObjectMaterial(resources, material, runtime, error));
      CHECK(runtime.material && runtime.material->resource.owner == generic &&
            runtime.material->collisionSound.owner == first &&
            runtime.material->impactSound.owner == impact &&
            runtime.material->threshold == 500.0f);
      CHECK(generic->head == &runtime.material->resource &&
            first->head == &runtime.material->collisionSound &&
            impact->head == &runtime.material->impactSound);
      CHECK(object::bindObjectMaterial(resources, {}, runtime, error));
      CHECK(generic->head == genericHead && first->head == firstHead &&
            impact->head == impactHead && runtime.material &&
            runtime.material->threshold == 0);
    }
  }
  CHECK(config.sfxRandom.size() == 1);
  if (config.sfxRandom.size() == 1) {
    CHECK((config.sfxRandom[0].resourceId & 0xffffu) == 0x55eu);
    CHECK(config.sfxRandom[0].param2 == 14400);
    CHECK(config.sfxRandom[0].param3 == 75600);
  }
  object_config::ObjectConfig soundDefaults;
  CHECK(object_config::applyObjectConfigLine({"sfx_random", "0x55e"}, 0,
                                             mapping, 0, soundDefaults, error));
  CHECK(soundDefaults.sfxRandom.size() == 1);
  if (soundDefaults.sfxRandom.size() == 1)
    CHECK(soundDefaults.sfxRandom[0].param3 == 86400);

  // 004650d0 stores the second damage value, substituting the first for
  // zero or an omitted second argument. 00465bd0 copies it into the live
  // instance rather than sharing mutable health between instances.
  {
    object_config::ObjectConfig damage;
    CHECK(damage.damage.capacity == -1.0f && damage.damage.root == -1.0f);
    CHECK(object_config::applyObjectConfigLine({"damage", "12"}, 0, mapping, 0,
                                               damage, error));
    CHECK(damage.damage.capacity == 12.0f && damage.damage.root == 12.0f);
    CHECK(object_config::applyObjectConfigLine({"damage", "20 0"}, 0, mapping,
                                               0, damage, error));
    CHECK(damage.damage.capacity == 20.0f && damage.damage.root == 20.0f);
    CHECK(object_config::applyObjectConfigLine({"damage", "20 7"}, 0, mapping,
                                               0, damage, error));
    CHECK(damage.damage.capacity == 20.0f && damage.damage.root == 7.0f);
    object::ObjectRuntime first(damage, object::ObjectPhysics::create());
    object::ObjectRuntime second(damage, object::ObjectPhysics::create());
    CHECK(first.damageRemaining == 7.0f && second.damageRemaining == 7.0f);
    first.damageRemaining = 3.0f;
    CHECK(second.damageRemaining == 7.0f && damage.damage.root == 7.0f);
    CHECK(!object_config::applyObjectConfigLine({"damage", "nan"}, 0, mapping,
                                                0, damage, error));
  }

  object_config::ObjectConfig lodConfig;
  CHECK(object_config::applyObjectConfigLine({"render", "0x1"}, 0, mapping, 0,
                                             lodConfig, error));
  CHECK(lodConfig.defaultSlotIndex == -1);
  CHECK(lodConfig.slots[0].modifiers.lodNear == 0 &&
        lodConfig.slots[0].modifiers.lodFar == 15);
  CHECK(object_config::applyObjectConfigLine(
      {"body", "0 0 0 0 0 0 1 box 1 1 1"}, 0, mapping, 0, lodConfig, error));
  CHECK(lodConfig.defaultSlotIndex == 1);
  CHECK(object_config::applyObjectConfigLine({"lods", "2 0.01 0.025"}, 0,
                                             mapping, 0, lodConfig, error));
  CHECK(lodConfig.lods.thresholds.size() == 3 &&
        lodConfig.lods.thresholds[2] == 1.0f);
  CHECK(object_config::applyObjectConfigLine({"lods", "2 0.1 2"}, 0, mapping, 0,
                                             lodConfig, error));
  CHECK(lodConfig.lods.thresholds.size() == 2 &&
        lodConfig.lods.thresholds[1] == 2.0f);
  CHECK(object_config::applyObjectConfigLine({"lods", "1 0.2"}, 1, mapping, 0,
                                             lodConfig, error));
  CHECK(lodConfig.lods.thresholds.size() == 2 &&
        lodConfig.lods.thresholds[1] == 2.0f);
  {
    PackRegistry packs("ref/raw");
    GameWorld world;
    auto *root = world.addRoot();
    ResourceWorld resources(world, packs);
    ResourceNode objectNode;
    objectNode.type = 1;
    objectNode.value = 123;
    auto handle = world.attachExternal(root->handle, objectNode);
    CHECK(handle.has_value());
    if (handle) {
      objectNode.handle = *handle;
      auto *typeA = world.addTypedChild(*root, 14, "a", 0);
      auto *typeB = world.addTypedChild(*root, 14, "b", 0);
      CHECK(typeA && typeB);
      if (typeA && typeB) {
        for (auto *type : {typeA, typeB})
          CHECK(resources.bindSceneMaterial(
              resources.resolve(type),
              std::make_shared<SceneMaterialResources>(
                  nullptr, std::vector<LinkOwner *>{})));
        object_config::ObjectConfig c;
        c.instance.position = {10, 20, 30};
        c.instance.id = 1;
        c.hasLods = true;
        c.lods.thresholds = {0.1f, 0.5f, 1.0f};
        c.slots.resize(2);
        for (auto &slot : c.slots)
          slot.kind = object_config::SlotKind::Render;
        c.slots[0].position = {1, 0, 0};
        c.slots[0].modifiers.lodFar = 0;
        c.slots[0].modifiers.colors = {0x112233u, 0x696b83u};
        c.slots[1].modifiers.lodNear = 1;
        c.slots[1].modifiers.lodFar = 2;
        c.slots[1].modifiers.colors = {std::nullopt};
        auto runtime = std::make_shared<object::ObjectRuntime>(
            std::move(c), object::ObjectPhysics::create());
        for (int i = 0; i < 2; ++i)
          runtime->resources.push_back(
              std::make_unique<object::ObjectRuntime::SlotResources>());
        for (auto *type : {typeA, typeB}) {
          auto link = std::make_unique<OwnerLink>();
          setLinkOwner(*link, type);
          runtime->resources[0]->variants.push_back(std::move(link));
        }
        setLinkOwner(runtime->resources[1]->primary, typeA);
        objectNode.objectRuntime = runtime;
        CHECK(object::updateObjectLod(resources, objectNode, 0, error));
        CHECK(runtime->lod == 0 && runtime->renders.size() == 1);
        if (runtime->renders.size() == 1) {
          auto *child = world.nodeFor(runtime->renders[0]->node.owner);
          auto scene =
              child ? resources.sceneRenderInstance(resources.resolve(child))
                    : nullptr;
          CHECK(scene && scene->typeId() == typeB->value);
          if (scene)
            CHECK(scene->boneMatrix()[12] == 110.0f);
          if (scene)
            CHECK(scene->instanceColor() == 0x696b83u);
        }
        // A colors hole (nullopt) no longer fails the slot: stock's colors
        // handler (FUN_004656d0) reads the truncated entry uninitialized and
        // keeps rendering, so the port renders it with color 0 instead.
        CHECK(object::updateObjectLod(resources, objectNode, 0.1f, error));
        CHECK(runtime->lod == 1 && runtime->renders.size() == 1 &&
              runtime->renders[0]->slot == 1);
        {
          auto *child = world.nodeFor(runtime->renders[0]->node.owner);
          auto scene =
              child ? resources.sceneRenderInstance(resources.resolve(child))
                    : nullptr;
          CHECK(scene && scene->instanceColor() == 0u);
        }
        runtime->config.slots[1].modifiers.colors[0] = 0xabcdefu;
        CHECK(object::updateObjectLod(resources, objectNode, 0.1f, error));
        CHECK(runtime->lod == 1 && runtime->renders.size() == 1 &&
              runtime->renders[0]->slot == 1);
        auto *retained = runtime->renders.empty()
                             ? nullptr
                             : runtime->renders[0]->node.owner;
        CHECK(object::updateObjectLod(resources, objectNode, 0.5f, error));
        CHECK(runtime->lod == 2 && runtime->renders.size() == 1 &&
              runtime->renders[0]->node.owner == retained);
        CHECK(object::updateObjectLod(resources, objectNode, 0.0f, error));
        CHECK(runtime->renders.size() == 1 && runtime->renders[0]->slot == 0);
        CHECK(!object::updateObjectLod(resources, objectNode,
                                       std::numeric_limits<float>::infinity(),
                                       error));
        CHECK(runtime->lod == 0 && runtime->renders.size() == 1);
        CHECK(world.detachExternal(*handle));
        CHECK(!runtime->renders.empty() && !runtime->renders[0]->node.owner);
        objectNode.objectRuntime.reset();
      }
    }
  }
  {
    PackRegistry packs("ref/raw");
    GameWorld world;
    auto *root = world.addRoot();
    ResourceWorld resources(world, packs);
    ResourceNode node;
    node.type = 1;
    node.value = 456;
    const auto handle = world.attachExternal(root->handle, node);
    CHECK(handle);
    if (handle) {
      node.handle = *handle;
      auto *type = world.addTypedChild(*root, 14, "timed", 0);
      CHECK(type);
      if (type) {
        CHECK(resources.bindSceneMaterial(
            resources.resolve(type), std::make_shared<SceneMaterialResources>(
                                         nullptr, std::vector<LinkOwner *>{})));
        object_config::ObjectConfig c;
        c.hasLods = true;
        c.lods.thresholds = {0.5f, 1.0f};
        c.slots.resize(3);
        for (auto &slot : c.slots) {
          slot.kind = object_config::SlotKind::Render;
          slot.modifiers.lodFar = 0;
        }
        c.slots[0].modifiers.flags = 4;
        c.slots[1].modifiers.flags = 0x44;
        c.instance.id = 1;
        auto runtime = std::make_shared<object::ObjectRuntime>(
            std::move(c), object::ObjectPhysics::create());
        for (int i = 0; i < 3; ++i) {
          auto links = std::make_unique<object::ObjectRuntime::SlotResources>();
          if (i != 2)
            setLinkOwner(links->primary, type);
          runtime->resources.push_back(std::move(links));
        }
        node.objectRuntime = runtime;
        CHECK(!object::updateObjectLod(resources, node, 0, error));
        CHECK(runtime->renders.empty());
        CHECK(object::updateObjectLod(resources, node, 0, error, 21599));
        CHECK(runtime->renders.size() == 2 && runtime->renders[0]->slot == 0 &&
              runtime->renders[1]->slot == 2 &&
              !runtime->renders[1]->node.owner);
        CHECK(object::updateObjectLod(resources, node, 0, error, 21600));
        CHECK(runtime->renders.size() == 3 && runtime->renders[2]->slot == 1);
        CHECK(object::updateObjectLod(resources, node, 0.5f, error, 75599));
        CHECK(runtime->renders.empty());
        CHECK(object::updateObjectLod(resources, node, 0, error, 75599));
        CHECK(runtime->renders.size() == 2 && runtime->renders[0]->slot == 1 &&
              runtime->renders[1]->slot == 2);
        CHECK(object::updateObjectLod(resources, node, 0, error, 75600));
        CHECK(runtime->renders.size() == 3 && runtime->renders[2]->slot == 0);
        CHECK(world.detachExternal(*handle));
        node.objectRuntime.reset();
      }
    }
  }
  {
    PackRegistry packs("ref/raw");
    GameWorld world;
    auto *root = world.addRoot();
    ResourceWorld resources(world, packs);
    uint16_t slot = 0;
    CHECK(packs.registerPackName("system.rpk", slot, error));
    const auto found = resources.lookup((uint32_t(slot) << 16) | 4, 1);
    CHECK(found.node.packNode);
    if (found.node.packNode) {
      auto &mounted = *found.node.packNode;
      const auto initialChildren = world.childOwners(mounted.handle);
      CHECK(initialChildren && initialChildren->empty());
      mounted.stateWord |= 1;
      mounted.flags &= ~(0x40u | 0x100u);
      mounted.bounds = ResourceBounds{1, {0, 0, 0, 1, 1, 1}};
      mounted.polygonCount = 0;
      auto *type = world.addTypedChild(*root, 14, "object-test-type", 0);
      CHECK(type);
      if (type) {
        CHECK(resources.bindSceneMaterial(
            resources.resolve(type), std::make_shared<SceneMaterialResources>(
                                         nullptr, std::vector<LinkOwner *>{})));
        object_config::ObjectConfig c;
        c.hasLods = true;
        c.lods.thresholds = {0.1f, 0.5f, 1.0f};
        c.slots.resize(2);
        c.slots[0].kind = c.slots[1].kind = object_config::SlotKind::Render;
        c.slots[0].modifiers.lodFar = 0;
        c.slots[1].modifiers.lodNear = 1;
        auto runtime = std::make_shared<object::ObjectRuntime>(
            std::move(c), object::ObjectPhysics::create());
        for (int i = 0; i < 2; ++i) {
          auto links = std::make_unique<object::ObjectRuntime::SlotResources>();
          setLinkOwner(links->primary, type);
          runtime->resources.push_back(std::move(links));
        }
        mounted.objectRuntime = runtime;
        CameraTraversalParams params{
            {0, 0, 0}, 1,     -0.95f,
            1,         1,     0,
            0,         false, ResourceBounds{0, {0, 0, 0, 100, 0, 0}},
            {}};
        {
          SceneCameraCandidates candidates;
          CameraType1Records records;
          CHECK(traverseCameraCandidates(world, mounted, candidates, params,
                                         error, &records, &resources));
          CHECK(runtime->lod == 0 && runtime->renders.size() == 1 &&
                runtime->renders[0]->slot == 0);
          if (!runtime->renders.empty() && runtime->renders[0]->node.owner)
            CHECK(candidates.hasRecord(*runtime->renders[0]->node.owner));
        }
        world.setFrameCounter(world.frameCounter() + 1);
        params.lodBias = -0.8f;
        {
          SceneCameraCandidates candidates;
          CameraType1Records records;
          CHECK(traverseCameraCandidates(world, mounted, candidates, params,
                                         error, &records, &resources));
          CHECK(runtime->lod == 1 && runtime->renders.size() == 1 &&
                runtime->renders[0]->slot == 1);
          if (!runtime->renders.empty() && runtime->renders[0]->node.owner)
            CHECK(candidates.hasRecord(*runtime->renders[0]->node.owner));
        }
        mounted.objectRuntime.reset();
      }
    }
  }

  {
    const auto parsed =
        scxLoadFile("ref/raw/objects/meshes/kuka_a/phys_01.scx");
    CHECK(parsed.ok);
    if (parsed.ok) {
      std::vector<physics::Vec3> vertices;
      std::vector<physics::MeshTriangle> triangles;
      CHECK(decodeCarCollisionMesh(parsed.model, vertices, triangles, error));
      if (!vertices.empty() && !triangles.empty()) {
        auto meshPhysics = object::ObjectPhysics::create();
        object_config::ObjectSlot mesh;
        mesh.kind = object_config::SlotKind::Body;
        mesh.shape.kind = object_config::ShapeKind::Mesh;
        mesh.shape.mass = 200;
        auto invalid = triangles;
        invalid[0].indices[0] = static_cast<uint32_t>(vertices.size());
        CHECK(meshPhysics->addMeshBody(mesh, 0, vertices, invalid, error) < 0);
        CHECK(meshPhysics->bodyCount() == 0 &&
              meshPhysics->defaultBody() == -1);
        CHECK(meshPhysics->addMeshBody(mesh, 0, vertices, triangles, error) ==
              0);
        const auto *proto = meshPhysics->protoForBody(0);
        CHECK(proto && proto->shape.type == physics::ShapeType::Mesh &&
              proto->shape.vertices.size() == vertices.size() &&
              proto->shape.triangles.size() == triangles.size() &&
              proto->shape.core && proto->massPropertiesSupported);
        auto instance = meshPhysics->instantiate({0, 0, 0}, {0, 0, 0}, error);
        CHECK(instance && instance->children[0]->proto == proto);
      }
    }
  }
  auto physics = object::ObjectPhysics::create();
  object_config::ObjectSlot bad;
  bad.kind = object_config::SlotKind::Body;
  bad.shape.kind = static_cast<object_config::ShapeKind>(99);
  CHECK(physics->addBody(bad, 0, nullptr, error) < 0);
  CHECK(physics->defaultBody() == -1);
  object_config::ObjectSlot box;
  box.kind = object_config::SlotKind::Body;
  box.shape.kind = object_config::ShapeKind::Box;
  box.shape.mass = 1;
  box.shape.params = {1, 1, 1};
  CHECK(physics->addBody(box, 0, nullptr, error) == 0);
  auto instance = physics->instantiate({0, 0, 0}, {0, 0, 0}, error);
  CHECK(instance.has_value());
  if (instance) {
    const auto *childProto = instance->children[0]->proto;
    CHECK(instance->root->proto == instance->rootPrototype.get());
    CHECK(physics->addBody(box, 1, nullptr, error) == 1);
    CHECK(instance->children[0]->proto == childProto);
    auto handle = object::adoptInstance(std::move(*instance));
    CHECK(handle.valid() && handle.root() && handle.root()->proto);
    CHECK(handle.root()->proto->invMass > 0);
    CHECK(handle.bodies().size() == 2 &&
          handle.bodies()[1]->proto == childProto);
  }
  if (failures)
    std::printf("%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
