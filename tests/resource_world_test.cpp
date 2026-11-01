#include "ground_runtime.hpp"
#define _DARWIN_C_SOURCE
#include "game_creation.hpp"
#include "game_physics_geometry.hpp"
#include "game_spatial_payload.hpp"
#include "resource_sound.hpp"
#include "resource_texture.hpp"
#include "resource_world.hpp"
#include "resource_extp_bounds.hpp"
#include "rpk.hpp"
#include "scx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);                      \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

namespace {

void put32(std::vector<uint8_t> &out, uint32_t n) {
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<uint8_t>(n >> (8 * i)));
}

struct FakeEntry {
  uint32_t super;
  uint32_t id;
  uint8_t type;
  std::string alias = "";
};

std::vector<uint8_t> makePack(const std::vector<std::string> &dependencies,
                              const std::vector<FakeEntry> &entries) {
  std::vector<uint8_t> out{'R', 'P', 'A', 'K'};
  put32(out, 0x200);
  put32(out, static_cast<uint32_t>(dependencies.size()));
  put32(out, 0);
  for (size_t i = 0; i < dependencies.size(); ++i) {
    put32(out, static_cast<uint32_t>((i + 1) << 16));
    std::string path = dependencies[i];
    path.resize(56, '\0');
    out.insert(out.end(), path.begin(), path.end());
    put32(out, 0);
  }
  std::vector<uint8_t> table;
  for (const auto &e : entries) {
    put32(table, e.super);
    put32(table, e.id);
    table.push_back(e.type);
    table.push_back(0);
    put32(table, 0);
    put32(table, 0);
    put32(table, 0);
    table.push_back(static_cast<uint8_t>(e.alias.size()));
    table.insert(table.end(), e.alias.begin(), e.alias.end());
  }
  put32(out, static_cast<uint32_t>(table.size()));
  put32(out, static_cast<uint32_t>(entries.size()));
  put32(out, 0);
  put32(out, 0);
  out.insert(out.end(), table.begin(), table.end());
  return out;
}

void writeFile(const std::filesystem::path &path,
               const std::vector<uint8_t> &bytes) {
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}

uint16_t slotOf(PackRegistry &registry, const std::string &name) {
  uint16_t slot = 0xffff;
  std::string error;
  registry.registerPackName(name, slot, error);
  return slot;
}

const RpkEntry *entryByAlias(const RpkPack &pack, const std::string &alias,
                             uint32_t type = 0) {
  for (const auto &e : pack.entries)
    if (e.alias == alias && (type == 0 || e.resType == type))
      return &e;
  return nullptr;
}

} // namespace

static void typeTable() {
  TextureAllocationInventory allocations;
  auto first = allocations.track(84);
  auto second = allocations.track(16);
  CHECK(allocations.count() == 2 && allocations.upperBound() == 100);
  auto shared = first;
  first.reset();
  CHECK(allocations.count() == 2);
  shared.reset();
  CHECK(allocations.count() == 1 && allocations.upperBound() == 16);
  auto unknown = allocations.track(std::nullopt);
  CHECK(!allocations.upperBound());
  unknown.reset();
  CHECK(allocations.upperBound() == 16);
  auto overflow = allocations.track(UINT64_MAX);
  CHECK(!allocations.upperBound());
  overflow.reset();
  second.reset();
  CHECK(allocations.count() == 0 && allocations.upperBound() == 0);
  CHECK((stockTextureCloneSize(1024, 512, 64u * 1024 * 1024) ==
         std::array<uint32_t, 2>{256, 128}));
  CHECK((stockTextureCloneSize(512, 1024, 128u * 1024 * 1024) ==
         std::array<uint32_t, 2>{256, 512}));
  CHECK((stockTextureCloneSize(1024, 512, 128u * 1024 * 1024 + 1) ==
         std::array<uint32_t, 2>{1024, 512}));
  CHECK(!stockTextureCloneSize(0, 1, 0));
  CHECK(!stockTextureCloneSize(1000, 500, 0));
  CHECK(!stockTextureCloneSize(16384, 1, 0));
  CHECK(stockTextureFamilyUpperBound(4, 2) == 336);
  CHECK(!stockTextureFamilyUpperBound(3, 2));
  {
    // The slot the host presents: caps 0x4000 (mip chain) and a 16384 maximum
    // leave a 1024 square at 11 levels, whose 4-byte bound is 4 * (4^11 - 1) /
    // 3 = 5592404 (00591d85 -> 005914a9 -> 004f6230).
    const auto caps = hostDeviceTextureCaps(16384);
    const auto shape = stockDeviceTextureShape(1024, 1024, caps);
    CHECK(shape && shape->width == 1024 && shape->height == 1024 &&
          shape->levels == 11);
    CHECK(shape && stockTextureSlotUpperBound(*shape) == 5592404u);
    const auto wide = stockDeviceTextureShape(512, 256, caps);
    CHECK(wide && wide->levels == 10 &&
          stockTextureSlotUpperBound(*wide) ==
              4u * (512 * 256 + 256 * 128 + 128 * 64 + 64 * 32 + 32 * 16 +
                    16 * 8 + 8 * 4 + 4 * 2 + 2 * 1 + 1));
    // Device maximum clamps, power-of-two and square-only caps enlarge or
    // reshape, no mip cap gives one level.
    const auto clamped =
        stockDeviceTextureShape(1024, 1024, hostDeviceTextureCaps(512));
    CHECK(clamped && clamped->width == 512 && clamped->height == 512 &&
          clamped->levels == 10);
    const auto pow2 = stockDeviceTextureShape(
        100, 20, {kD3DTexCapsPow2 | kD3DTexCapsMipMap, 4096, 4096, 0});
    CHECK(pow2 && pow2->width == 128 && pow2->height == 32 &&
          pow2->levels == 8);
    const auto square = stockDeviceTextureShape(
        256, 64, {kD3DTexCapsSquareOnly | kD3DTexCapsMipMap, 4096, 4096, 0});
    CHECK(square && square->width == 64 && square->height == 64);
    const auto aspect =
        stockDeviceTextureShape(256, 64, {kD3DTexCapsMipMap, 4096, 4096, 2});
    CHECK(aspect && aspect->width == 128 && aspect->height == 64);
    const auto flat = stockDeviceTextureShape(64, 64, {0, 4096, 4096, 0});
    CHECK(flat && flat->levels == 1 &&
          stockTextureSlotUpperBound(*flat) == 64u * 64 * 4);
    CHECK(!stockDeviceTextureShape(
        64, 64, {kD3DTexCapsMipMap, 0, 0, 0})); // no reported maximum: unknown
    CHECK(!stockDeviceTextureShape(
        2, 2, caps)); // compressed-format rounding unmodelled
    CHECK(!stockTextureSlotUpperBound({0, 4, 1}));
  }
  CHECK(stockTextureUpperBound(4, 4) == 84);
  CHECK(stockTextureUpperBound(3, 1) == 16);
  CHECK(!stockTextureUpperBound(0, 1));
  CHECK(!stockTextureUpperBound(UINT32_MAX, UINT32_MAX));
  ScxModel mesh;
  mesh.version = 3;
  mesh.meshes.resize(2);
  mesh.meshes[0].vertexCount = 1024;
  mesh.meshes[1].vertexCount = 7;
  CHECK(stockMeshVertexBytes(mesh) == (1024u + 7u) * 56u);
  mesh.version = 4;
  CHECK(!stockMeshVertexBytes(mesh));
  ScxModel quad;
  quad.version = 4;
  quad.materials.resize(1);
  quad.meshes.resize(1);
  quad.materials[0].functionWord = 0x40000000u;
  quad.materials[0].maps.push_back({});
  quad.meshes[0].materialIndex = 0;
  quad.meshes[0].vertexFlags = 0x241;
  quad.meshes[0].vertexCount = 4;
  CHECK(stockMeshVertexBytes(quad) == 160);
  quad.materials[0].maps[0].slot = 1;
  CHECK(!stockMeshVertexBytes(quad));
  CHECK(stockMeshVertexBytesUpperBound(quad) == 4u * 56u);
  quad.materials[0].flags = 12;
  quad.materials[0].functionWord = 0;
  CHECK(stockMeshVertexBytesUpperBound(quad) == 4u * 32u);
  quad.materials[0].functionWord = 0x80000000u;
  CHECK(stockMeshVertexBytesUpperBound(quad) == 4u * 304u);
  CHECK(stockMeshVertexBytesUpperBound(mesh) == 1031u * 304u);
  mesh.version = 5;
  CHECK(!stockMeshVertexBytesUpperBound(mesh));
  for (uint32_t type :
       {1u, 2u, 3u, 5u, 6u, 7u, 8u, 9u, 10u, 12u, 13u, 14u, 18u, 19u, 20u, 21u})
    CHECK(nodeTypeInfo(type));
  for (uint32_t type : {0u, 4u, 11u, 15u, 16u, 17u, 22u, 0xffffu})
    CHECK(!nodeTypeInfo(type));
  CHECK(nodeTypeInfo(5)->baseFlags == 0x100 &&
        nodeTypeInfo(2)->baseFlags == 0x100 && nodeTypeInfo(7)->baseFlags == 0);
  CHECK(sectorHookedType(1) && sectorHookedType(3) && sectorHookedType(0x10) &&
        !sectorHookedType(0) && !sectorHookedType(7));
}

