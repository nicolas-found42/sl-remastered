#include "scene_raster3d.hpp"
#include "car_tint.hpp"
#include "car_runtime.hpp"
#include "game_spatial_payload.hpp"
#include "ground_runtime.hpp"
#include "render2d.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"
#include "scene_2d.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace {
void note(const std::string &s) {
  static std::set<std::string> seen;
  if (seen.insert(s).second)
    runlog::event(runlog::EventKind::Note, "world raster: %s", s.c_str());
}
// Per-layer draw statistics for the run log, and an optional layer filter
// (SLRR_RASTER_LAYERS=ground,objects,car) to see each layer on its own.
struct LayerStats {
  size_t pieces = 0, textured = 0, noTextureSlot = 0, textureMissing = 0,
         noUv = 0, blended = 0, skipped = 0;
  bool operator==(const LayerStats &o) const {
    return pieces == o.pieces && textured == o.textured &&
           noTextureSlot == o.noTextureSlot &&
           textureMissing == o.textureMissing && noUv == o.noUv &&
           blended == o.blended && skipped == o.skipped;
  }
};
bool layerEnabled(const char *layer) {
  static const std::string filter = [] {
    const char *value = std::getenv("SLRR_RASTER_LAYERS");
    return value ? std::string(",") + value + "," : std::string();
  }();
  return filter.empty() ||
         filter.find(std::string(",") + layer + ",") != std::string::npos;
}
} // namespace

bool sceneGroundMesh(const GamePolygonGroup &group, ScxMesh &mesh,
                     std::string &error) {
  std::vector<Raster3DGroundVertex> vertices;
  if (!raster3dGroundFvf(group.records.data(), group.records.size(), vertices,
                         error))
    return false;
  ScxMesh result;
  result.vertexCount = static_cast<uint32_t>(vertices.size());
  result.positions.reserve(vertices.size() * 3);
  result.normals.reserve(vertices.size() * 3);
  result.uv1.reserve(vertices.size() * 2);
  for (const auto &v : vertices) {
    result.positions.insert(result.positions.end(), v.position.begin(),
                            v.position.end());
    result.normals.insert(result.normals.end(), v.normal.begin(),
                          v.normal.end());
    result.uv1.insert(result.uv1.end(), v.uv[0].begin(), v.uv[0].end());
  }
  for (const auto &tri : group.triples)
    for (auto index : tri) {
      if (index >= vertices.size()) {
        error = "ground polygon index out of range";
        return false;
      }
      result.indices.push_back(index);
    }
  mesh = std::move(result);
  error.clear();
  return true;
}

SceneRaster3D::~SceneRaster3D() {
  for (auto &o : objects_)
    setLinkOwner(*o, nullptr);
}
void SceneRaster3D::addObject(LinkOwner &owner) {
  if (std::any_of(objects_.begin(), objects_.end(),
                  [&](const auto &p) { return p->owner == &owner; }))
    return;
  auto link = std::make_unique<OwnerLink>();
  setLinkOwner(*link, &owner);
  objects_.push_back(std::move(link));
}
void SceneRaster3D::remove(LinkOwner &owner) {
  objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                [&](auto &p) {
                                  if (p->owner != &owner && p->owner)
                                    return false;
                                  setLinkOwner(*p, nullptr);
                                  return true;
                                }),
                 objects_.end());
}

