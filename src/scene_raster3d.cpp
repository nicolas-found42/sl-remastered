#include "scene_raster3d.hpp"
#include "car_tint.hpp"
#include "car_runtime.hpp"
#include "game_spatial_payload.hpp"
#include "ground_runtime.hpp"
#include "render2d.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"
#include "scene_2d.hpp"
#include "scene_render_light.hpp"
#include "skeleton.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>

namespace {
void note(const std::string& s) {
  static std::set<std::string> seen;
  if (seen.insert(s).second)
    runlog::event(runlog::EventKind::Note, "world raster: %s", s.c_str());
}
// Per-layer draw statistics for the run log, and an optional layer filter
// (SLRR_RASTER_LAYERS=ground,objects,car) to see each layer on its own.
struct LayerStats {
  size_t pieces = 0, textured = 0, noTextureSlot = 0, textureMissing = 0,
         noUv = 0, blended = 0, skipped = 0;
  bool operator==(const LayerStats& o) const {
    return pieces == o.pieces && textured == o.textured &&
           noTextureSlot == o.noTextureSlot &&
           textureMissing == o.textureMissing && noUv == o.noUv &&
           blended == o.blended && skipped == o.skipped;
  }
};
bool layerEnabled(const char* layer) {
  static const std::string filter = [] {
    const char* value = std::getenv("SLRR_RASTER_LAYERS");
    return value ? std::string(",") + value + "," : std::string();
  }();
  return filter.empty() ||
         filter.find(std::string(",") + layer + ",") != std::string::npos;
}
} // namespace

bool sceneSkinMesh(const ScxMesh& mesh,
                   const std::vector<std::array<float, 16>>& palette,
                   std::vector<float>& positions, std::vector<float>& normals,
                   std::string& error) {
  positions.clear();
  normals.clear();
  if (mesh.bonePalette.empty() && mesh.boneIndices.empty() && mesh.weights.empty())
    return true;
  if (mesh.bonePalette.empty()) {
    error = "SCX skin missing mesh bone map";
    return false;
  }
  const bool indexed = !mesh.boneIndices.empty();
  const size_t count = mesh.vertexCount;
  const size_t explicitWeights = mesh.weightBytesPerVertex / 4;
  if (mesh.positions.size() != count * 3 ||
      (indexed && mesh.boneIndices.size() != count * 4) ||
      (!indexed && (mesh.bonePalette.size() != 2 || explicitWeights != 1)) ||
      mesh.weightBytesPerVertex % 4 || explicitWeights > 3 ||
      mesh.weights.size() != count * mesh.weightBytesPerVertex ||
      (!mesh.normals.empty() && mesh.normals.size() != count * 3)) {
    error = "invalid SCX skin vertex layout";
    return false;
  }
  // FUN_004ea350 case 6 uploads each group-map entry as three affine
  // rows. Embedded skinmesh1..4 shaders (005f1e58..005f25a8) undo the
  // D3DCOLOR .zyxw swizzle, then use the original byte order as local
  // indices. Their final weight is 1 - dot(explicit weights, ones).
  // FUN_004ed2c0 supplies the unindexed two-matrix fixed-function path.
  for (uint32_t bone : mesh.bonePalette) {
    if (bone >= palette.size()) {
      error = "SCX skin bone map outside skeleton palette";
      return false;
    }
  }
  for (size_t v = 0; v < count; ++v)
    for (size_t j = 0; j <= explicitWeights; ++j)
      if (indexed && mesh.boneIndices[v * 4 + j] >= mesh.bonePalette.size()) {
        error = "SCX skin local index outside mesh bone map";
        return false;
      }
  positions.resize(count * 3);
  if (!mesh.normals.empty())
    normals.resize(count * 3);
  for (size_t v = 0; v < count; ++v) {
    float sum = 0.0f;
    for (size_t j = 0; j <= explicitWeights; ++j) {
      float weight = 1.0f - sum;
      if (j < explicitWeights) {
        const auto* p =
            mesh.weights.data() + v * mesh.weightBytesPerVertex + j * 4;
        const uint32_t bits = uint32_t(p[0]) | uint32_t(p[1]) << 8 |
                              uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        std::memcpy(&weight, &bits, sizeof(weight));
        if (!std::isfinite(weight)) {
          positions.clear();
          normals.clear();
          error = "non-finite SCX skin weight";
          return false;
        }
        sum += weight;
      }
      const size_t localBone = indexed ? mesh.boneIndices[v * 4 + j] : j;
      const auto& m = palette[mesh.bonePalette[localBone]];
      const auto* p = mesh.positions.data() + v * 3;
      for (size_t axis = 0; axis < 3; ++axis) {
        positions[v * 3 + axis] += weight *
                                   (m[axis] * p[0] + m[4 + axis] * p[1] +
                                    m[8 + axis] * p[2] + m[12 + axis] * 10.0f);
        if (!normals.empty()) {
          const auto* n = mesh.normals.data() + v * 3;
          normals[v * 3 + axis] +=
              weight * (m[axis] * n[0] + m[4 + axis] * n[1] +
                        m[8 + axis] * n[2]);
        }
      }
    }
    // skinmesh2..4 normalize the blended normal with dp3/rsq/mul;
    // skinmesh1 preserves the directly transformed normal.
    if (indexed && !normals.empty() && explicitWeights != 0) {
      auto* n = normals.data() + v * 3;
      const float inverseLength = 1.0f / std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      for (size_t axis = 0; axis < 3; ++axis) n[axis] *= inverseLength;
    }
  }
  return true;
}