static void meshInventory() {
  PackRegistry packs("ref/raw");
  GameWorld world;
  const auto initialFrame = world.frameCounter();
  auto *root = world.addRoot();
  ResourceWorld resources(world, packs);
  CHECK(resources.stockSceneMeshUpperBound() == 0);
  auto *mesh = world.addTypedChild(*root, 5, "mesh", 0);
  CHECK(mesh && !resources.stockSceneMeshUpperBound());
  if (!mesh)
    return;
  auto model = std::make_shared<ScxModel>();
  model->version = 3;
  model->meshes.resize(1);
  model->meshes[0].vertexCount = 100;
  CHECK(resources.bindSceneMesh(resources.resolve(mesh), model));
  CHECK(resources.stockSceneMeshUpperBound() == 5600);
  world.markLoaded(*mesh);
  world.setFrameCounter(1);
  mesh->field6c = 0.75f;
  mesh->stamp7c = 1;
  world.noteRecentUse(*mesh, 5, mesh->stamp74, mesh->recentUseQueued);
  std::string error;
  world.setFrameCounter(9);
  CHECK(resources.ageRecentUses(error) && mesh->recentUseQueued &&
        mesh->field6c == 0.75f);
  world.setFrameCounter(10);
  CHECK(resources.ageRecentUses(error) && !mesh->recentUseQueued &&
        mesh->field6c == 0.0f);
  CHECK(mesh->stamp7c == 1 && world.loadList(5, true).size() == 1);
  auto *material = world.addTypedChild(*root, 14, "material", 0);
  CHECK(material);
  if (material) {
    world.markLoaded(*material);
    material->field6c = 0.25f;
    material->visibilityBias = 1.5f;
    material->stamp78 = 10;
    world.noteRecentUse(*material, 14, material->stamp74,
                        material->recentUseQueued);
    world.setFrameCounter(18);
    CHECK(resources.ageRecentUses(error) && material->recentUseQueued &&
          material->field6c == 0.25f);
    world.setFrameCounter(19);
    CHECK(resources.ageRecentUses(error) && !material->recentUseQueued &&
          material->field6c == 1.0f);
    CHECK(material->stamp78 == 10 && world.loadList(14, true).size() == 1);
    CHECK(world.destroy(*material));
  }

  // Flush tail 00537ed0(1) forces threshold 0: an age-4 node is processed
  // where the unforced per-pass sweep still skips it.
  world.noteRecentUse(*mesh, 5, mesh->stamp74, mesh->recentUseQueued);
  mesh->field6c = 0.75f;
  mesh->stamp7c = 1;
  world.setFrameCounter(23);
  CHECK(resources.ageRecentUses(error) && mesh->recentUseQueued);
  CHECK(resources.ageRecentUses(error, 0) && !mesh->recentUseQueued &&
        mesh->field6c == 0.0f);
  // Script-created runtime rectangles (type 3) enter stock's recent list
  // through the same Use path; flush ages them like any family.
  auto *rect = world.addTypedChild(*root, 3, "rect", 0);
  CHECK(rect);
  if (rect) {
    world.noteRecentUse(*rect, 3, rect->stamp74, rect->recentUseQueued);
    world.setFrameCounter(24);
    CHECK(resources.ageRecentUses(error) && rect->recentUseQueued);
    CHECK(resources.ageRecentUses(error, 0) && !rect->recentUseQueued);
    CHECK(world.destroy(*rect));
  }

  CHECK(!resources.forceRenderPreflight(error, 100000, 100000, 100000, 5600));
  CHECK(resources.forceRenderPreflight(error, 100000, 100000, 100000, 5601));
  model->version = 4;
  CHECK(resources.stockSceneMeshUpperBound().has_value());
  model->version = 3;
  world.destroy(*mesh);
  CHECK(resources.stockSceneMeshUpperBound() == 0);
  world.setFrameCounter(initialFrame);
}

static void viewportAttachments() {
  GameWorld world;
  WorldNode *root = world.addRoot();
  WorldNode *first = world.addTypedChild(*root, 3, "first", 0);
  WorldNode *second = world.addTypedChild(*root, 3, "second", 0);
  WorldNode *third = world.addTypedChild(*root, 3, "third", 0);
  CHECK(first && second && third);
  if (!first || !second || !third)
    return;
  ViewportRenderAttachments attachments;
  attachments.prepare(*first, 4);
  attachments.prepare(*second, 8);
  attachments.prepare(*third, 4);
  attachments.prepare(*first, 100);
  CHECK(attachments.activate(*second, -1));
  CHECK(attachments.activate(*first, -1));
  CHECK(attachments.activate(*third, -1));
  CHECK(attachments.active() ==
        std::vector<LinkOwner *>({third, first, second}));
  CHECK(world.destroy(*first));
  CHECK(attachments.active() == std::vector<LinkOwner *>({third, second}));
  ViewportRenderAttachments unknownPriority;
  unknownPriority.prepare(*second, std::nullopt);
  unknownPriority.prepare(*third, 4);
  CHECK(unknownPriority.activate(*second, -1));
  CHECK(!unknownPriority.activate(*third, -1));
  CHECK(unknownPriority.active() == std::vector<LinkOwner *>({second}));
}

static void cameraResource() {
  const auto camera =
      makeCameraResource(1.5f, 60.0f, 0.5f, 100.0f, 2.0f, 3.0f, 7, 11);
  CHECK(camera.viewportAspect == 1.5f && camera.fieldOfView == 60.0f);
  CHECK(camera.nearDistance == 5.0f && camera.farDistance == 1000.0f);
  CHECK(camera.lodBias == 2.0f && camera.lodAmplitude == 3.0f &&
        camera.culling == 7 && camera.planeTest == 11);
  const auto projection = makeCameraRendererFields(camera);
  CHECK(projection.viewportAspect == 1.5f && projection.fieldOfView == 60.0f &&
        projection.nearDistance == 5.0f && projection.farMetric > 1315.0f &&
        projection.farMetric < 1317.0f && projection.lodBias == 2.0f &&
        projection.lodAmplitude == 3.0f && projection.planeTest == 11);
  auto noAspect = camera;
  noAspect.viewportAspect = 0.0f;
  const auto updated = updateCameraRendererFields(noAspect, 2.0f);
  CHECK(updated.viewportAspect == 2.0f && updated.farMetric > 1146.0f &&
        updated.farMetric < 1148.0f);
  const auto matrix = cameraProjectionMatrix(updated);
  CHECK(matrix[0] > 0.86f && matrix[0] < 0.87f && matrix[5] > 1.72f &&
        matrix[5] < 1.74f && matrix[10] < -1.0f && matrix[11] == -1.0f &&
        matrix[14] < -5.0f && matrix[15] == 0.0f);
  std::array<float, 16> worldMatrix{1, 0, 0, 0, 0, 1,  0, 0,
                                    0, 0, 1, 0, 4, -2, 3, 1};
  const auto view = cameraViewMatrix(worldMatrix);
  CHECK(view && (*view)[12] == -4 && (*view)[13] == 2 && (*view)[14] == -3);
  const auto state = cameraViewState(noAspect, worldMatrix, 2.0f);
  CHECK(state && state->fields.viewportAspect == 2.0f &&
        state->view[12] == -4 && state->projection[11] == -1.0f);
  const auto planes = cameraFrustumPlanes(updated, worldMatrix);
  CHECK(planes && (*planes)[0][2] == -1.0f && (*planes)[0][3] > 100.0f &&
        (*planes)[1][0] > 0.0f && (*planes)[2][0] < 0.0f &&
        (*planes)[3][1] > 0.0f && (*planes)[4][1] < 0.0f);
  const auto center =
      view ? projectCameraPoint({4, -2, -7}, *view, matrix) : std::nullopt;
  CHECK(center && (*center)[0] == 0 && (*center)[1] == 0 && (*center)[2] > 0 &&
        (*center)[2] < 1);
  const auto right =
      view ? projectCameraPoint({5, -2, -7}, *view, matrix) : std::nullopt;
  CHECK(right && (*right)[0] > 0 && (*right)[0] < 1);
  CHECK(view && !projectCameraPoint({4, -2, 3}, *view, matrix));
  CHECK(view && !projectCameraPoint({4, -2, 4}, *view, matrix));
  const ViewportInstance viewport{
      0, 0.25f, 0.0f, 0.5f, 1.0f, ViewportList::Pending, {}};
  const auto pixel =
      center ? cameraViewportPoint(*center, viewport, 800, 600) : std::nullopt;
  CHECK(pixel && (*pixel)[0] == 400 && (*pixel)[1] == 300);
  CHECK(!cameraViewportPoint({1.1f, 0, 0.5f}, viewport, 800, 600));
  CHECK(!cameraViewportPoint({0, 0, -0.1f}, viewport, 800, 600));
  const std::array<CameraClipVertex, 3> triangle{
      {{{{-2, 0, 0.5f, 1}}, {{0, 0}}},
       {{{0, -0.5f, 0.5f, 1}}, {{1, 0}}},
       {{{0, 0.5f, 0.5f, 1}}, {{1, 1}}}}};
  const auto clipped = clipCameraTriangle(triangle);
  CHECK(clipped && clipped->size() == 4);
  CHECK(clipped && (*clipped)[0].uv[0] == 0.5f && (*clipped)[0].uv[1] == 0.5f &&
        (*clipped)[1].uv[0] == 0.5f && (*clipped)[1].uv[1] == 0.0f);
  if (clipped)
    for (const auto &vertex : *clipped)
      CHECK(vertex.position[0] >= -vertex.position[3] &&
            vertex.position[0] <= vertex.position[3]);
  worldMatrix[0] = 2;
  const auto scaled = cameraViewMatrix(worldMatrix);
  CHECK(scaled && (*scaled)[0] == 0.5f && (*scaled)[12] == -2);
  worldMatrix[0] = 0;
  CHECK(!cameraViewMatrix(worldMatrix));
}