void SceneRaster3D::draw(ResourceWorld &resources, Render2D &renderer,
                         const std::vector<ResourceNode *> &ground,
                         const std::array<float, 16> &view,
                         const std::array<float, 16> &projection,
                         const ViewportInstance &viewport,
                         const CameraFog *fog) {
  int width = 0, height = 0;
  std::string error;
  if (!renderer.outputSize(width, height, error) || width <= 0 || height <= 0) {
    note(error);
    return;
  }
  if ((raster_.width() != static_cast<uint32_t>(width) ||
       raster_.height() != static_cast<uint32_t>(height)) &&
      !raster_.resize(width, height, error)) {
    note(error);
    return;
  }
  raster_.clear(0, 0, 0, 0);
  struct Piece {
    const ScxMesh *mesh;
    Raster3DDraw draw;
    float depth = 0;
    const char *layer = "";
  };
  std::vector<Piece> pieces;
  std::vector<std::shared_ptr<ScxModel>> retainedModels;
  const auto texture = [&](const SceneMaterialResources &material,
                           size_t index) -> const Raster3DTexture * {
    if (index >= material.textureCount())
      return nullptr;
    const auto id = material.textureId(index);
    auto found = textures_.find(id);
    if (found != textures_.end())
      return &found->second;
    const auto node = resources.lookup(id, 7);
    if (node.status != LookupStatus::Found || !node.node.owner)
      return nullptr;
    SceneMaterialResources single(nullptr, {node.node.owner});
    ResourceTexturePixels pixels;
    if (!sceneRefreshTexture(resources, single, resources.packs().root(),
                             pixels, error, false)) {
      note(error);
      return nullptr;
    }
    Raster3DTexture result;
    if (!Raster3DTexture::build(pixels.width, pixels.height, pixels.rgba.data(),
                                pixels.rgba.size(), result, error)) {
      note(error);
      return nullptr;
    }
    return &textures_.emplace(id, std::move(result)).first->second;
  };
  std::map<std::string, LayerStats> stats;
  // A part's tint belongs to the body record that names its render resource
  // (FUN_0048c8c0 writes +0xcc per record).
  const auto bodyTintFor = [](WorldNode *carNode,
                              const CarRuntimeInstance &inst)
      -> std::optional<uint32_t> {
    if (!carNode || !carNode->carInstance)
      return std::nullopt;
    return lookupBodyTint(carNode->carInstance->bodyTints, inst.renderType);
  };
  const auto append = [&](const ScxMesh &mesh, const ScxMaterial &material,
                          const SceneMaterialResources &resourcesMaterial,
                          const std::array<float, 16> &model, bool isGround,
                          const char *layer,
                          std::optional<uint32_t> tint = std::nullopt) {
    auto &layerStats = stats[layer];
    if (!layerEnabled(layer)) {
      ++layerStats.skipped;
      return;
    }
    if (!mesh.vertexCount || mesh.positions.size() != mesh.vertexCount * 3 ||
        mesh.indices.empty())
      return;
    Raster3DScxMaterial mapped;
    if (isGround && material.scyV3) {
      mapped.state.diffuse = material.diffuse;
      const auto id = material.scyV3->textureIndices[0];
      if (id >= 0)
        mapped.baseTextureIndex = static_cast<uint32_t>(id);
      if ((material.flags & 0x1000u)) {
        mapped.state.blend = Raster3DBlend::AlphaBlend;
        mapped.state.depthWrite = false;
      }
    } else if (!raster3dScxMaterial(material, {true, true}, mapped, error)) {
      note(error);
      return;
    }
    if (mapped.baseTextureIndex && mesh.uv1.size() == mesh.vertexCount * 2)
      mapped.state.texture =
          texture(resourcesMaterial, *mapped.baseTextureIndex);
    ++layerStats.pieces;
    static std::set<std::string> sampled;
    static std::map<std::string, int> sampleCount;
    // Raw mesh extent before positionScale, to identify the stock unit.
    float meshBounds[6] = {0, 0, 0, 0, 0, 0};
    for (size_t v = 0; v < mesh.vertexCount; ++v) {
      const float x = mesh.positions[v * 3], y = mesh.positions[v * 3 + 1],
                  z = mesh.positions[v * 3 + 2];
      if (v == 0) {
        meshBounds[0] = meshBounds[3] = x;
        meshBounds[1] = meshBounds[4] = y;
        meshBounds[2] = meshBounds[5] = z;
      }
      meshBounds[0] = std::min(meshBounds[0], x);
      meshBounds[1] = std::min(meshBounds[1], y);
      meshBounds[2] = std::min(meshBounds[2], z);
      meshBounds[3] = std::max(meshBounds[3], x);
      meshBounds[4] = std::max(meshBounds[4], y);
      meshBounds[5] = std::max(meshBounds[5], z);
    }
    if (sampled.size() < 12 && sampleCount[layer]++ < 4)
      runlog::event(
          runlog::EventKind::Note,
          "world raster %s sample %zu: flags=0x%x diffuse=%.2f,%.2f,%.2f,%.2f "
          "texSlot=%d texture=%d verts=%u tris=%zu uv=%d "
          "bounds=%.1f,%.1f,%.1f..%.1f,%.1f,%.1f",
          layer, layerStats.pieces, material.flags, material.diffuse[0],
          material.diffuse[1], material.diffuse[2], material.diffuse[3],
          mapped.baseTextureIndex ? int(*mapped.baseTextureIndex) : -1,
          mapped.state.texture ? 1 : 0, mesh.vertexCount,
          mesh.indices.size() / 3, int(mesh.uv1.size() == mesh.vertexCount * 2),
          meshBounds[0], meshBounds[1], meshBounds[2], meshBounds[3],
          meshBounds[4], meshBounds[5]);
    if (mapped.state.texture)
      ++layerStats.textured;
    else if (!mapped.baseTextureIndex)
      ++layerStats.noTextureSlot;
    else if (mesh.uv1.size() != mesh.vertexCount * 2)
      ++layerStats.noUv;
    else
      ++layerStats.textureMissing;
    if (mapped.state.blend == Raster3DBlend::AlphaBlend)
      ++layerStats.blended;
    if (!mapped.state.texture) {
      mapped.state.diffuse = material.diffuse;
      if (mapped.state.diffuse[0] + mapped.state.diffuse[1] +
              mapped.state.diffuse[2] <
          0.05f)
        mapped.state.diffuse = {0.6f, 0.6f, 0.6f, 1};
      if (mapped.state.blend != Raster3DBlend::AlphaBlend)
        mapped.state.diffuse[3] = 1;
    } else if (isGround)
      mapped.state.diffuse = {1, 1, 1, 1};
    if (tint) {
      // FUN_0048c8c0 stores colors[scheme % count] on the body's render
      // resource (+0xcc). The palette entries in the shipped cfgs are
      // 0x00RRGGBB (e.g. Taxi's 0x00f3be00), so the high byte is NOT an
      // opacity: only RGB is a tint, and the material keeps its own alpha.
      applyBodyTint(mapped.state.diffuse, *tint);
    }
    Piece piece;
    piece.layer = layer;
    piece.mesh = &mesh;
    piece.draw.model = model;
    // Ground mesh vertices are already world-space (measured: sector meshes
    // span the sector in metres around the car), while part/object meshes are
    // stored in centimetre units. Scaling ground by 10 put the whole city
    // outside the view.
    static bool groundModelLogged = false;
    if (isGround && !groundModelLogged) {
      groundModelLogged = true;
      runlog::event(
          runlog::EventKind::Note,
          "ground draw view: row3=(%.1f,%.1f,%.1f,%.1f) row0=(%.3f,%.3f,%.3f)",
          view[12], view[13], view[14], view[15], view[0], view[1], view[2]);
      runlog::event(runlog::EventKind::Note,
                    "ground draw proj: p0=(%.4f,%.4f) p5=(%.4f,%.4f) "
                    "p10=%.4f p14=%.2f",
                    projection[0], projection[8], projection[5], projection[10],
                    projection[10], projection[14]);
    }
    piece.draw.positionScale = isGround ? 10.0f : 0.1f;
    piece.draw.view = view;
    piece.draw.projection = projection;
    piece.draw.viewport = {viewport.left, viewport.top, viewport.width,
                           viewport.height};
    piece.draw.material = mapped.state;
    if (fog && fog->farDistance > fog->nearDistance) {
      const auto color = static_cast<uint32_t>(fog->color);
      piece.draw.material.fogNear = fog->nearDistance;
      piece.draw.material.fogFar = fog->farDistance;
      piece.draw.material.fogColor = {float((color >> 16) & 255) / 255.0f,
                                      float((color >> 8) & 255) / 255.0f,
                                      float(color & 255) / 255.0f};
    }
    piece.depth = model[12] * view[2] + model[13] * view[6] +
                  model[14] * view[10] + view[14];
    pieces.push_back(std::move(piece));
  };
  for (auto *leaf : ground) {
    if (!leaf || !leaf->spatialPayload || !leaf->spatialPayload->polygons)
      continue;
    const auto sector = resources.resolve(leaf->sectorLink.owner);
    if (!sector.packNode || !sector.packNode->groundFactory ||
        !sector.packNode->groundFactory->preparation)
      continue;
    const auto &g = *sector.packNode->groundFactory->preparation;
    if (!g.pendingMesh || !g.pendingMaterial || !g.pendingRender)
      continue;
    g.pendingRender->processRootBoneUpdate();
    g.pendingRender->refreshSingleBoneDrawMatrix();
    const auto &groups = leaf->spatialPayload->polygons->groups;
    auto &cached = groundMeshes_[leaf->value];
    if (cached.payload.lock() != leaf->spatialPayload) {
      cached.payload = leaf->spatialPayload;
      cached.meshes.clear();
      cached.meshes.resize(groups.size());
      for (size_t i = 0; i < groups.size(); ++i)
        if (!sceneGroundMesh(groups[i], cached.meshes[i], error))
          note(error);
    }
    for (size_t i = 0; i < groups.size(); ++i) {
      if (groups[i].kind >= g.pendingMesh->materials.size())
        continue;
      append(cached.meshes[i], g.pendingMesh->materials[groups[i].kind],
             *g.pendingMaterial, g.pendingRender->drawMatrix(), true, "ground");
    }
  }
  objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                [](const auto &p) { return !p->owner; }),
                 objects_.end());
  for (const auto &owner : objects_) {
    const auto node = resources.resolve(owner->owner);
    const auto instance = node ? resources.sceneRenderInstance(node) : nullptr;
    if (!instance || instance->textState())
      continue;
    const auto type = resources.lookup(instance->typeId(), 14);
    auto material = type.status == LookupStatus::Found
                        ? resources.sceneMaterial(type.node)
                        : nullptr;
    if (!material)
      continue;
    const auto meshNode = resources.lookup(material->meshId(), 5);
    auto model = meshNode.status == LookupStatus::Found
                     ? resources.sceneMesh(meshNode.node)
                     : nullptr;
    if (!model) {
      auto loaded = std::make_shared<ScxModel>();
      if (!sceneLoadMesh(resources, material->meshId(),
                         resources.packs().root(), *loaded, error)) {
        note(error);
        continue;
      }
      model = loaded;
      if (meshNode.status == LookupStatus::Found)
        resources.bindSceneMesh(meshNode.node, model);
    }
    retainedModels.push_back(model);
    instance->processRootBoneUpdate();
    instance->refreshSingleBoneDrawMatrix();
    for (const auto &mesh : model->meshes) {
      if (!mesh.materialIndex || *mesh.materialIndex >= model->materials.size())
        continue;
      append(mesh, model->materials[*mesh.materialIndex], *material,
             instance->drawMatrix(), false, "objects");
    }
  }
  // Draw every car: the chassis at car_root plus every installed part
  // (wheels included) at its own physics body pose, like stock render
  // instances per part. The player car, the dealer showroom cars and garage
  // cars are all car nodes; none is special-cased.
  if (!ground.empty()) {
    unsigned drawnParts = 0, skippedParts = 0;
    for (const uint32_t carId : resources.world().carInstanceIds()) {
    auto *carNode = resources.world().findInstance(carId);
    auto *car = carNode ? carNode->carInstance.get() : nullptr;
    auto *partNode = car && car->chassisObject
                         ? resources.world().findByReceiver(car->chassisObject)
                         : carNode;
    auto *part =
        partNode && partNode->carInstance ? partNode->carInstance.get() : car;
    auto *bodyNode =
        car ? resources.world().nodeFor(car->physicsRoot.owner) : nullptr;
    auto body =
        bodyNode ? resources.world().dynamicBody(*bodyNode).lock() : nullptr;
    std::function<void(WorldNode *, const char *, physics::Body *)>
        drawCarPart = [&](WorldNode *node, const char *label,
                          physics::Body *fallbackBody) {
          if (!node || !node->carInstance) {
            ++skippedParts;
            return;
          }
          auto &inst = *node->carInstance;
          physics::Body *partBody = nullptr;
          if (inst.partPhysics && inst.partPhysics->root &&
              inst.partPhysics->root->current < 2)
            partBody = inst.partPhysics->root.get();
          if (!partBody)
            partBody = fallbackBody;
          if (!partBody || partBody->current > 1) {
            ++skippedParts;
            return;
          }
          const auto renderType = inst.renderType
                                      ? resources.lookup(inst.renderType, 14)
                                      : LookupResult{};
          auto partMaterial = renderType.status == LookupStatus::Found
                                  ? resources.sceneMaterial(renderType.node)
                                  : nullptr;
          // The car's type-14 render material (mesh, one texture per mesh
          // material) has not been bound by anything else: load and bind it
          // once.
          if (!partMaterial && renderType.status == LookupStatus::Found &&
              inst.renderType) {
            auto loaded = sceneLoadMaterial(resources, part->renderType);
            if (loaded.material) {
              std::shared_ptr<SceneMaterialResources> shared(
                  std::move(loaded.material));
              if (resources.bindSceneMaterial(renderType.node, shared))
                partMaterial = shared;
            } else {
              note("car render material: " + loaded.error);
            }
          }
          const uint32_t meshId = inst.meshId    ? inst.meshId
                                  : partMaterial ? partMaterial->meshId()
                                                 : 0;
          if (!meshId) {
            ++skippedParts;
            return;
          }
          {
            const auto meshNode = resources.lookup(meshId, 5);
            auto model = meshNode.status == LookupStatus::Found
                             ? resources.sceneMesh(meshNode.node)
                             : nullptr;
            if (!model) {
              auto loaded = std::make_shared<ScxModel>();
              if (sceneLoadMesh(resources, meshId, resources.packs().root(),
                                *loaded, error)) {
                model = loaded;
                if (meshNode.status == LookupStatus::Found)
                  resources.bindSceneMesh(meshNode.node, model);
              } else
                note(error);
            }
            if (model) {
              retainedModels.push_back(model);
              static bool carInfoLogged = false;
              if (!carInfoLogged) {
                carInfoLogged = true;
                runlog::event(
                    runlog::EventKind::Note,
                    "world raster car source: renderType=0x%x (%s) "
                    "partMaterial=%d textures=%zu meshId=0x%x part "
                    "textureId=0x%x meshes=%zu materials=%zu",
                    inst.renderType,
                    renderType.status == LookupStatus::Found ? "found"
                                                             : "missing",
                    partMaterial ? 1 : 0,
                    partMaterial ? partMaterial->textureCount() : size_t(0),
                    meshId, inst.textureId, model->meshes.size(),
                    model->materials.size());
              }
              const auto texNode = resources.lookup(inst.textureId, 7);
              std::vector<LinkOwner *> links;
              if (texNode.status == LookupStatus::Found && texNode.node.owner)
                links.push_back(texNode.node.owner);
              SceneMaterialResources material(nullptr, links);
              SceneRenderInstance transform(nullptr);
              const auto &state = partBody->state[partBody->current];
              transform.setBoneTransform(state.pos, state.rot);
              transform.processRootBoneUpdate();
              transform.refreshSingleBoneDrawMatrix();
              for (const auto &mesh : model->meshes)
                if (mesh.materialIndex &&
                    *mesh.materialIndex < model->materials.size())
                  append(mesh, model->materials[*mesh.materialIndex],
                         partMaterial ? *partMaterial : material,
                         transform.drawMatrix(), false, label,
                         bodyTintFor(carNode, inst));
              ++drawnParts;
            }
          }
        };
    // The chassis node draws at the car root body (the compound root lives
    // on the car record, not on the chassis part); the chassis receiver may
    // resolve to a node without its own car instance, in which case the car
    // node's instance carries the mesh.
    drawCarPart(partNode && partNode->carInstance ? partNode : carNode, "car",
                body.get());
    // Installed parts (wheels, panels) are linked through the car record's
    // attachedParts owner links.
    std::function<void(WorldNode *)> walkParts = [&](WorldNode *node) {
      if (!node || !node->carInstance)
        return;
      for (const auto &link : node->carInstance->attachedParts) {
        if (!link || !link->owner)
          continue;
        auto *child = resources.world().nodeFor(link->owner);
        if (!child || !child->carInstance || child == node)
          continue;
        drawCarPart(child, "car-part", body.get());
        walkParts(child);
      }
    };
    walkParts(carNode);
    }
    static unsigned lastPartReport = 0, lastSkipReport = 0;
    if (drawnParts != lastPartReport || skippedParts != lastSkipReport) {
      lastPartReport = drawnParts;
      lastSkipReport = skippedParts;
      runlog::event(runlog::EventKind::Note,
                    "world raster car parts: drawn=%u skipped=%u", drawnParts,
                    skippedParts);
    }
  }
  if (!ground.empty()) {
    // Reported on the first frame and then every 5 s (counts move every
    // frame while ground leaves stream in).
    static double lastReport = -1e9;
    const double now = runlog::monotonicSeconds();
    if (now - lastReport >= 5.0) {
      lastReport = now;
      for (const auto &entry : stats) {
        const auto &l = entry.second;
        runlog::event(
            runlog::EventKind::Note,
            "world raster %s: pieces=%zu textured=%zu no_texture_slot=%zu "
            "texture_absent=%zu no_uv=%zu blended=%zu filtered_out=%zu "
            "(ground leaves %zu)",
            entry.first.c_str(), l.pieces, l.textured, l.noTextureSlot,
            l.textureMissing, l.noUv, l.blended, l.skipped, ground.size());
      }
    }
  }
  std::stable_sort(
      pieces.begin(), pieces.end(), [](const Piece &a, const Piece &b) {
        const bool ab = a.draw.material.blend == Raster3DBlend::AlphaBlend,
                   bb = b.draw.material.blend == Raster3DBlend::AlphaBlend;
        return ab != bb ? !ab : ab && a.depth < b.depth;
      });
  std::map<std::string, Raster3DStats> rasterStats;
  {
    // Sky piece: the huge object mesh. Is it drawn, and where do its
    // triangles land?
    static bool skyLogged = false;
    if (!skyLogged) {
      for (const auto &piece : pieces) {
        if (piece.mesh->vertexCount != 376)
          continue;
        skyLogged = true;
        runlog::event(runlog::EventKind::Note,
                      "sky piece: depth=%.1f blend=%d depthWrite=%d "
                      "texture=%d zTest=%d",
                      piece.depth, static_cast<int>(piece.draw.material.blend),
                      int(piece.draw.material.depthWrite),
                      piece.draw.material.texture ? 1 : 0,
                      int(piece.draw.material.depthTest));
        break;
      }
    }
  }
  for (auto &piece : pieces) {
    const auto &m = *piece.mesh;
    auto &d = piece.draw;
    d.positions = m.positions.data();
    d.vertexCount = m.vertexCount;
    d.indices = m.indices.data();
    d.indexCount = m.indices.size();
    d.uvs = m.uv1.size() == m.vertexCount * 2 ? m.uv1.data() : nullptr;
    d.normals =
        m.normals.size() == m.vertexCount * 3 ? m.normals.data() : nullptr;
    d.colors = m.color.size() == m.vertexCount * 4 ? m.color.data() : nullptr;
    const auto before = raster_.stats();
    if (!raster_.draw(d, error))
      note(error);
    const auto &after = raster_.stats();
    if (static_cast<unsigned>(m.vertexCount) == 376u) {
      static bool skyResultLogged = false;
      if (!skyResultLogged) {
        skyResultLogged = true;
        runlog::event(
            runlog::EventKind::Note,
            "sky raster: submitted=%llu rasterized_delta=%llu "
            "outside_delta=%llu clipped_delta=%llu",
            static_cast<unsigned long long>(after.trianglesSubmitted -
                                            before.trianglesSubmitted),
            static_cast<unsigned long long>(after.trianglesRasterized -
                                            before.trianglesRasterized),
            static_cast<unsigned long long>(after.trianglesRejectedOutside -
                                            before.trianglesRejectedOutside),
            static_cast<unsigned long long>(after.trianglesClipped -
                                            before.trianglesClipped));
      }
    }
    auto &r = rasterStats[piece.layer];
    r.trianglesSubmitted +=
        after.trianglesSubmitted - before.trianglesSubmitted;
    r.trianglesRejectedOutside +=
        after.trianglesRejectedOutside - before.trianglesRejectedOutside;
    r.trianglesClipped += after.trianglesClipped - before.trianglesClipped;
    r.trianglesCulled += after.trianglesCulled - before.trianglesCulled;
    r.trianglesDegenerate +=
        after.trianglesDegenerate - before.trianglesDegenerate;
    r.trianglesRasterized +=
        after.trianglesRasterized - before.trianglesRasterized;
    r.pixelsDepthRejected +=
        after.pixelsDepthRejected - before.pixelsDepthRejected;
    r.pixelsAlphaRejected +=
        after.pixelsAlphaRejected - before.pixelsAlphaRejected;
    r.pixelsWritten += after.pixelsWritten - before.pixelsWritten;
  }
  {
    static double lastRasterReport = -1e9;
    const double now = runlog::monotonicSeconds();
    if (!pieces.empty() && now - lastRasterReport >= 0.5) {
      lastRasterReport = now;
      for (const auto &entry : rasterStats) {
        const auto &r = entry.second;
        runlog::event(
            runlog::EventKind::Note,
            "world raster %s triangles: submitted=%llu outside=%llu "
            "clipped=%llu "
            "culled=%llu degenerate=%llu rasterized=%llu | pixels written=%llu "
            "depth_rejected=%llu alpha_rejected=%llu",
            entry.first.c_str(), (unsigned long long)r.trianglesSubmitted,
            (unsigned long long)r.trianglesRejectedOutside,
            (unsigned long long)r.trianglesClipped,
            (unsigned long long)r.trianglesCulled,
            (unsigned long long)r.trianglesDegenerate,
            (unsigned long long)r.trianglesRasterized,
            (unsigned long long)r.pixelsWritten,
            (unsigned long long)r.pixelsDepthRejected,
            (unsigned long long)r.pixelsAlphaRejected);
      }
    }
  }
  if (!pieces.empty() && !raster_.blit(renderer, error))
    note(error);
}