bool sceneGroundMesh(const GamePolygonGroup& group, ScxMesh& mesh,
                     std::string& error) {
  std::vector<Raster3DGroundVertex> vertices;
  if (!raster3dGroundFvf(group.records.data(), group.records.size(), vertices,
                         error))
    return false;
  ScxMesh result;
  result.vertexCount = static_cast<uint32_t>(vertices.size());
  result.positions.reserve(vertices.size() * 3);
  result.normals.reserve(vertices.size() * 3);
  result.uv1.reserve(vertices.size() * 2);
  for (const auto& v : vertices) {
    result.positions.insert(result.positions.end(), v.position.begin(),
                            v.position.end());
    result.normals.insert(result.normals.end(), v.normal.begin(),
                          v.normal.end());
    result.uv1.insert(result.uv1.end(), v.uv[0].begin(), v.uv[0].end());
  }
  for (const auto& tri : group.triples)
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

bool sceneBackgroundMaterial(uint32_t materialFlags) {
  return (materialFlags & 1u) != 0;
}

Raster3DLight sceneWorldRasterLight(const SceneRenderLight& light) {
  // FUN_004bb7f0 per-vertex light: ambient (+0x30) plus N.L-scaled diffuse
  // (+0x10), with the direction towards the light (D3DLIGHT9 convention).
  Raster3DLight out;
  out.enabled = true;
  out.toLight = {-light.direction[0], -light.direction[1], -light.direction[2]};
  const float ll = std::sqrt(out.toLight[0] * out.toLight[0] +
                             out.toLight[1] * out.toLight[1] +
                             out.toLight[2] * out.toLight[2]);
  if (!(ll > 1e-4f))
    out.toLight = {0.3f, -0.8f, 0.5f};
  out.diffuse = light.at10;
  out.ambient = light.at30;
  return out;
}

const Raster3DTexture* sceneBodyTexture(
    const std::function<const Raster3DTexture*(uint32_t)>& sample,
    uint32_t materialId, uint32_t defaultTextureId, uint32_t currentTextureId) {
  return sample(scenePaintTexture(materialId, defaultTextureId,
                                  currentTextureId));
}

uint32_t scenePaintTexture(uint32_t materialId, uint32_t defaultTextureId,
                           uint32_t currentTextureId) {
  // FUN_00467ff0: the class scripts swap the cfg paint texture on the body
  // material; every other slot keeps its own texture.
  static bool pairLogged = false;
  if (!pairLogged && (defaultTextureId || currentTextureId)) {
    pairLogged = true;
    runlog::event(runlog::EventKind::Note,
                  "world raster paint path: material=0x%x cfg=0x%x current=0x%x",
                  materialId, defaultTextureId, currentTextureId);
  }
  const uint32_t painted =
      bodyPaintTexture(materialId, defaultTextureId, currentTextureId);
  if (painted != materialId) {
    static bool paintLogged = false;
    if (!paintLogged) {
      paintLogged = true;
      runlog::event(runlog::EventKind::Note,
                    "world raster car paint: cfg texture=0x%x replaced by "
                    "0x%x (Part.setTexture, FUN_00467ff0)",
                    materialId, painted);
    }
  }
  return painted;
}

void sceneApplyBackgroundPass(Raster3DDraw& draw) {
  // FUN_004bb050 inverts the view, zeroes the inverse's translation and
  // inverts back: for an affine view that is the view with its translation
  // row cleared.
  draw.view[12] = draw.view[13] = draw.view[14] = 0;
  draw.material.depthTest = false; // D3DRS_ZFUNC = D3DCMP_ALWAYS
  draw.material.depthWrite = false;
  draw.material.fogNear = draw.material.fogFar = 0;
  draw.light.enabled = false; // FUN_004bb5f0(0, 0): D3DRS_LIGHTING off
}

SceneRaster3D::~SceneRaster3D() {
  for (auto& o : objects_)
    setLinkOwner(*o, nullptr);
}
void SceneRaster3D::addObject(LinkOwner& owner) {
  if (std::any_of(objects_.begin(), objects_.end(),
                  [&](const auto& p) { return p->owner == &owner; }))
    return;
  auto link = std::make_unique<OwnerLink>();
  setLinkOwner(*link, &owner);
  objects_.push_back(std::move(link));
}
void SceneRaster3D::remove(LinkOwner& owner) {
  objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                [&](auto& p) {
                                  if (p->owner != &owner && p->owner)
                                    return false;
                                  setLinkOwner(*p, nullptr);
                                  return true;
                                }),
                 objects_.end());
}