static void systemMount() {
  PackRegistry registry("ref/raw");
  GameWorld world;
  WorldNode *root = world.addRoot();
  CHECK(root);
  const uint16_t system = slotOf(registry, "system.rpk");
  CHECK(system == 0);
  ResourceWorld rw(world, registry);
  CHECK(rw.mountState(system) == MountState::Unmounted);

  const auto parsed = rpkLoadFile("ref/raw/system.rpk");
  CHECK(parsed.ok && parsed.pack.entries.size() == 28);

  const MountResult mounted = rw.mountPack(system);
  CHECK(mounted.status == MountStatus::Mounted &&
        mounted.state == MountState::Mounted);
  // Types 4, 11, 15, 16 and 17 have no case in FUN_00536a10: stock logs and
  // creates nothing.
  CHECK(mounted.nodesCreated == 23 && mounted.entriesSkipped == 5);
  CHECK(rw.log().size() == 5 &&
        rw.log()[0].find("Node registration error") == 0);
  CHECK(rw.nodeCount() == 23);
  CHECK(rw.mountState(system) == MountState::Mounted);
  CHECK(rw.mountPack(system).status == MountStatus::AlreadyMounted);
  CHECK(rw.nodeCount() == 23);

  // Fifteen creatable entries have parent 0 and hang under the default parent,
  // in file order.
  const auto &top = rw.defaultParentChildren();
  CHECK(top.size() == 15);
  if (top.size() == 15) {
    CHECK(top[0]->value == 1 && top[0]->type == 8 &&
          top[0]->name == "typ_game_object");
    CHECK(top[3]->value == 4 && top[3]->type == 1);
  }

  // Config, Dummy, game and native are scripts under typ_game_object.
  LookupResult base = rw.lookup(1);
  CHECK(base.status == LookupStatus::Found && base.node.packNode &&
        base.node.script && base.node.type == 8);
  if (base.node.packNode) {
    const auto &kids = base.node.packNode->children;
    CHECK(kids.size() == 4);
    if (kids.size() == 4) {
      CHECK(kids[0]->value == 0x47 && kids[0]->name == "Config");
      CHECK(kids[1]->value == 0x1b && kids[1]->name == "Dummy");
      CHECK(kids[2]->value == 0x4c && kids[2]->name == "game");
      CHECK(kids[3]->value == 0x15 && kids[3]->name == "native");
      CHECK(kids[3]->children.size() == 4 &&
            kids[3]->children[1]->name == "Controller");
      CHECK(kids[1]->parent == base.node.packNode && kids[1]->parentId == 1);
    }
  }

  // The script node is the one GameType creation uses, and it has not been
  // loaded.
  ScriptNode *dummy = world.findScript(0x1b);
  CHECK(dummy && dummy->className.empty() && !dummy->loaded);
  const LookupResult dummyLookup = rw.lookup(0x1b);
  CHECK(dummyLookup.status == LookupStatus::Found &&
        dummyLookup.node.script == dummy && dummyLookup.node.owner == dummy);

  // Per-type construction: flags, bounds record, sector root, load state.
  const LookupResult game = rw.lookup(4);
  CHECK(game.status == LookupStatus::Found && game.node.type == 1 &&
        game.node.packNode);
  if (game.node.packNode) {
    const ResourceNode &n = *game.node.packNode;
    CHECK(n.flags == 1 && n.payloadSize == 8 && n.bounds.has_value());
    CHECK(n.sectorLink.owner == &n && n.sectorLink.cachedValue == 4);
    CHECK(n.stateWord == 0x40 && !n.loaded() && n.loadPriority == 0.0f);
    CHECK(n.field6c == 0.0f && n.field70 == 0.0f);
    CHECK(n.stamp78 == 0 && n.stamp7c == 0 && n.visibilityBias == 0 &&
          n.visibilityGain == 1 && n.polygonCount == 0);
    CHECK(n.name.empty());
    if (n.bounds && parsed.ok) {
      const auto *entry = entryByAlias(parsed.pack, "ins_game");
      CHECK(entry && entry->transform && n.bounds->mode == 1 &&
            n.bounds->values[0] == (*entry->transform)[0] &&
            n.bounds->values[5] == (*entry->transform)[5]);
    }
  }
  const LookupResult physics = rw.lookup(5);
  CHECK(physics.node.packNode && physics.node.packNode->flags == 0x100 &&
        !physics.node.packNode->bounds);
  CHECK(physics.node.packNode &&
        physics.node.packNode->sectorLink.owner == physics.node.packNode);
  const LookupResult mesh = rw.lookup(7);
  CHECK(mesh.node.packNode && mesh.node.packNode->type == 5 &&
        mesh.node.packNode->flags == 0x104);
  CHECK(mesh.node.packNode && mesh.node.packNode->sectorLink.owner == nullptr);
  const LookupResult texture = rw.lookup(8);
  CHECK(texture.node.packNode && texture.node.packNode->type == 7 &&
        texture.node.packNode->flags == 4);

  // Lookup failures.
  CHECK(rw.lookup(0).status == LookupStatus::NotFound);
  CHECK(rw.missingIds().empty());
  CHECK(rw.lookup(0x7e).status == LookupStatus::NotFound);
  CHECK(rw.missingIds().size() == 1 && rw.missingIds()[0] == 0x7e);
  CHECK(rw.lookup(0x7f, 0, true).status == LookupStatus::NotFound &&
        rw.missingIds().size() == 1);
  // An id whose entry the type switch skipped is simply not there.
  CHECK(rw.lookup(0xa).status == LookupStatus::NotFound);
  // A type filter.
  CHECK(rw.lookup(0x1b, 7).status == LookupStatus::NotFound);
  CHECK(rw.lookup(0x1b, 8).status == LookupStatus::Found);
  // A pack slot that does not exist is not mounted and not found.
  CHECK(rw.lookup(0x00090001).status == LookupStatus::NotFound);

  const PayloadRequest payload = rw.requestPayload(texture.node);
  CHECK(payload.status == PayloadStatus::Deferred &&
        payload.detail.find("res_texture") != std::string::npos);
}

static void referenceSet() {
  OwnerLink link;
  PackRegistry registry("ref/raw");
  GameWorld world;
  world.addRoot();
  slotOf(registry, "system.rpk");
  ResourceWorld rw(world, registry);

  // A fresh link set to 0 changes nothing.
  CHECK(resourceRefSet(rw, link, 0).status == RefSetStatus::Unchanged);
  const RefSetResult first = resourceRefSet(rw, link, 0x1b);
  CHECK(first.status == RefSetStatus::Linked &&
        link.owner == first.node.owner && link.cachedValue == 0x1b);
  CHECK(rw.mountState(0) == MountState::Mounted);
  CHECK(first.node.owner && first.node.owner->head == &link);
  // The same id again changes nothing (and does not repeat the lookup).
  const size_t missingBefore = rw.missingIds().size();
  CHECK(resourceRefSet(rw, link, 0x1b).status == RefSetStatus::Unchanged);
  CHECK(rw.missingIds().size() == missingBefore);
  // Another id moves the link.
  OwnerLink second;
  CHECK(resourceRefSet(rw, second, 0x1b).status == RefSetStatus::Linked);
  CHECK(first.node.owner->head == &second && second.next == &link);
  const RefSetResult moved = resourceRefSet(rw, link, 0x47);
  CHECK(moved.status == RefSetStatus::Linked &&
        link.owner == moved.node.owner && link.cachedValue == 0x47);
  CHECK(first.node.owner->head == &second && second.next == nullptr);
  // An id that resolves to nothing leaves the old link detached and id 0.
  const RefSetResult missing = resourceRefSet(rw, link, 0x7e);
  CHECK(missing.status == RefSetStatus::NotFound && link.owner == nullptr &&
        link.cachedValue == 0);
  CHECK(rw.missingIds().size() == missingBefore + 1);
  // Setting 0 detaches.
  CHECK(resourceRefSet(rw, link, 0x47).status == RefSetStatus::Linked);
  CHECK(resourceRefSet(rw, link, 0).status == RefSetStatus::Cleared &&
        link.owner == nullptr && link.cachedValue == 0);
  // Stale id: with no node linked, stock compares and skips the unlink, so an
  // unresolved id stays.
  OwnerLink stale;
  stale.cachedValue = 5;
  CHECK(resourceRefSet(rw, stale, 0).status == RefSetStatus::Unchanged &&
        stale.cachedValue == 5);
  CHECK(resourceRefSet(rw, stale, 5).status == RefSetStatus::Unchanged);
  CHECK(resourceRefSet(rw, stale, 0x1b).status == RefSetStatus::Linked &&
        stale.cachedValue == 0x1b);
  setLinkOwner(stale, nullptr);
  setLinkOwner(second, nullptr);

  // Dynamic nodes (pack 0xffff) are found by id too.
  WorldNode *parent = world.defaultParent();
  WorldNode *dynamic =
      parent ? world.addChild(*parent, "thing", 0x21) : nullptr;
  CHECK(dynamic && (dynamic->instanceId >> 16) == 0xffff);
  if (dynamic) {
    const RefSetResult dyn = resourceRefSet(rw, link, dynamic->instanceId);
    CHECK(dyn.status == RefSetStatus::Linked && dyn.node.worldNode == dynamic &&
          dyn.node.type == 1);
    CHECK(rw.resolve(link.owner).worldNode == dynamic);
    CHECK(resourceRefSet(rw, link, 0).status == RefSetStatus::Cleared);
  }
}

static void frontendMount() {
  OwnerLink link;
  PackRegistry registry("ref/raw");
  GameWorld world;
  world.addRoot();
  CHECK(slotOf(registry, "system.rpk") == 0);
  std::string error;
  // The literals are the ones java.render.Frontend and java.render.Text hold:
  // defLoadingPic and RID_*.
  uint32_t loadingPic = 0;
  CHECK(registry.resolveLiteral("frontend.rpk", 0xa8, loadingPic, error));
  const struct {
    const char *alias;
    uint32_t literal;
    uint32_t id;
  } fonts[] = {
      {"console5", 0xb2, 0},     {"console10", 0x23, 0}, {"sl28", 0x2e, 0},
      {"slii11", 0x138, 0},      {"slii17", 0x139, 0},   {"slii24", 0x25, 0},
      {"sl14", 0x8c, 0},         {"simple20", 0x20, 0},  {"simple40", 0x22, 0},
      {"sym_pointers", 0x3c, 0},
  };
  uint32_t fontIds[10] = {};
  for (size_t i = 0; i < 10; ++i)
    CHECK(registry.resolveLiteral("frontend.rpk", fonts[i].literal, fontIds[i],
                                  error));
  const uint16_t frontend = static_cast<uint16_t>(loadingPic >> 16);
  CHECK(frontend == 1 && loadingPic == 0x000100a8);
  // Resolving a literal only registers names; the mount comes with the first
  // lookup.
  ResourceWorld rw(world, registry);
  CHECK(rw.mountState(0) == MountState::Unmounted &&
        rw.mountState(frontend) == MountState::Unmounted);

  const RefSetResult set = resourceRefSet(rw, link, loadingPic);
  CHECK(set.status == RefSetStatus::Linked && set.node.type == 7 &&
        set.node.packNode);
  // The dependency system.rpk was mounted when the first frontend entry looked
  // for its parent.
  CHECK(rw.mountState(frontend) == MountState::Mounted &&
        rw.mountState(0) == MountState::Mounted);
  // garage.rpk and cars.rpk are registered by the mount but nothing needs them.
  CHECK(registry.slotCount() >= 4);
  for (uint16_t slot = 2; slot < registry.slotCount(); ++slot)
    CHECK(rw.mountState(slot) == MountState::Unmounted);

  const auto parsed = rpkLoadFile("ref/raw/frontend.rpk");
  CHECK(parsed.ok && parsed.pack.entries.size() == 279);
  CHECK(rw.nodeCount() == 23 + 279);
  // Every frontend entry is a node of its own type, with its payload location.
  size_t resolved = 0;
  for (const auto &entry : parsed.pack.entries) {
    const LookupResult found =
        rw.lookup((uint32_t{frontend} << 16) | (entry.typeId & 0xffff));
    if (found.status == LookupStatus::Found &&
        found.node.type == entry.resType && found.node.packNode &&
        found.node.packNode->payloadOffset == entry.payloadOffset &&
        found.node.packNode->payloadSize == entry.payloadSize)
      ++resolved;
  }
  CHECK(resolved == 279);
  CHECK(rw.missingIds().empty());

  // The loading picture hangs under backgrounds, OSD and the system res_texture
  // node.
  const ResourceNode *pic = set.node.packNode;
  CHECK(pic && pic->parentId == 0x000100d2);
  const ResourceNode *backgrounds = pic ? pic->parent : nullptr;
  CHECK(backgrounds && backgrounds->value == 0x000100d2 &&
        backgrounds->parentId == 0x00010003);
  const ResourceNode *osd = backgrounds ? backgrounds->parent : nullptr;
  CHECK(osd && osd->value == 0x00010003 && osd->parentId == 0x00000008);
  const ResourceNode *systemTexture = osd ? osd->parent : nullptr;
  CHECK(systemTexture && systemTexture->value == 8 &&
        systemTexture->parent == nullptr);

  // The Frontend fonts are type-14 nodes (typ_render_object) under the frontend
  // 'fonts' node (frontend id 0x1f), which hangs under the system
  // typ_render_object node (system id 3).
  for (size_t i = 0; i < 10; ++i) {
    const LookupResult found = rw.lookup(fontIds[i]);
    const RpkEntry *entry = entryByAlias(parsed.pack, fonts[i].alias, 14);
    CHECK(entry && entry->resType == 14 &&
          (entry->typeId & 0xffff) == fonts[i].literal);
    CHECK(found.status == LookupStatus::Found && found.node.type == 14 &&
          found.node.packNode);
    CHECK(entry && found.node.packNode &&
          found.node.packNode->payloadOffset == entry->payloadOffset);
    CHECK(found.node.packNode && found.node.packNode->parentId == 0x0001001f &&
          found.node.packNode->name.empty());
    CHECK(found.node.packNode && found.node.packNode->parent &&
          found.node.packNode->parent->value == 0x0001001f);
  }
  const LookupResult fontsNode = rw.lookup(0x0001001f);
  CHECK(fontsNode.status == LookupStatus::Found && fontsNode.node.type == 14 &&
        fontsNode.node.packNode);
  CHECK(fontsNode.node.packNode && fontsNode.node.packNode->parent &&
        fontsNode.node.packNode->parent->value == 3);
  CHECK(fontsNode.node.packNode && fontsNode.node.packNode->parent &&
        fontsNode.node.packNode->parent->parent == nullptr);
  // A font node is renderer dependent: the mount keeps only its payload
  // location.
  const PayloadRequest fontPayload =
      rw.requestPayload(rw.lookup(fontIds[1]).node);
  CHECK(fontPayload.status == PayloadStatus::Deferred &&
        fontPayload.detail.find("typ_render_object") != std::string::npos);
  // The Frontend sound effects are type-6 nodes under the frontend 'frontend'
  // sound node (0x2c).
  uint32_t sfx = 0;
  CHECK(registry.resolveLiteral("frontend.rpk", 0x9b, sfx, error));
  const LookupResult money = rw.lookup(sfx);
  CHECK(money.status == LookupStatus::Found && money.node.type == 6 &&
        money.node.packNode && money.node.packNode->parentId == 0x0001002c);
  if (money.node.packNode) {
    const auto originalSoundFrame = world.frameCounter();
    world.setFrameCounter(originalSoundFrame + 1);
    auto &sound = *money.node.packNode;
    sound.sound = std::make_shared<SoundSample>();
    sound.soundBufferBytes = 100;
    sound.stateWord = 0x11;
    world.trackLoadState(sound, 6, true);
    world.noteRecentUse(sound, 6, sound.stamp74, sound.recentUseQueued);
    CHECK(sound.recentUseQueued && world.loadList(6, true).empty());
    CHECK(!rw.forceRenderPreflight(error, 100000, 100));
    CHECK(rw.forceRenderPreflight(error, 100000, 101));
    sound.soundBufferBytes = 0;
    CHECK(!rw.forceRenderPreflight(error, 100000, 101));
    world.finishRecentUse(sound, 6);
    world.forgetLoadState(sound);
    sound.recentUseQueued = false;
    sound.sound.reset();
    sound.stateWord = 0x40;
    world.setFrameCounter(originalSoundFrame);
  }

  // Re-setting the same ref after the mount does not mount or look up again.
  CHECK(resourceRefSet(rw, link, loadingPic).status == RefSetStatus::Unchanged);
  // 0053d3c0 uses current load priority, not the previous requested demand.
  auto &texture = *set.node.packNode;
  texture.flags |= 0x04000000u;
  CHECK(!rw.forceRenderPreflight(error) &&
        error.find("pending pack") != std::string::npos);
  texture.flags &= ~uint32_t{0x04000000u};
  texture.flags |= 0x02000000u;
  CHECK(!rw.forceRenderPreflight(error) &&
        error.find("pending pack") != std::string::npos);
  texture.flags &= ~uint32_t{0x02000000u};
  const auto originalFrame = world.frameCounter();
  world.setFrameCounter(1);
  texture.field6c = 0.0f;
  CHECK(rw.setTextureDemand(texture, 1.0f, error));
  CHECK(texture.field6c && std::abs(*texture.field6c - 0.1f) < 0.00001f);
  texture.loadPriority = 0.5f;
  texture.stamp78 = 1;
  texture.field6c = 0.8f;
  CHECK(rw.setTextureDemand(texture, 0.0f, error));
  CHECK(texture.field6c && *texture.field6c == 0.8f);
  world.setFrameCounter(2);
  CHECK(rw.setTextureDemand(texture, 0.0f, error));
  CHECK(texture.field6c && std::abs(*texture.field6c - 0.425f) < 0.00001f);
  texture.texture = std::make_shared<ResourceTextureCache>();
  const auto cache = texture.texture;
  texture.stateWord = 0x11;
  world.trackLoadState(texture, 7, true);
  CHECK(!rw.forceRenderPreflight(error) &&
        error.find("pressure accounting") != std::string::npos);
  CHECK(!rw.forceRenderPreflight(error, 100000, 100000, 100));
  texture.texture->allocation = rw.textureAllocations().track(100);
  CHECK(!rw.forceRenderPreflight(error, 100000, 100000, 100));
  CHECK(rw.forceRenderPreflight(error, 100000, 100000, 101));
  auto unownedAllocation = rw.textureAllocations().track(1);
  CHECK(!rw.forceRenderPreflight(error, 100000, 100000, 102));
  unownedAllocation.reset();
  world.noteRecentUse(texture, 7, texture.stamp74, texture.recentUseQueued);
  CHECK(world.loadList(7, true).empty());
  CHECK(rw.forceRenderPreflight(error, 100000, 100000, 101));
  CHECK(!rw.forceRenderPreflight(error) &&
        error.find("pressure accounting") != std::string::npos);
  world.setFrameCounter(10);
  CHECK(rw.ageRecentUses(error) && texture.recentUseQueued);
  world.setFrameCounter(11);
  CHECK(rw.ageRecentUses(error) && !texture.recentUseQueued &&
        texture.texture == cache);
  CHECK(texture.field6c && std::abs(*texture.field6c - 0.425f) < 0.00001f);
  setLinkOwner(link, nullptr);
  world.setFrameCounter(originalFrame);
}

static void failures_() {
  namespace fs = std::filesystem;
  char pattern[] = "/tmp/slr-resource-world-XXXXXX";
  const char *made = mkdtemp(pattern);
  CHECK(made);
  if (!made)
    return;
  const fs::path dir = made;
  {
    PackRegistry registry(dir);
    GameWorld world;
    auto *root = world.addRoot();
    auto *instance = root ? world.addChild(*root, "instance", 0) : nullptr;
    ResourceWorld resources(world, registry);
    std::string error;
    CHECK(instance);
    if (instance) {
      world.markLoaded(*instance);
      CHECK(!resources.forceRenderPreflight(error));
      CHECK(!resources.forceRenderPreflight(error, 1));
      CHECK(resources.forceRenderPreflight(error, 2));
      world.noteInstanceCreated(*instance);
      CHECK(!resources.forceRenderPreflight(error, 2));
      CHECK(resources.forceRenderPreflight(error, 3));
      instance->flags |= 0x04000000u;
      CHECK(!resources.forceRenderPreflight(error, 3));
    }
  }
  writeFile(dir / "bad.rpk", {1, 2, 3, 4, 5});
  writeFile(dir / "orphan.rpk", makePack({}, {{0x99, 0x01, 7}}));
  writeFile(dir / "mismatch.rpk",
            makePack({}, {{0, 0x01, 7}, {0x01, 0x02, 5}}));
  writeFile(dir / "skips.rpk",
            makePack({}, {{0, 0x01, 4}, {0, 0x02, 11}, {0, 0x03, 7}}));
  writeFile(dir / "dup.rpk", makePack({}, {{0, 0x01, 7}, {0, 0x01, 7}}));
  writeFile(dir / "needs.rpk",
            makePack({"base.rpk", "unused.rpk"}, {{0x00010002, 0x05, 7}}));
  writeFile(dir / "base.rpk", makePack({}, {{0, 0x02, 7}}));
  writeFile(dir / "unused.rpk", makePack({}, {{0, 0x02, 7}}));
  writeFile(
      dir / "dyn.rpk",
      makePack({}, {{0, 0x01, 8, "script_one"},
                    {0, 0x02, 8, "an_alias_of_more_than_thirty_bytes_long"}}));

  {
    PackRegistry registry(dir);
    GameWorld world;
    world.addRoot();
    ResourceWorld rw(world, registry);
    const uint16_t missing = slotOf(registry, "nope.rpk");
    const MountResult gone = rw.mountPack(missing);
    CHECK(gone.status == MountStatus::FileMissing &&
          rw.mountState(missing) == MountState::Unmounted);
    // Stock leaves the state at 0, so the next lookup tries again and then
    // misses.
    CHECK(rw.lookup((uint32_t{missing} << 16) | 1).status ==
          LookupStatus::NotFound);
    CHECK(rw.missingIds().size() == 1 &&
          rw.mountState(missing) == MountState::Unmounted);

    const uint16_t bad = slotOf(registry, "bad.rpk");
    const MountResult invalid = rw.mountPack(bad);
    CHECK(invalid.status == MountStatus::Invalid &&
          rw.mountState(bad) == MountState::Unmounted);
    CHECK(rw.lookup((uint32_t{bad} << 16) | 1).status ==
          LookupStatus::MountRefused);
    CHECK(rw.mountPack(0x0fff).status == MountStatus::Invalid);

    const uint16_t orphan = slotOf(registry, "orphan.rpk");
    const MountResult lost = rw.mountPack(orphan);
    CHECK(lost.status == MountStatus::StockCrash && lost.nodesCreated == 0 &&
          rw.mountState(orphan) == MountState::Mounting);
    CHECK(rw.log().size() == 1 && rw.log()[0].find("Unregistrable node") == 0);
    // A pack stuck in Mounting is never mounted again.
    CHECK(rw.mountPack(orphan).status == MountStatus::AlreadyMounted);

    const uint16_t mismatch = slotOf(registry, "mismatch.rpk");
    const MountResult wrongType = rw.mountPack(mismatch);
    CHECK(wrongType.status == MountStatus::StockCrash &&
          wrongType.nodesCreated == 1);

    const uint16_t skips = slotOf(registry, "skips.rpk");
    const MountResult skipped = rw.mountPack(skips);
    CHECK(skipped.status == MountStatus::Mounted && skipped.nodesCreated == 1 &&
          skipped.entriesSkipped == 2);

    const uint16_t dup = slotOf(registry, "dup.rpk");
    const MountResult duplicated = rw.mountPack(dup);
    CHECK(duplicated.status == MountStatus::Mounted &&
          duplicated.nodesCreated == 2);
    const LookupResult first = rw.lookup((uint32_t{dup} << 16) | 1);
    CHECK(
        first.status == LookupStatus::Found &&
        first.node.packNode ==
            rw.defaultParentChildren()[rw.defaultParentChildren().size() - 2]);

    const uint16_t needs = slotOf(registry, "needs.rpk");
    const MountResult dependent = rw.mountPack(needs);
    CHECK(dependent.status == MountStatus::Mounted &&
          dependent.nodesCreated == 1);
    uint16_t base = 0, unused = 0;
    std::string error;
    CHECK(registry.registerPackName("base.rpk", base, error) &&
          registry.registerPackName("unused.rpk", unused, error));
    CHECK(rw.mountState(base) == MountState::Mounted &&
          rw.mountState(unused) == MountState::Unmounted);
    const LookupResult child = rw.lookup((uint32_t{needs} << 16) | 5);
    CHECK(child.status == LookupStatus::Found && child.node.packNode &&
          child.node.packNode->parent &&
          child.node.packNode->parent->value == ((uint32_t{base} << 16) | 2));

    const uint16_t scripts = slotOf(registry, "dyn.rpk");
    CHECK(rw.mountPack(scripts).status == MountStatus::Mounted);
    const LookupResult one = rw.lookup((uint32_t{scripts} << 16) | 1);
    const LookupResult two = rw.lookup((uint32_t{scripts} << 16) | 2);
    CHECK(one.node.packNode && one.node.packNode->name == "script_one");
    // FUN_00551120 copies at most 30 bytes of the alias.
    CHECK(two.node.packNode && two.node.packNode->name.size() == 30 &&
          two.node.packNode->name ==
              std::string("an_alias_of_more_than_thirty_bytes_long")
                  .substr(0, 30));
  }
  {
    // With no default parent stock dereferences null on the first parentless
    // entry.
    PackRegistry registry(dir);
    GameWorld world;
    ResourceWorld rw(world, registry);
    const uint16_t skips = slotOf(registry, "skips.rpk");
    const MountResult crashed = rw.mountPack(skips);
    CHECK(crashed.status == MountStatus::StockCrash &&
          crashed.detail.find("default parent") != std::string::npos);
    CHECK(rw.lookup((uint32_t{skips} << 16) | 3).status ==
          LookupStatus::NotFound);
  }
  fs::remove_all(dir);
}