void SceneRaster3D::draw(ResourceWorld& resources, Render2D& renderer,
                         const std::vector<ResourceNode*>& ground,
                         const std::array<float, 16>& view,
                         const std::array<float, 16>& projection,
                         const ViewportInstance& viewport,
                         const CameraFog* fog, const Raster3DLight* worldLight) {
  int width = 0, height = 0;
  std::string error;
  if (!renderer.outputSize(width, height, error) || width <= 0 || height <= 0) {
    note(error);
    return;
  }
  if (worldLight && worldLight->enabled) {
    static bool lightLogged = false;
    if (!lightLogged) {
      lightLogged = true;
      runlog::event(
          runlog::EventKind::Note,
          "world raster light: dir=(%.2f,%.2f,%.2f) diffuse=(%.2f,%.2f,%.2f) "
          "ambient=(%.2f,%.2f,%.2f)",
          worldLight->toLight[0], worldLight->toLight[1],
          worldLight->toLight[2], worldLight->diffuse[0],
          worldLight->diffuse[1], worldLight->diffuse[2],
          worldLight->ambient[0], worldLight->ambient[1],
          worldLight->ambient[2]);
    }
  }
  if (fog) {
    static bool fogLogged = false;
    if (!fogLogged) {
      fogLogged = true;
      runlog::event(runlog::EventKind::Note,
                    "world raster fog: near=%.1f far=%.1f color=0x%06x",
                    fog->nearDistance, fog->farDistance,
                    static_cast<uint32_t>(fog->color) & 0xffffffu);
    }
  }
  if ((raster_.width() != static_cast<uint32_t>(width) ||
       raster_.height() != static_cast<uint32_t>(height)) &&
      !raster_.resize(width, height, error)) {
    note(error);
    return;
  }
  raster_.clear(0, 0, 0, 0);
  struct Piece {
    const ScxMesh* mesh;
    Raster3DDraw draw;
    float depth = 0;
    const char* layer = "";
    bool background = false;
    std::vector<float> skinnedPositions, skinnedNormals;
  };
  std::vector<Piece> pieces;
  std::vector<std::shared_ptr<ScxModel>> retainedModels;
  const auto textureById = [&](uint32_t id) -> const Raster3DTexture* {
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
  const auto texture = [&](const SceneMaterialResources& material,
                           size_t index) -> const Raster3DTexture* {
    if (index >= material.textureCount())
      return nullptr;
    return textureById(material.textureId(index));
  };
  std::map<std::string, LayerStats> stats;
  // A part's tint belongs to the body record that names its render resource
  // (FUN_0048c8c0 writes colors[scheme % count] onto the body's render
  // resource, read back as instance +0xcc by FUN_0048aa70).
  const auto bodyTintFor = [&](WorldNode* carNode, WorldNode* partNode,
                               const CarRuntimeInstance& inst)
      -> std::optional<uint32_t> {
    const uint32_t instanceColor = [&] {
      const auto renderNode =
          partNode ? resources.resolve(partNode) : ResolvedNode{};
      const auto renderInstance =
          renderNode ? resources.sceneRenderInstance(renderNode) : nullptr;
      return renderInstance ? renderInstance->instanceColor() : 0xffffffffu;
    }();
    const auto tint = resolveBodyTint(
        carNode && carNode->carInstance ? carNode->carInstance->bodyTints
                                        : std::map<uint32_t, uint32_t>{},
        inst.renderType, instanceColor);
    if (tint) {
      static bool tintLogged = false;
      if (!tintLogged) {
        tintLogged = true;
        runlog::event(runlog::EventKind::Note,
                      "world raster car tint: renderType=0x%x +0xcc=0x%08x",
                      inst.renderType, *tint);
      }
    }
    return tint;
  };
  const auto append = [&](const ScxMesh& mesh, const ScxMaterial& material,
                          const SceneMaterialResources& resourcesMaterial,
                          const std::array<float, 16>& model, bool isGround,
                          const char* layer,
                          std::optional<uint32_t> tint = std::nullopt,
                          uint32_t defaultTextureId = 0,
                          uint32_t currentTextureId = 0,
                          const SkeletonPose* pose = nullptr) {
    auto& layerStats = stats[layer];
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
    // An index beyond the bound material's textures leaves the piece
    // untextured, as the texture() lookup does.
    if (mapped.baseTextureIndex && mesh.uv1.size() == mesh.vertexCount * 2 &&
        *mapped.baseTextureIndex < resourcesMaterial.textureCount()) {
      // FUN_00467ff0 replaces only the cfg paint texture (Part.setTexture);
      // glass/trim materials keep their own texture.
      const auto materialId =
          resourcesMaterial.textureId(*mapped.baseTextureIndex);
      mapped.state.texture =
          sceneBodyTexture(textureById, materialId, defaultTextureId,
                           currentTextureId);
    }
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
    piece.background =
        !isGround && sceneBackgroundMaterial(resourcesMaterial.flags());
    piece.layer = piece.background ? "background" : layer;
    piece.mesh = &mesh;
    if (pose && (!mesh.bonePalette.empty() || !mesh.boneIndices.empty()) &&
        !sceneSkinMesh(mesh, pose->palette(), piece.skinnedPositions,
                       piece.skinnedNormals, error)) {
      note(error);
      return;
    }
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
    // Stock lights world pieces per vertex (FUN_004bb7f0 writes ambient +
    // N.L-scaled diffuse into the vertex colours; D3DRS_LIGHTING stays on for
    // the draw). Background pieces stay unlit (FUN_004bb5f0(0,0)).
    if (worldLight && worldLight->enabled && !mesh.normals.empty() &&
        !piece.background) {
      piece.draw.light = *worldLight;
      piece.draw.light.enabled = true;
    }
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
    if (piece.background)
      sceneApplyBackgroundPass(piece.draw);
    pieces.push_back(std::move(piece));
  };
  for (auto* leaf : ground) {
    if (!leaf || !leaf->spatialPayload || !leaf->spatialPayload->polygons)
      continue;
    const auto sector = resources.resolve(leaf->sectorLink.owner);
    if (!sector.packNode || !sector.packNode->groundFactory ||
        !sector.packNode->groundFactory->preparation)
      continue;
    const auto& g = *sector.packNode->groundFactory->preparation;
    if (!g.pendingMesh || !g.pendingMaterial || !g.pendingRender)
      continue;
    g.pendingRender->processRootBoneUpdate();
    g.pendingRender->refreshSingleBoneDrawMatrix();
    const auto& groups = leaf->spatialPayload->polygons->groups;
    auto& cached = groundMeshes_[leaf->value];
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
                                [](const auto& p) { return !p->owner; }),
                 objects_.end());
  for (const auto& owner : objects_) {
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
    // A driver instance attached to the car root follows the car's physics
    // pose first, then solves its persistent IK targets before skinning.
    if (instance->hasPhysicsBoneAttachment())
      instance->refreshPhysicsBone();
    if (!sceneEnsureSkeleton(resources, *instance, error)) {
      note(error);
      continue;
    }
    if (instance->skeletonPose())
      instance->skeletonPose()->solve(8);
    instance->processRootBoneUpdate();
    instance->refreshSingleBoneDrawMatrix();
    for (const auto& mesh : model->meshes) {
      if (!mesh.materialIndex || *mesh.materialIndex >= model->materials.size())
        continue;
      append(mesh, model->materials[*mesh.materialIndex], *material,
             instance->drawMatrix(), false, "objects", std::nullopt, 0, 0,
             instance->skeletonPose().get());
    }
  }
  // Draw every car: the chassis at car_root plus every installed part
  // (wheels included) at its own physics body pose, like stock render
  // instances per part. The player car, the dealer showroom cars and garage
  // cars are all car nodes; none is special-cased.
  if (!ground.empty()) {
    unsigned drawnParts = 0, skippedParts = 0;
    for (const uint32_t carId : resources.world().carInstanceIds()) {
      auto* carNode = resources.world().findInstance(carId);
      auto* car = carNode ? carNode->carInstance.get() : nullptr;
      auto* partNode = car && car->chassisObject
                           ? resources.world().findByReceiver(car->chassisObject)
                           : carNode;
      auto* bodyNode =
          car ? resources.world().nodeFor(car->physicsRoot.owner) : nullptr;
      auto body =
          bodyNode ? resources.world().dynamicBody(*bodyNode).lock() : nullptr;

      std::function<void(WorldNode*, const char*, physics::Body*)>
          drawCarPart = [&](WorldNode* node, const char* label,
                            physics::Body* fallbackBody) {
            if (!node || !node->carInstance) {
              ++skippedParts;
              return;
            }
            auto& inst = *node->carInstance;
            physics::Body* partBody = nullptr;
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
              auto loaded = sceneLoadMaterial(resources, inst.renderType);
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
                static int carInfoLogged = 0;
                if (carInfoLogged < 12) {
                  ++carInfoLogged;
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
                std::vector<LinkOwner*> links;
                if (texNode.status == LookupStatus::Found && texNode.node.owner)
                  links.push_back(texNode.node.owner);
                SceneMaterialResources material(nullptr, links);
                SceneRenderInstance transform(nullptr);
                const auto& state = partBody->state[partBody->current];
                transform.setBoneTransform(state.pos, state.rot);
                transform.processRootBoneUpdate();
                transform.refreshSingleBoneDrawMatrix();
                // Skeletal part meshes (e.g. the driver) skin against the
                // pose loaded from the part material's bind data; a failed
                // ensure keeps the rigid draw instead of skipping the part.
                const SkeletonPose* partPose = nullptr;
                for (const auto& mesh : model->meshes)
                  if (mesh.materialIndex &&
                      *mesh.materialIndex < model->materials.size() &&
                      (!mesh.bonePalette.empty() || !mesh.boneIndices.empty()) && !partPose) {
                    if (sceneEnsureSkeleton(resources, transform, error))
                      partPose = transform.skeletonPose().get();
                    else
                      note("car part skin: " + error);
                  }
                for (const auto& mesh : model->meshes)
                  if (mesh.materialIndex &&
                      *mesh.materialIndex < model->materials.size())
                    append(mesh, model->materials[*mesh.materialIndex],
                           partMaterial ? *partMaterial : material,
                           transform.drawMatrix(), false, label,
                           bodyTintFor(carNode, node, inst),
                           inst.defaultTextureId, inst.textureId, partPose);
                ++drawnParts;
              }
            }
          };
      // The chassis node draws at the car root body (the compound root lives
      // on the car record, not on the chassis part); the chassis receiver may
      // resolve to a node without its own car instance, in which case the car
      // node's instance carries the mesh.
      // FUN_00475ec0/FUN_0048c8c0 work on the car record itself: the chassis
      // body draws its own mesh from the car instance, whose render record the
      // class scripts paint (Part.setTexture lands on this instance).
      drawCarPart(carNode, "car",
                  body.get());
      // Installed parts (wheels, panels) are linked through the car record's
      // attachedParts owner links.
      std::function<void(WorldNode*)> walkParts = [&](WorldNode* node) {
        if (!node || !node->carInstance)
          return;
        for (const auto& link : node->carInstance->attachedParts) {
          if (!link || !link->owner)
            continue;
          auto* child = resources.world().nodeFor(link->owner);
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
      for (const auto& entry : stats) {
        const auto& l = entry.second;
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
  // FUN_0050eba0 draws the background list (+0x548) first, in queue order.
  std::stable_sort(
      pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) {
        if (a.background != b.background)
          return a.background;
        if (a.background)
          return false;
        const bool ab = a.draw.material.blend == Raster3DBlend::AlphaBlend,
                   bb = b.draw.material.blend == Raster3DBlend::AlphaBlend;
        return ab != bb ? !ab : ab && a.depth < b.depth;
      });
  std::map<std::string, Raster3DStats> rasterStats;
  for (auto& piece : pieces) {
    const auto& m = *piece.mesh;
    auto& d = piece.draw;
    d.positions = piece.skinnedPositions.empty()
                      ? m.positions.data()
                      : piece.skinnedPositions.data();
    d.vertexCount = m.vertexCount;
    d.indices = m.indices.data();
    d.indexCount = m.indices.size();
    d.uvs = m.uv1.size() == m.vertexCount * 2 ? m.uv1.data() : nullptr;
    d.normals = !piece.skinnedNormals.empty()
                    ? piece.skinnedNormals.data()
                    : (m.normals.size() == m.vertexCount * 3 ? m.normals.data()
                                                             : nullptr);
    d.colors = m.color.size() == m.vertexCount * 4 ? m.color.data() : nullptr;
    const auto before = raster_.stats();
    if (!raster_.draw(d, error))
      note(error);
    const auto& after = raster_.stats();
    auto& r = rasterStats[piece.layer];
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
      for (const auto& entry : rasterStats) {
        const auto& r = entry.second;
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