static void createsOnMountedScript() {
  PackRegistry registry("ref/raw");
  GameWorld world;
  world.addRoot();
  world.setClassResolver([](const std::string &name) {
    return name == "java.util.resource.Dummy";
  });
  CHECK(slotOf(registry, "system.rpk") == 0);
  ResourceWorld rw(world, registry);
  ReceiverBinding receiver;
  receiver.getInfo = [](int) { return std::optional<int32_t>(0); };
  // ResourceRef.set mounts system.rpk through a lookup; GameType creation then
  // loads the node the mount made.
  OwnerLink ref;
  CHECK(resourceRefSet(rw, ref, 0x1b).status == RefSetStatus::Linked);
  ScriptNode *before = world.findScript(0x1b);
  CHECK(before && !before->loaded);
  const auto request = prepareGameCreation(registry, 0x1b, std::nullopt,
                                           std::nullopt, std::string("dummy"));
  CHECK(request.request.has_value());
  if (request.request) {
    const CreationResult created =
        createNativeInstance(world, *request.request, &receiver);
    CHECK(created.status == CreationStatus::Created);
    CHECK(world.findScript(0x1b) == before && before->loaded &&
          before->className == "java.util.resource.Dummy");
    CHECK(rw.lookup(0x1b).node.script == before);
    CHECK(rw.nodeCount() == 23);
    const auto parent = rw.lookup(4, 1);
    CHECK(parent.node.packNode);
    if (parent.node.packNode) {
      auto underPack = *request.request;
      underPack.parentResourceId = 4;
      ReceiverBinding other;
      other.getInfo = [](int) { return std::optional<int32_t>(0); };
      const auto pending = createNativeInstance(world, underPack, &other, &rw);
      // FUN_00536a10 links under the pack node without testing its load state.
      CHECK(pending.status == CreationStatus::Created && pending.node &&
            world.parentOwner(pending.node->handle) == parent.node.owner);
      std::string loadError;
      const auto unloaded = world.loadList(1, false);
      CHECK(std::find(unloaded.begin(), unloaded.end(), parent.node.owner) !=
            unloaded.end());
      CHECK(rw.loadEmptyGameParent(*parent.node.packNode, loadError));
      const auto loaded = world.loadList(1, true);
      CHECK(parent.node.packNode->loaded() &&
            std::find(loaded.begin(), loaded.end(), parent.node.owner) !=
                loaded.end());
      CHECK(rw.loadEmptyGameParent(*parent.node.packNode, loadError));
      const auto attached = createNativeInstance(world, underPack, &other, &rw);
      CHECK(attached.status == CreationStatus::Created && attached.node &&
            world.parentOwner(attached.node->handle) == parent.node.owner &&
            attached.node->sectorLink.owner ==
                parent.node.packNode->sectorLink.owner);
    }
  }
  setLinkOwner(ref, nullptr);
}

static void sharedParentage() {
  PackRegistry registry("ref/raw");
  GameWorld world;
  WorldNode *root = world.addRoot();
  CHECK(root && slotOf(registry, "system.rpk") == 0);
  uint32_t instance = 0;
  OwnerLink instanceLink;
  {
    ResourceWorld rw(world, registry);
    const auto packParent = rw.lookup(4, 1);
    CHECK(packParent.node.packNode);
    if (!packParent.node.packNode)
      return;
    ResourceNode &parent = *packParent.node.packNode;
    const auto rootChildren = world.childOwners(root->handle);
    CHECK(rootChildren &&
          rootChildren->size() == rw.defaultParentChildren().size());
    CHECK(!world.destroy(*root));
    WorldNode *child = rw.addGameChild(parent, "Config", 0x21);
    CHECK(child && world.parentOwner(child->handle) == &parent &&
          child->sectorLink.owner == parent.sectorLink.owner);
    if (!child)
      return;
    instance = child->instanceId;
    setLinkOwner(instanceLink, child);
    WorldNode *other = rw.addGameChild(parent, "second", 0x31);
    CHECK(other && other->sectorLink.owner == other);
    const auto children = world.childOwners(parent.handle);
    CHECK(children && children->size() >= 2 &&
          (*children)[children->size() - 2] == child &&
          children->back() == other);
    ResourceNode foreign;
    CHECK(!rw.addGameChild(foreign, "invalid", 0));
    WorldNode *material = world.addTypedChildUnderExternal(
        parent.handle, parent, parent.type, parent.sectorLink.owner, 14, "copy",
        0x200);
    CHECK(material && material->type == 14 && !material->sectorLink.owner &&
          world.parentOwner(material->handle) == &parent);
    WorldNode *mesh = world.addTypedChildUnderExternal(
        parent.handle, parent, parent.type, parent.sectorLink.owner, 5, "mesh",
        0x200);
    CHECK(mesh && mesh->type == 5 && mesh->flags == 0x300 &&
          !mesh->sectorLink.owner &&
          world.parentOwner(mesh->handle) == &parent);
    CHECK(mesh && world.loadList(5, false).back() == mesh);
    WorldNode *nested = world.addTypedChild(*child, 5, "nested mesh", 0);
    CHECK(nested && nested->flags == 0x100 && !nested->sectorLink.owner &&
          world.parentOwner(nested->handle) == child);
    WorldNode *render = world.addTypedChildUnderExternal(
        parent.handle, parent, parent.type, parent.sectorLink.owner, 3,
        "render", 0);
    CHECK(render && render->flags == 0x100 &&
          render->sectorLink.owner == parent.sectorLink.owner &&
          world.parentOwner(render->handle) == &parent);
    CHECK(render && cameraTraversalRoot(*render) == parent.sectorLink.owner);
    CHECK(!cameraTraversalRoot(*child));
    CHECK(render && world.loadList(3, false).back() == render);
    CHECK(render && render->bounds.mode == 2 &&
          render->bounds.values[0] == 0.0f);
    CHECK(render && render->field6c == 0.0f && render->field70 == 0.0f);
    CHECK(render && render->stamp78 == 0 && render->stamp7c == 0 &&
          render->visibilityBias == 0.0f && render->visibilityGain == 1.0f);
    world.setFrameCounter(0);
    GameWorld otherWorld;
    CHECK(otherWorld.frameCounter() == 0);
    WorldNode *wrapped = world.addTypedChild(*child, 3, "wrapped", 0);
    CHECK(wrapped && !wrapped->field6c && !wrapped->field70);
    CHECK(wrapped && wrapped->stamp78 == 0xffffffffu &&
          wrapped->stamp7c == 0xffffffffu);
    world.setFrameCounter(1);
    CHECK(otherWorld.frameCounter() == 1);
    WorldNode *camera = world.addTypedChild(*child, 12, "camera", 0);
    CHECK(camera && camera->type == 12 && camera->flags == 0 &&
          !camera->sectorLink.owner &&
          world.parentOwner(camera->handle) == child);
    CHECK(camera && world.loadList(12, false).back() == camera);
  }
  CHECK(!world.findInstance(instance) && !instanceLink.owner);
  CHECK(world.childOwners(root->handle) &&
        world.childOwners(root->handle)->empty());
}

static void mountedInstanceCount() {
  PackRegistry registry("ref/raw");
  GameWorld world;
  world.addRoot();
  CHECK(slotOf(registry, "system.rpk") == 0);
  WorldNode *created = world.addChild(*world.defaultParent(), "created", 0x21);
  CHECK(created);
  if (!created)
    return;
  world.noteInstanceCreated(*created);
  CHECK(world.createdInstances() == 1);
  {
    ResourceWorld resources(world, registry);
    const auto parent = resources.lookup(4, 1);
    CHECK(parent.node.packNode);
    if (parent.node.packNode) {
      std::string error;
      CHECK(world.createdInstances() == 1);
      CHECK(resources.loadEmptyGameParent(*parent.node.packNode, error));
      CHECK(world.createdInstances() == 2);
      CHECK(resources.loadEmptyGameParent(*parent.node.packNode, error));
      CHECK(world.createdInstances() == 2);
      auto &node = *parent.node.packNode;
      CHECK(resources.unloadEmptyGameParent(node, error));
      CHECK(!node.loaded() && world.createdInstances() == 1);
      const auto unloaded = world.loadList(1, false);
      CHECK(std::find(unloaded.begin(), unloaded.end(), parent.node.owner) != unloaded.end());
      CHECK(resources.unloadEmptyGameParent(node, error));
      CHECK(world.createdInstances() == 1);
      CHECK(resources.loadEmptyGameParent(node, error));
      CHECK(node.loaded() && world.createdInstances() == 2);
      node.flags |= 0x200;
      CHECK(!resources.unloadEmptyGameParent(node, error));
      CHECK(node.loaded() && world.createdInstances() == 2);
      node.flags &= ~uint32_t{0x200};
    }
  }
  CHECK(world.createdInstances() == 1);
  CHECK(world.destroy(*created));
  CHECK(world.createdInstances() == 0);
}

static void malformedGamePreparation() {
  namespace fs = std::filesystem;
  char pattern[] = "/tmp/slrr-game-payload-XXXXXX";
  const char *made = mkdtemp(pattern);
  CHECK(made);
  if (!made)
    return;
  const fs::path dir = made;
  auto bytes = makePack({}, {{0, 1, 1}});
  const uint32_t offset = static_cast<uint32_t>(bytes.size());
  put32(bytes, 0);
  put32(bytes, 0);
  put32(bytes, 0x50545845);
  put32(bytes, 4);
  put32(bytes, 1);
  writeFile(dir / "bad.rpk", bytes);
  {
    PackRegistry registry(dir);
    GameWorld world;
    CHECK(world.addRoot());
    ResourceWorld resources(world, registry);
    const uint16_t slot = slotOf(registry, "bad.rpk");
    auto found = resources.lookup((uint32_t(slot) << 16) | 1, 1);
    CHECK(found.node.packNode);
    if (found.node.packNode) {
      auto &node = *found.node.packNode;
      node.payloadOffset = offset;
      node.payloadSize = 20;
      GameResourcePayload result;
      std::string error;
      const size_t count = resources.nodeCount();
      CHECK(!resources.prepareGamePayload(node, result, error));
      CHECK(error == "type-1 external pack index is out of range");
      CHECK(resources.nodeCount() == count && !node.loaded());
      CHECK(!resources.loadPassiveSpatialGame(node, error));
      CHECK(error == "type-1 external pack index is out of range" &&
            resources.nodeCount() == count && !node.loaded() && !node.spatialPayload);
      node.payloadSize = 19;
      CHECK(!resources.prepareGamePayload(node, result, error));
      CHECK(resources.nodeCount() == count && !node.loaded());
    }
  }
  fs::remove_all(dir);
}

static void passiveExternalLoad() {
  namespace fs = std::filesystem;
  char pattern[] = "/tmp/slrr-passive-extp-XXXXXX";
  const char *made = mkdtemp(pattern);
  CHECK(made);
  if (!made) return;
  const fs::path dir = made;
  auto bytes = makePack({"other.rpk"}, {{0, 1, 1}});
  const uint32_t offset = static_cast<uint32_t>(bytes.size());
  put32(bytes, 0); put32(bytes, 0);
  put32(bytes, 0x50545845); put32(bytes, 4); put32(bytes, 1);
  writeFile(dir / "parent.rpk", bytes);
  writeFile(dir / "other.rpk", makePack({}, {}));
  {
    PackRegistry registry(dir);
    GameWorld world;
    CHECK(world.addRoot());
    ResourceWorld resources(world, registry);
    const uint16_t slot = slotOf(registry, "parent.rpk");
    const auto found = resources.lookup((uint32_t(slot) << 16) | 1, 1);
    CHECK(found.node.packNode);
    if (found.node.packNode) {
      auto &node = *found.node.packNode;
      node.payloadOffset = offset;
      node.payloadSize = 20;
      ResourceBounds bounds{};
      bounds.mode = 1;
      bounds.values = {0, 0, 0, 1, 1, 1};
      node.bounds = bounds;
      PackView view;
      std::string error;
      CHECK(registry.view(slot, view, error) == PackViewStatus::Ok && view.dependencies.size() == 1);
      CHECK(resources.loadPassiveSpatialGame(node, error));
      CHECK(node.loaded() && node.spatialPayload && node.polygonCount == 0);
      if (view.dependencies.size() == 1)
        CHECK(resources.mountState(view.dependencies[0]) == MountState::Mounted);
      CHECK(node.preparedPayloadActions == 2);
      const auto count = resources.nodeCount();
      CHECK(resources.loadPassiveSpatialGame(node, error));
      CHECK(resources.nodeCount() == count && node.preparedPayloadActions == 2);
    }
  }
  fs::remove_all(dir);
}

static void missingGameTypeCanBeRetried() {
  namespace fs = std::filesystem;
  char pattern[] = "/tmp/slrr-game-retry-XXXXXX";
  const char *made = mkdtemp(pattern);
  CHECK(made);
  if (!made)
    return;
  const fs::path dir = made;
  auto bytes = makePack({"game.rpk"}, {{0, 1, 1}});
  const uint32_t offset = static_cast<uint32_t>(bytes.size());
  put32(bytes, 0);
  put32(bytes, 0);
  const std::string text = "gametype 0x10001\n";
  put32(bytes, 0x00445352);
  put32(bytes, static_cast<uint32_t>(text.size()));
  bytes.insert(bytes.end(), text.begin(), text.end());
  writeFile(dir / "source.rpk", bytes);
  {
    PackRegistry registry(dir);
    GameWorld world;
    CHECK(world.addRoot());
    ResourceWorld resources(world, registry);
    const uint16_t slot = slotOf(registry, "source.rpk");
    const auto found = resources.lookup((uint32_t(slot) << 16) | 1, 1);
    CHECK(found.node.packNode);
    if (found.node.packNode) {
      auto &node = *found.node.packNode;
      node.payloadOffset = offset;
      node.payloadSize = static_cast<uint32_t>(bytes.size() - offset);
      GameResourcePayload result;
      std::string error;
      CHECK(!resources.prepareGamePayload(node, result, error));
      CHECK(error == "type-1 game type not found");
      CHECK(!node.gameTypeLink.owner && !node.loaded());
      writeFile(dir / "game.rpk", makePack({}, {{0, 1, 8, "game"}}));
      CHECK(resources.prepareGamePayload(node, result, error));
      CHECK(node.gameTypeLink.owner && result.runtimeGameType &&
            node.gameTypeLink.cachedValue == *result.runtimeGameType);
      CHECK(!node.loaded());
    }
  }
  fs::remove_all(dir);
}

static void zeroGameTypeClearsLink() {
  namespace fs = std::filesystem;
  char pattern[] = "/tmp/slrr-game-clear-XXXXXX";
  const char *made = mkdtemp(pattern);
  CHECK(made);
  if (!made)
    return;
  const fs::path dir = made;
  auto bytes = makePack({"game.rpk"}, {{0, 1, 1}});
  const uint32_t offset = static_cast<uint32_t>(bytes.size());
  put32(bytes, 0);
  put32(bytes, 0);
  const std::string text = "gametype 0x10001\ngametype 0\n";
  put32(bytes, 0x00445352);
  put32(bytes, static_cast<uint32_t>(text.size()));
  bytes.insert(bytes.end(), text.begin(), text.end());
  writeFile(dir / "source.rpk", bytes);
  writeFile(dir / "game.rpk", makePack({}, {{0, 1, 8, "game"}}));
  {
    PackRegistry registry(dir);
    GameWorld world;
    CHECK(world.addRoot());
    ResourceWorld resources(world, registry);
    const uint16_t slot = slotOf(registry, "source.rpk");
    const auto found = resources.lookup((uint32_t(slot) << 16) | 1, 1);
    CHECK(found.node.packNode);
    if (found.node.packNode) {
      auto &node = *found.node.packNode;
      node.payloadOffset = offset;
      node.payloadSize = static_cast<uint32_t>(bytes.size() - offset);
      GameResourcePayload result;
      std::string error;
      CHECK(resources.prepareGamePayload(node, result, error));
      CHECK(result.actions.size() == 2);
      CHECK(!node.gameTypeLink.owner && node.gameTypeLink.cachedValue == 0);
      CHECK(!node.loaded());
      CHECK(resources.prepareGamePayload(node, result, error));
      CHECK(!node.gameTypeLink.owner && !node.loaded());
    }
  }
  fs::remove_all(dir);
}

static void geometryChunkBounds() {
  namespace fs = std::filesystem;
  char pattern[] = "/tmp/slrr-game-geometry-XXXXXX";
  const char *made = mkdtemp(pattern);
  CHECK(made);
  if (!made) return;
  const fs::path dir = made;
  auto bytes = makePack({}, {{0, 1, 1}});
  const uint32_t offset = static_cast<uint32_t>(bytes.size());
  put32(bytes, 0); put32(bytes, 0);
  put32(bytes, 0x53594850); put32(bytes, 4); put32(bytes, 0);
  put32(bytes, 0x594c4f50); put32(bytes, 8); put32(bytes, 1); put32(bytes, 0);
  const uint32_t validSize = static_cast<uint32_t>(bytes.size()) - offset;
  const uint32_t badOffset = static_cast<uint32_t>(bytes.size());
  put32(bytes, 0); put32(bytes, 0);
  put32(bytes, 0x53594850); put32(bytes, 4); put32(bytes, UINT32_MAX);
  writeFile(dir / "geometry.rpk", bytes);
  {
    PackRegistry registry(dir);
    GameWorld world;
    CHECK(world.addRoot());
    ResourceWorld resources(world, registry);
    const auto slot = slotOf(registry, "geometry.rpk");
    const auto found = resources.lookup((uint32_t(slot) << 16) | 1, 1);
    CHECK(found.node.packNode);
    if (found.node.packNode) {
      auto &node = *found.node.packNode;
      node.payloadOffset = offset; node.payloadSize = validSize;
      GameResourcePayload result;
      std::string error;
      CHECK(resources.inspectGamePayload(node, result, error));
      CHECK(result.geometryChunks.size() == 2);
      if (result.geometryChunks.size() == 2)
        CHECK(std::holds_alternative<GamePhysicsChunk>(result.geometryChunks[0]) &&
              std::holds_alternative<GamePolygonChunk>(result.geometryChunks[1]));
      node.payloadOffset = badOffset; node.payloadSize = 20;
      CHECK(!resources.inspectGamePayload(node, result, error));
    }
  }
  fs::remove_all(dir);
}

static void cityGamePayload() {
  PackRegistry registry("ref/raw");
  GameWorld world;
  CHECK(world.addRoot());
  ResourceWorld resources(world, registry);
  uint16_t slot = 0;
  std::string error;
  CHECK(registry.registerPackName("maps\\city.rpk", slot, error));
  const auto found = resources.lookup((uint32_t(slot) << 16) | 1, 1);
  CHECK(found.node.packNode);
  if (!found.node.packNode)
    return;
  GameResourcePayload payload;
  CHECK(resources.inspectGamePayload(*found.node.packNode, payload, error));
  CHECK(payload.children.size() == 3);
  CHECK(payload.gameType && *payload.gameType == 0x10035);
  PackView view;
  CHECK(registry.view(slot, view, error) == PackViewStatus::Ok);
  {
    auto &node = *found.node.packNode;
    const auto savedOffset = node.payloadOffset;
    const auto savedSize = node.payloadSize;
    GameResourcePayload nested = payload;
    const auto descend = [&](uint32_t id) {
      const auto it = std::find_if(nested.children.begin(), nested.children.end(),
          [id](const GameResourceChild &child) { return child.entry.typeId == id; });
      if (it == nested.children.end()) return false;
      CHECK(it->entry.resType == 1);
      node.payloadOffset = it->entry.payloadOffset;
      node.payloadSize = it->entry.payloadSize;
      return resources.inspectGamePayload(node, nested, error);
    };
    CHECK(descend(0xd5b2));
    CHECK(descend(0xd5b3));
    CHECK(descend(0xd5b4));
    CHECK(descend(0xaa08));
    CHECK(node.payloadSize == 9952);
    CHECK(nested.geometryChunks.size() == 1 && std::holds_alternative<GamePolygonChunk>(nested.geometryChunks[0]));
    if (nested.geometryChunks.size() == 1 && std::holds_alternative<GamePolygonChunk>(nested.geometryChunks[0])) {
      const auto &chunk = std::get<GamePolygonChunk>(nested.geometryChunks[0]);
      CHECK(chunk.version == 1 && chunk.groups.size() == 1);
      if (chunk.groups.size() == 1) {
        const auto &group = chunk.groups[0];
        CHECK(group.kind == 9 && group.records.size() == 159 && group.triples.size() == 72);
        if (!group.records.empty()) {
          const std::array<uint32_t, 14> first{{0xc365ab8e, 0x3f117aa4, 0xc4c964d3, 0xb4035ae1,
              0x3f800000, 0xb2bd4d9c, 0xffffffff, 0xffffff01, 0xc0a0fd4b, 0x3f7fdf03,
              0, 0, 0xc2a0cefb, 0xc42fff45}};
          CHECK(group.records[0] == first);
        }
      }
    }
    GameResourcePayload truncated;
    --node.payloadSize;
    CHECK(!resources.inspectGamePayload(node, truncated, error));
    ++node.payloadSize;
    CHECK(descend(0xaa09));
    CHECK(node.payloadSize == 272);
    CHECK(nested.geometryChunks.size() == 1 && std::holds_alternative<GamePhysicsChunk>(nested.geometryChunks[0]));
    if (nested.geometryChunks.size() == 1 && std::holds_alternative<GamePhysicsChunk>(nested.geometryChunks[0])) {
      const auto &chunk = std::get<GamePhysicsChunk>(nested.geometryChunks[0]);
      CHECK(chunk.records.size() == 5 && chunk.triples.size() == 9);
      if (!chunk.records.empty()) {
        const std::array<uint32_t, 7> first{{9, 0, 1, 2, 0xb486ac0c, 0x3f800000, 0x2e81561a}};
        CHECK(chunk.records[0] == first);
      }
      GamePhysicsGeometry geometry;
      CHECK(decodeGamePhysicsGeometry(chunk, geometry, error));
      CHECK(geometry.vertices.size() == 9 && geometry.triangles.size() == 5);
      if (geometry.vertices.size() == 9 && geometry.triangles.size() == 5) {
        CHECK(std::fabs(geometry.vertices[0][0] + 229.6701355f) < 0.0001f);
        CHECK(std::fabs(geometry.vertices[0][2] + 1611.1507568f) < 0.0001f);
        CHECK(geometry.triangles[0].materialWord == 9);
        CHECK((geometry.triangles[0].vertices == std::array<uint32_t,3>{0,1,2}));
        CHECK(std::fabs(geometry.triangles[0].normal[1] - 1.0f) < 0.00001f);
        auto bad = chunk;
        bad.records[0][1] = 9;
        CHECK(!decodeGamePhysicsGeometry(bad, geometry, error));
        CHECK(geometry.vertices.size() == 9 && geometry.triangles[0].materialWord == 9);
        bad = chunk;
        bad.triples[0][0] = 0x7fc00000u;
        CHECK(!decodeGamePhysicsGeometry(bad, geometry, error));
        bad = chunk;
        bad.records[0][0] = 0xffffffffu;
        CHECK(decodeGamePhysicsGeometry(bad, geometry, error));
        CHECK(geometry.triangles[0].materialWord == 0xffffffffu);
      }
    }
    --node.payloadSize;
    CHECK(!resources.inspectGamePayload(node, truncated, error));
    node.payloadOffset = savedOffset;
    node.payloadSize = savedSize;
  }
  {
    ResourceBounds parent{1, {0,0,0,1,1,1}};
    ResourceBounds right{1, {3,-4,0,1,1,1}};
    ResourceBounds left{1, {-6,0,0,1,1,1}};
    const auto expanded=expandExtpBounds(parent,{right,left});
    CHECK(expanded && (expanded->values == std::array<float,6>{0,0,0,7,5,1}));
    CHECK((parent.values == std::array<float,6>{0,0,0,1,1,1}));
    left.mode=2;
    CHECK(!expandExtpBounds(parent,{right,left}));
    left.mode=1; left.values[3]=-1;
    CHECK(!expandExtpBounds(parent,{left}));
  }
  {
    uint16_t city2Slot = 0;
    CHECK(registry.registerPackName("maps\\city2.rpk", city2Slot, error));
    const auto city2 = resources.lookup((uint32_t(city2Slot) << 16) | 1, 1);
    CHECK(city2.node.packNode);
    if (city2.node.packNode) {
      auto &node = *city2.node.packNode;
      const auto savedOffset = node.payloadOffset, savedSize = node.payloadSize;
      GameResourcePayload nested;
      CHECK(resources.inspectGamePayload(node, nested, error));
      PreparedSpatialGamePayload spatial;
      CHECK(!prepareSpatialGamePayload(nested, spatial, error)); // city2 needs its ground factory
      CHECK(!node.loaded() && !node.groundFactory);
      CHECK(node.bounds && nested.children.size() == 3);
      if (node.bounds && nested.children.size() == 3) {
        std::vector<ResourceBounds> childBounds;
        for (const auto &child : nested.children) {
          CHECK(child.entry.transform.has_value());
          if (child.entry.transform) {
            ResourceBounds bounds;
            bounds.mode=1;
            for (size_t i=0;i<6;++i) bounds.values[i]=(*child.entry.transform)[i];
            childBounds.push_back(bounds);
          }
        }
        const auto expanded=expandExtpBounds(*node.bounds, childBounds);
        CHECK(expanded && expanded->mode == 1 && expanded->values[0] == node.bounds->values[0]);
        if (expanded) for (const auto &child : childBounds) for (size_t axis=0;axis<3;++axis) {
          CHECK(child.values[axis]-child.values[axis+3] >=
                expanded->values[axis]-expanded->values[axis+3]-0.001f);
          CHECK(child.values[axis]+child.values[axis+3] <=
                expanded->values[axis]+expanded->values[axis+3]+0.001f);
        }
      }
      for (const uint32_t id : {0xcba0u,0xcba1u,0xd692u,0xdc2fu,0xdc60u,0xe249u,
                                0xe43du,0xe43eu,0xe530u,0xe549u,0xe55cu,0xe55du,
                                0x98cdu,0x98cfu,0x98d8u}) {
        const auto it = std::find_if(nested.children.begin(), nested.children.end(),
            [id](const GameResourceChild &child) { return child.entry.typeId == id; });
        if (it == nested.children.end()) { CHECK(false); break; }
        node.payloadOffset = it->entry.payloadOffset;
        node.payloadSize = it->entry.payloadSize;
        if (!resources.inspectGamePayload(node, nested, error)) { CHECK(false); break; }
        if (id == 0x98cdu) {
          CHECK(prepareSpatialGamePayload(nested, spatial, error));
          CHECK(spatial.children.size() == 4 && spatial.polygons && !spatial.physics);
          if (spatial.polygons) CHECK(!spatial.polygons->groups.empty());
        }
      }
      CHECK(nested.geometryChunks.size() == 1 &&
            std::holds_alternative<GamePhysicsChunk>(nested.geometryChunks[0]));
      if (nested.geometryChunks.size() == 1 && std::holds_alternative<GamePhysicsChunk>(nested.geometryChunks[0])) {
        CHECK(prepareSpatialGamePayload(nested, spatial, error));
        CHECK(spatial.children.empty() && !spatial.polygons && spatial.physics &&
              spatial.physics->triangles.size() == 7);
        auto invalid=nested;
        std::get<GamePhysicsChunk>(invalid.geometryChunks[0]).records[0][1]=0xffffffffu;
        CHECK(!prepareSpatialGamePayload(invalid, spatial, error));
        CHECK(spatial.physics && spatial.physics->triangles.size() == 7);
        GamePhysicsGeometry geometry;
        CHECK(decodeGamePhysicsGeometry(std::get<GamePhysicsChunk>(nested.geometryChunks[0]), geometry, error));
        CHECK(geometry.triangles.size() == 7 && geometry.vertices.size() == 9);
        if (geometry.triangles.size() == 7 && geometry.vertices.size() == 9) {
          const auto &face = geometry.triangles[4];
          CHECK(face.materialWord == 6 && (face.vertices == std::array<uint32_t,3>{6,1,0}));
          const auto &a=geometry.vertices[face.vertices[0]], &b=geometry.vertices[face.vertices[1]],
                     &c=geometry.vertices[face.vertices[2]];
          const float x=-278.518f, z=1033.002f;
          const float det=(b[2]-c[2])*(a[0]-c[0])+(c[0]-b[0])*(a[2]-c[2]);
          const float u=((b[2]-c[2])*(x-c[0])+(c[0]-b[0])*(z-c[2]))/det;
          const float v=((c[2]-a[2])*(x-c[0])+(a[0]-c[0])*(z-c[2]))/det;
          CHECK(u>=0 && v>=0 && u+v<=1 && std::fabs(face.normal[1]-1.0f)<0.0001f);
          CHECK(std::fabs(u*a[1]+v*b[1]+(1-u-v)*c[1]-9.81177f)<0.001f);
          const auto hit=queryGamePhysicsFace(geometry, 4, {x,9.8f,z}, {x,10.8f,z});
          CHECK(hit.status == GamePhysicsHitStatus::Hit && hit.materialWord == 6 &&
                std::fabs(hit.point[1]-9.81177f)<0.001f && hit.fraction>0 && hit.fraction<0.02f);
          CHECK(queryGamePhysicsFace(geometry, 4, {x,10.8f,z}, {x,9.8f,z}).status == GamePhysicsHitStatus::Miss);
          CHECK(queryGamePhysicsFace(geometry, 4, {x+100,9.8f,z}, {x+100,10.8f,z}).status == GamePhysicsHitStatus::Miss);
          // 0049fa60's coplanar-graze branch clips the segment against the
          // triangle with the interior on the non-positive side of each edge
          // plane. A vertical graze projects both ends to the same point:
          // f0 == f1, so the clip never crosses an edge and stock misses it.
          CHECK(queryGamePhysicsFace(geometry, 4, {x, 9.81f, z},
                                     {x, 9.82f, z})
                    .status == GamePhysicsHitStatus::Miss);
          // The literal 0049fa60 clip misses both a shallow graze whose
          // projection crosses the face and one that starts inside and
          // leaves: the interior side follows the winding, so the overlap
          // test (exit <= enter breaks) never confirms those overlaps. Stock
          // observable behavior is no contact from grazing faces, which is
          // what the car step relies on (no abort). A graze far outside also
          // misses.
          CHECK(queryGamePhysicsFace(geometry, 4, {x - 2, 9.80f, z},
                                     {x + 2, 9.82f, z})
                    .status == GamePhysicsHitStatus::Miss);
          CHECK(queryGamePhysicsFace(geometry, 4, {x - 2, 9.80f, z + 50},
                                     {x + 2, 9.82f, z + 50})
                    .status == GamePhysicsHitStatus::Miss);
          // A graze that starts inside and leaves, like the other grazing
          // configurations, misses: the literal clip's overlap confirmation
          // never survives all three edges on normally-wound faces.
          // A graze starting on the interior side of every edge and leaving
          // through one survives the literal clip and reports the leaving
          // fraction (max of the f0 >= 0 branch updates), with the mirrored
          // point formula start + (start - end) * fraction.
          const auto leaving = queryGamePhysicsFace(
              geometry, 4, {x, 9.80f, z}, {x + 3, 9.82f, z});
          CHECK(leaving.status == GamePhysicsHitStatus::Hit &&
                leaving.fraction > 0 && leaving.fraction < 1 &&
                leaving.point[0] < x &&
                std::fabs(leaving.point[1] - 9.80f) < 0.02f);
          CHECK(queryGamePhysicsFace(geometry, 99, {x,9.8f,z}, {x,10.8f,z}).status == GamePhysicsHitStatus::Unsupported);
        }
      }
      node.payloadOffset = savedOffset;
      node.payloadSize = savedSize;
    }
  }
  // Synthetic 0049fa60 coplanar-graze cases on a controlled triangle: the
  // literal clip misses every grazing configuration here (vertical-inside
  // projects both ends to one point, the crossing graze fails the
  // exit <= enter break on this winding), which is what the car step relies
  // on when a probe runs parallel to a face.
  {
    GamePhysicsGeometry g;
    g.vertices = {{{-2, 0, -2}, {0, 0, 2}, {2, 0, -2}}};
    GamePhysicsTriangle t;
    t.materialWord = 6;
    t.vertices = {0, 1, 2};
    t.normal = {0, 1, 0};
    g.triangles = {t};
    CHECK(queryGamePhysicsFace(g, 0, {0, 0.001f, 0}, {0, -0.001f, 0})
              .status == GamePhysicsHitStatus::Miss);
    CHECK(queryGamePhysicsFace(g, 0, {0, -0.001f, 0}, {0, 0.001f, 0})
              .status == GamePhysicsHitStatus::Miss);
    CHECK(queryGamePhysicsFace(g, 0, {-2, -0.001f, 0}, {2, 0.001f, 0})
              .status == GamePhysicsHitStatus::Miss);
    CHECK(queryGamePhysicsFace(g, 0, {0, -0.001f, 0}, {3, 0.001f, 0})
              .status == GamePhysicsHitStatus::Miss);
    CHECK(queryGamePhysicsFace(g, 0, {-2, -0.001f, 50}, {2, 0.001f, 50})
              .status == GamePhysicsHitStatus::Miss);
    // A perpendicular crossing uses the regular branch and hits.
    const auto hit = queryGamePhysicsFace(g, 0, {0, -1, 0}, {0, 1, 0});
    CHECK(hit.status == GamePhysicsHitStatus::Hit && hit.fraction == 0.5f &&
          hit.materialWord == 6 && hit.point[1] == 0);
  }
  CHECK(!view.dependencies.empty());
  if (!view.dependencies.empty())
    CHECK(payload.runtimeGameType &&
          *payload.runtimeGameType ==
              (uint32_t(view.dependencies[0]) << 16 | 0x35));
  if (payload.children.size() == 3) {
    CHECK(payload.children[0].entry.resType == 1 &&
          payload.children[1].entry.resType == 1 &&
          payload.children[2].entry.resType == 1);
    CHECK(payload.children[0].runtimeId == (uint32_t(slot) << 16 | 0xd5b2));
    CHECK(payload.children[0].parentId == found.node.id);
    CHECK(payload.children[0].runtimeFlags ==
          (payload.children[0].entry.flags | 8));
  }
  CHECK(payload.parameters &&
        *payload.parameters ==
            "0x00000010,0,0,0,0,0,0,maps\\city\\meshes\\city.scm,"
            "maps\\city\\ai\\city.trc");
  CHECK(payload.externalTypes.size() == 1 && payload.externalTypes[0] == 3);
  CHECK(!found.node.packNode->loaded());
  const size_t oldChildren = found.node.packNode->children.size();
  const auto boundsBeforeExtp = found.node.packNode->bounds;
  found.node.packNode->flags |= 0x80000;
  CHECK(!resources.registerGamePayloadChildren(*found.node.packNode, error));
  CHECK(found.node.packNode->children.size() == oldChildren);
  found.node.packNode->flags &= ~uint32_t{0x80000};
  CHECK(resources.registerGamePayloadChildren(*found.node.packNode, error));
  CHECK(!found.node.packNode->loaded());
  CHECK(found.node.packNode->children.size() == oldChildren + payload.children.size());
  if (found.node.packNode->children.size() == oldChildren + payload.children.size()) {
    for (size_t i = 0; i < payload.children.size(); ++i) {
      const auto *child = found.node.packNode->children[oldChildren + i];
      CHECK(child->value == payload.children[i].runtimeId);
      CHECK(child->parent == found.node.packNode);
      CHECK(child->parentId == found.node.id);
      CHECK((child->flags & 8) != 0);
    }
  }
  if (found.node.packNode->children.size() == oldChildren + 3) {
    auto *child = found.node.packNode->children[oldChildren];
    PackResource topLevel;
    CHECK(!registry.lookup(child->value, topLevel, error));
    GameResourcePayload nested;
    CHECK(resources.inspectGamePayload(*child, nested, error));
    CHECK(nested.children.size() == 2);
    CHECK(resources.registerGamePayloadChildren(*child, error));
    CHECK(child->children.size() == 2);
  }
  CHECK(resources.registerGamePayloadChildren(*found.node.packNode, error));
  CHECK(found.node.packNode->children.size() == oldChildren + payload.children.size());
  CHECK(!found.node.packNode->loaded());
  GameResourcePayload prepared;
  CHECK(resources.prepareGamePayload(*found.node.packNode, prepared, error));
  if (boundsBeforeExtp) {
    std::vector<ResourceBounds> allChildren;
    for (const auto *child : found.node.packNode->children) {
      CHECK(child->bounds.has_value());
      if (child->bounds) allChildren.push_back(*child->bounds);
    }
    const auto expected = expandExtpBounds(*boundsBeforeExtp, allChildren);
    CHECK(expected && found.node.packNode->bounds &&
          expected->values == found.node.packNode->bounds->values);
  } else CHECK(false);
  CHECK(prepared.runtimeGameType == payload.runtimeGameType);
  CHECK(prepared.parameters == payload.parameters);
  CHECK(view.dependencies.size() >= 3);
  if (view.dependencies.size() >= 3)
    CHECK(resources.mountState(view.dependencies[2]) == MountState::Mounted);
  CHECK(found.node.packNode->gameTypeLink.cachedValue == *payload.runtimeGameType);
  CHECK(!found.node.packNode->loaded());
  const auto countAfterPreparation = resources.nodeCount();
  CHECK(!resources.unloadEmptyGameParent(*found.node.packNode, error));
  CHECK(resources.nodeCount() == countAfterPreparation);
  CHECK(!found.node.packNode->loaded());
  CHECK(resources.prepareGamePayload(*found.node.packNode, prepared, error));
  CHECK(resources.nodeCount() == countAfterPreparation);
  CHECK(!found.node.packNode->loaded());
  std::vector<ResourceNode *> preserved;
  for (ResourceNode *child : found.node.packNode->children)
    if (std::find(found.node.packNode->payloadChildren.begin(),
                  found.node.packNode->payloadChildren.end(), child) ==
        found.node.packNode->payloadChildren.end())
      preserved.push_back(child);
  CHECK(!preserved.empty());
  OwnerLink childLink;
  CHECK(!found.node.packNode->payloadChildren.empty());
  if (!found.node.packNode->payloadChildren.empty()) {
    ResourceNode *child = found.node.packNode->payloadChildren.front();
    setLinkOwner(childLink, child);
    child->flags |= 0x200;
    CHECK(!resources.resetPreparedGamePayload(*found.node.packNode, error));
    CHECK(childLink.owner == child && resources.nodeCount() == countAfterPreparation);
    child->flags &= ~uint32_t{0x200};
    found.node.packNode->groundPreparation = std::make_shared<GroundPreparation>();
    auto *body = world.addTypedChildUnderExternal(
        found.node.packNode->handle, *found.node.packNode, 1,
        found.node.packNode->sectorLink.owner, 2, "groundbody", 0x201);
    CHECK(body);
    OwnerLink bodyObserver;
    const auto countBeforeBody = world.createdInstances();
    if (body) {
      CHECK((body->flags & 0x301) == 0x301);
      world.noteInstanceCreated(*body);
      world.markLoaded(*body);
      CHECK(world.createdInstances() == countBeforeBody + 1);
      setLinkOwner(found.node.packNode->groundPreparation->bodyResource, body);
      setLinkOwner(bodyObserver, body);
    }
    auto *render = world.addTypedChildUnderExternal(
        found.node.packNode->handle, *found.node.packNode, 1,
        found.node.packNode->sectorLink.owner, 3, "r_ground", 0x201);
    CHECK(render);
    OwnerLink renderObserver;
    if (render) {
      CHECK((render->flags & 0x301) == 0x301);
      world.noteInstanceCreated(*render);
      CHECK(!render->loaded && !resources.sceneRenderInstance(resources.resolve(render)));
      setLinkOwner(found.node.packNode->groundPreparation->renderResource, render);
      setLinkOwner(renderObserver, render);
    }
    std::weak_ptr<GroundPreparation> preparation = found.node.packNode->groundPreparation;
    const bool reset = resources.resetPreparedGamePayload(*found.node.packNode, error);
    CHECK(!reset || (preparation.expired() && !bodyObserver.owner &&
                     !renderObserver.owner && world.createdInstances() == countBeforeBody));
    setLinkOwner(bodyObserver, nullptr);
    setLinkOwner(renderObserver, nullptr);
    if (!reset) std::printf("reset error: %s\n", error.c_str());
    CHECK(reset);
    CHECK(!childLink.owner && found.node.packNode->children == preserved);
    for (ResourceNode *kept : preserved) {
      CHECK(world.parentOwner(kept->handle) == found.node.owner);
      const auto lookup = resources.lookup(kept->value, kept->type);
      CHECK(lookup.node.packNode == kept);
    }
    CHECK(!found.node.packNode->gameTypeLink.owner && !found.node.packNode->loaded());
    CHECK(resources.prepareGamePayload(*found.node.packNode, prepared, error));
    CHECK(found.node.packNode->children.size() == preserved.size() + payload.children.size());
    for (ResourceNode *child : preserved)
      CHECK(std::find(found.node.packNode->children.begin(), found.node.packNode->children.end(), child) != found.node.packNode->children.end());
    CHECK(found.node.packNode->gameTypeLink.owner && !found.node.packNode->loaded());
    CHECK(resources.prepareGamePayload(*found.node.packNode, prepared, error));
    CHECK(found.node.packNode->children.size() == preserved.size() + payload.children.size());
    setLinkOwner(childLink, nullptr);
  }
}

int main() {
  typeTable();
  meshInventory();
  viewportAttachments();
  cameraResource();
  systemMount();
  referenceSet();
  frontendMount();
  failures_();
  createsOnMountedScript();
  sharedParentage();
  mountedInstanceCount();
  malformedGamePreparation();
  passiveExternalLoad();
  missingGameTypeCanBeRetried();
  zeroGameTypeClearsLink();
  geometryChunkBounds();
  cityGamePayload();
  if (failures == 0)
    std::printf("resource_world_test: ok\n");
  return failures ? 1 : 0;
}
