#include "scene_2d.hpp"
#include "font.hpp"
#include "game_spatial_payload.hpp"
#include "ground_runtime.hpp"
#include "natives/resource_ref.hpp"
#include "raster3d.hpp"
#include "render2d.hpp"
#include "run_log.hpp"
#include "scx.hpp"
#include "scene_render_light.hpp"

#include "resource_id.hpp"
#include "resource_material.hpp"
#include "resource_texture.hpp"
#include "resource_world.hpp"
#include "world_model.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

SceneRenderKind sceneRenderKindFromFlags(uint32_t flags) {
  if (flags & 0x4)
    return SceneRenderKind::Kind3;
  if (flags & 0x100)
    return SceneRenderKind::Kind4;
  if (flags & 0x2000)
    return SceneRenderKind::Kind5;
  if (flags & 0x100000)
    return SceneRenderKind::Kind6;
  if (flags & 0x800)
    return SceneRenderKind::Kind0;
  return SceneRenderKind::Kind2;
}

ScenePassSelection scenePasses(uint32_t entryFlags, bool specialView,
                               bool resourceFlag8, bool hasCustomCollection) {
  ScenePassSelection result;
  if (specialView) {
    result.passes[result.count++] = ScenePass::Pass548;
    return result;
  }
  switch (entryFlags & 3) {
  case 0:
    result.passes[result.count++] = ScenePass::Pass41c;
    break;
  case 1:
    result.passes[result.count++] = ScenePass::Pass4cc;
    break;
  case 2:
    result.passes[result.count++] = ScenePass::Pass448;
    result.passes[result.count++] = ScenePass::Pass474;
    break;
  case 3:
    result.passes[result.count++] = ScenePass::Pass50c;
    break;
  }
  result.customCollection = resourceFlag8 && hasCustomCollection;
  return result;
}

static uint32_t sceneTint(uint32_t color, uint32_t tint) {
  if (tint == 0xffffffffu)
    return color;
  uint32_t result = 0;
  for (unsigned shift = 0; shift < 32; shift += 8)
    result |= (((color >> shift & 255u) * (tint >> shift & 255u) + 127u) / 255u) << shift;
  return result;
}

uint32_t sceneType0EntryFlags(uint32_t functionWord) {
  return ((functionWord & 0x40000000u) ? 1u : 0u) |
         ((functionWord & 0x80000000u) ? 2u : 0u);
}

std::optional<uint32_t> sceneScyMaterialType(const ScxMaterial &material) {
  if (!material.scyV3)
    return std::nullopt;
  const auto &legacy = *material.scyV3;
  if (!std::isfinite(legacy.specularStrength) ||
      !std::isfinite(legacy.shininess) ||
      !std::all_of(legacy.specular.begin(), legacy.specular.end(),
                   [](float value) { return std::isfinite(value); }))
    return std::nullopt;
  bool specular = legacy.shininess > 8.0f && legacy.textureIndices[2] == -1 &&
                  std::any_of(legacy.specular.begin(), legacy.specular.end(),
                              [&](float value) {
                                return value * legacy.specularStrength > 0.1f;
                              });
  const bool textured =
      legacy.textureIndices[0] != -1 || legacy.textureIndices[4] != -1;
  const bool sphere = legacy.textureIndices[3] != -1;
  if (!textured)
    return 0x30u + (sphere ? 2u : 0u) + (specular ? 1u : 0u);
  uint32_t flags = material.flags;
  if (legacy.textureIndices[4] == -1)
    flags &= ~0x1040u;
  uint32_t type = (sphere ? 8u : 0u) + (specular ? 4u : 0u);
  if (flags & 0x40u)
    ++type;
  else if (flags & 0x1000u)
    type += 2;
  else if ((flags & 0x100u) && !(flags & 1u))
    type += 3;
  return type;
}

std::optional<SceneScyBlend> sceneScyBlendState(const ScxMaterial &material) {
  if (!material.scyV3)
    return std::nullopt;
  const float alpha = material.diffuse[3];
  if (!std::isfinite(alpha))
    return std::nullopt;
  // 004be950: `(flags & 1) || diffuse.a < 0.99f` selects the transparent
  // branch; only then does flag 0x800 choose the alpha test over blending.
  // Flag 0x1000 selects material type 10/2 (VCA) and is unrelated.
  constexpr float kOpaqueAlpha = 0.99f; // DAT_005f0d28
  if ((material.flags & 1u) || alpha < kOpaqueAlpha) {
    if (material.flags & 0x800u)
      return SceneScyBlend::AlphaTest;
    return SceneScyBlend::AlphaBlend;
  }
  return SceneScyBlend::Opaque;
}

uint32_t sceneSphereRelation(const std::array<float, 3> &boundCenter,
                             float boundRadius,
                             const std::array<float, 3> &center, float radius) {
  if (!std::isfinite(boundRadius) || !std::isfinite(radius) ||
      boundRadius < 0 || radius < 0 ||
      !std::all_of(
          boundCenter.begin(), boundCenter.end(),
          [](float x) { return std::isfinite(x); }) ||
      !std::all_of(
          center.begin(), center.end(),
          [](float x) { return std::isfinite(x); }))
    return 0;
  const double x = static_cast<double>(boundCenter[0]) - center[0];
  const double y = static_cast<double>(boundCenter[1]) - center[1];
  const double z = static_cast<double>(boundCenter[2]) - center[2];
  const double distanceSquared = x * x + y * y + z * z;
  const double sum = static_cast<double>(boundRadius) + radius;
  const double difference = static_cast<double>(boundRadius) - radius;
  if (sum * sum < distanceSquared)
    return 1;
  if (difference * difference <= distanceSquared)
    return 2;
  if (boundRadius < radius)
    return 4;
  return 8;
}

uint32_t sceneBoxRelation(const ResourceBounds &bound,
                          const std::array<float, 3> &center,
                          const std::array<float, 3> &extents) {
  if (bound.mode > 1 ||
      !std::all_of(bound.values.begin(), bound.values.end(),
                   [](float x) { return std::isfinite(x); }) ||
      !std::all_of(center.begin(), center.end(),
                   [](float x) { return std::isfinite(x); }) ||
      !std::all_of(extents.begin(), extents.end(),
                   [](float x) { return std::isfinite(x) && x >= 0; }))
    return 0;
  std::array<double, 3> distance{}, outer{};
  for (size_t i = 0; i < 3; ++i) {
    distance[i] = std::abs(static_cast<double>(center[i]) - bound.values[i]);
    outer[i] = bound.values[3 + (bound.mode == 0 ? 0 : i)];
    if (outer[i] < 0)
      return 0;
    if (outer[i] + extents[i] < distance[i])
      return 1;
  }
  bool insideBox = true;
  for (size_t i = 0; i < 3; ++i)
    insideBox &= distance[i] < extents[i] - outer[i];
  if (insideBox)
    return 4;
  if (bound.mode == 0) {
    double squared = 0;
    for (size_t i = 0; i < 3; ++i)
      squared += static_cast<double>(extents[i]) * extents[i] +
                 distance[i] * distance[i];
    if (squared < outer[0] * outer[0])
      return 8;
  } else {
    bool containsBox = true;
    for (size_t i = 0; i < 3; ++i)
      containsBox &= distance[i] < outer[i] - extents[i];
    if (containsBox)
      return 8;
  }
  return 2;
}

bool sceneCameraBound(float farMetric, const std::array<float, 16> &cameraWorld,
                      ResourceBounds &out) {
  if (!std::isfinite(farMetric) || farMetric < 0 ||
      !std::all_of(
          cameraWorld.begin(), cameraWorld.end(),
          [](float x) { return std::isfinite(x); }))
    return false;
  ResourceBounds next;
  next.mode = 0;
  for (size_t i = 0; i < 3; ++i) {
    next.values[i] =
        cameraWorld[12 + i] * 0.1f + cameraWorld[8 + i] * (farMetric * -0.05f);
    next.values[3 + i] = farMetric * 0.06f;
    if (!std::isfinite(next.values[i]) || !std::isfinite(next.values[3 + i]))
      return false;
  }
  out = next;
  return true;
}

uint32_t sceneBoundPlaneRelation(const ResourceBounds &bound,
                                 const std::array<float, 4> &plane) {
  if (bound.mode > 1 ||
      !std::all_of(bound.values.begin(), bound.values.end(),
                   [](float x) { return std::isfinite(x); }) ||
      !std::all_of(plane.begin(), plane.end(),
                   [](float x) { return std::isfinite(x); }))
    return 0;
  for (size_t i = 3; i < 6; ++i)
    if (bound.values[i] < 0)
      return 0;
  const double distance = static_cast<double>(bound.values[0]) * plane[0] +
                          static_cast<double>(bound.values[1]) * plane[1] +
                          static_cast<double>(bound.values[2]) * plane[2] -
                          plane[3];
  if (bound.mode == 0) {
    if (bound.values[3] < distance)
      return 1;
    if (distance < -bound.values[3])
      return 4;
    return 2;
  }
  double radiusSquared = 0;
  for (size_t i = 3; i < 6; ++i)
    radiusSquared += static_cast<double>(bound.values[i]) * bound.values[i];
  if (distance > 0 && radiusSquared < distance * distance)
    return 1;
  if (distance < 0 && distance * distance < -radiusSquared)
    return 4;
  double nearSide = -plane[3], farSide = -plane[3];
  for (size_t i = 0; i < 3; ++i) {
    const double extent =
        plane[i] <= 0 ? -bound.values[i + 3] : bound.values[i + 3];
    nearSide += (bound.values[i] - extent) * plane[i];
    farSide += (bound.values[i] + extent) * plane[i];
  }
  if (nearSide > 0)
    return 1;
  if (farSide < 0)
    return 4;
  return 2;
}

bool sceneUpdateType3Visibility(SceneVisibilityFields &fields, uint32_t frame,
                                float input, float bias, float gain) {
  if (!std::isfinite(input) || !std::isfinite(bias) || !std::isfinite(gain))
    return false;
  const double combined = static_cast<double>(input) * gain + bias;
  const float value = static_cast<float>(std::clamp(combined, 0.0, 1.0));
  if ((frame <= fields.stamp78 &&
       (!fields.field6c || !std::isfinite(*fields.field6c))) ||
      (frame <= fields.stamp7c &&
       (!fields.field70 || !std::isfinite(*fields.field70))))
    return false;
  if (frame > fields.stamp78 || value > *fields.field6c)
    fields.field6c = value;
  fields.stamp78 = frame;
  if (frame > fields.stamp7c || value > *fields.field70)
    fields.field70 = value;
  return true;
}

std::optional<float> sceneVisibilityInput(
    const ResourceBounds &bounds, const std::array<float, 3> &cameraPosition,
    float cameraFactor, float lodBias, float lodAmplitude, float worldScale) {
  if (!std::isfinite(lodBias))
    return std::nullopt;
  if (bounds.mode == 2) {
    const float value = 1.0f + lodBias;
    return std::isfinite(value) ? std::optional<float>(value) : std::nullopt;
  }
  if (bounds.mode > 1 || !std::isfinite(cameraFactor) ||
      !std::isfinite(lodAmplitude) || !std::isfinite(worldScale) ||
      !std::all_of(bounds.values.begin(), bounds.values.end(),
                   [](float x) { return std::isfinite(x); }) ||
      !std::all_of(cameraPosition.begin(), cameraPosition.end(),
                   [](float x) { return std::isfinite(x); }))
    return std::nullopt;
  const double dx = static_cast<double>(cameraPosition[0]) - bounds.values[0];
  const double dy = static_cast<double>(cameraPosition[1]) - bounds.values[1];
  const double dz = static_cast<double>(cameraPosition[2]) - bounds.values[2];
  const double distance = static_cast<double>(worldScale) * worldScale *
                          (dx * dx + dy * dy + dz * dz);
  double radius = bounds.values[3];
  if (bounds.mode == 1)
    radius =
        std::sqrt(radius * radius +
                  static_cast<double>(bounds.values[4]) * bounds.values[4] +
                  static_cast<double>(bounds.values[5]) * bounds.values[5]);
  double amount = 1.0;
  if (distance >= 0x1.a36e2ep-14f) {
    const double size = radius * cameraFactor * lodAmplitude;
    amount = std::min(1.0, std::sqrt((size * size) / distance));
  }
  const float value = static_cast<float>(amount + lodBias);
  return std::isfinite(value) ? std::optional<float>(value) : std::nullopt;
}

bool sceneUpdateType3NodeVisibility(WorldNode &node, uint32_t frame,
                                    float input) {
  if (node.type != 3)
    return false;
  SceneVisibilityFields fields{node.field6c, node.field70, node.stamp78,
                               node.stamp7c};
  if (!sceneUpdateType3Visibility(fields, frame, input, node.visibilityBias,
                                  node.visibilityGain))
    return false;
  node.field6c = fields.field6c;
  node.field70 = fields.field70;
  node.stamp78 = fields.stamp78;
  return true;
}

std::optional<float>
sceneTransparentDepth(const std::array<float, 16> &objectMatrix,
                      const std::array<float, 16> &cameraWorld) {
  if (!std::all_of(objectMatrix.begin(), objectMatrix.end(),
                   [](float x) { return std::isfinite(x); }) ||
      !std::all_of(cameraWorld.begin(), cameraWorld.end(),
                   [](float x) { return std::isfinite(x); }))
    return std::nullopt;
  const float x = objectMatrix[12] - cameraWorld[12] * 0.1f;
  const float y = objectMatrix[13] - cameraWorld[13] * 0.1f;
  const float z = objectMatrix[14] - cameraWorld[14] * 0.1f;
  const float depth =
      x * cameraWorld[8] + y * cameraWorld[9] + z * cameraWorld[10];
  return std::isfinite(depth) ? std::optional<float>(depth) : std::nullopt;
}

SceneVisibleQueue::~SceneVisibleQueue() {
  for (auto &entry : entries_)
    setLinkOwner(entry->instance, nullptr);
}

bool SceneVisibleQueue::push(LinkOwner &instance, float score) {
  if (!std::isfinite(score))
    return false;
  auto entry = std::make_unique<Entry>();
  entry->score = score;
  setLinkOwner(entry->instance, &instance);
  entries_.push_back(std::move(entry));
  size_t i = entries_.size() - 1;
  while (i && entries_[(i - 1) / 2]->score > entries_[i]->score) {
    std::swap(entries_[(i - 1) / 2], entries_[i]);
    i = (i - 1) / 2;
  }
  return true;
}

void SceneVisibleQueue::detach(LinkOwner &instance) {
  for (auto &entry : entries_)
    if (entry->instance.owner == &instance)
      setLinkOwner(entry->instance, nullptr);
}

void SceneVisibleQueue::drain(
    uint32_t budget, bool unlimited,
    const std::function<uint32_t(LinkOwner &)> &create) {
  uint32_t used = 0;
  while (!entries_.empty() && (used <= budget || unlimited)) {
    auto entry = std::move(entries_.front());
    if (entries_.size() == 1)
      entries_.pop_back();
    else {
      entries_.front() = std::move(entries_.back());
      entries_.pop_back();
      size_t i = 0;
      while (i * 2 + 1 < entries_.size()) {
        size_t child = i * 2 + 1;
        if (child + 1 < entries_.size() &&
            entries_[child + 1]->score < entries_[child]->score)
          ++child;
        if (entries_[i]->score <= entries_[child]->score)
          break;
        std::swap(entries_[i], entries_[child]);
        i = child;
      }
    }
    if (entry->instance.owner)
      used += create(*entry->instance.owner);
    setLinkOwner(entry->instance, nullptr);
  }
  for (auto &entry : entries_)
    setLinkOwner(entry->instance, nullptr);
  entries_.clear();
}

SceneCandidateResult sceneQueueType3(SceneVisibleQueue &queue,
                                     LinkOwner &instance, uint32_t boundMode,
                                     float field6c, float field70,
                                     float threshold, float &scoreState,
                                     uint32_t &recordFlags, bool fullyInside) {
  if (boundMode > 2 || !std::isfinite(field6c) || !std::isfinite(field70) ||
      !std::isfinite(threshold) || !std::isfinite(scoreState))
    return SceneCandidateResult::Unsupported;
  // 00511180 calls 004369b0 for box bounds, then discards the result.
  // Its heap key is still 1-field70, exactly as for sphere bounds.
  const float score = boundMode == 2 ? scoreState : 1.0f - field70;
  if (!std::isfinite(score))
    return SceneCandidateResult::Unsupported;
  if (boundMode != 2 && !fullyInside)
    scoreState = score;
  if (field6c < threshold)
    return SceneCandidateResult::BelowThreshold;
  if (recordFlags & 0xb0000000u)
    return SceneCandidateResult::AlreadyPresent;
  if (!queue.push(instance, score))
    return SceneCandidateResult::Unsupported;
  recordFlags = (recordFlags & 0xfffffffcu) | 4;
  return SceneCandidateResult::Queued;
}

SceneCameraCandidates::~SceneCameraCandidates() {
  for (auto &record : records_)
    setLinkOwner(record->owner, nullptr);
}

SceneCandidateResult SceneCameraCandidates::submit(WorldNode &node,
                                                   float threshold,
                                                   float &scoreState,
                                                   bool fullyInside) {
  if (node.type != 3 || !node.field6c || !node.field70)
    return SceneCandidateResult::Unsupported;
  records_.erase(std::remove_if(records_.begin(), records_.end(),
                                [](const auto &record) {
                                  return record->owner.owner == nullptr;
                                }),
                 records_.end());
  auto found =
      std::find_if(records_.begin(), records_.end(), [&](const auto &record) {
        return record->owner.owner == &node;
      });
  if (found != records_.end())
    return sceneQueueType3(queue_, node, node.bounds.mode, *node.field6c,
                           *node.field70, threshold, scoreState,
                           (*found)->flags, fullyInside);
  uint32_t flags = 0;
  const auto result =
      sceneQueueType3(queue_, node, node.bounds.mode, *node.field6c,
                      *node.field70, threshold, scoreState, flags, fullyInside);
  if (result == SceneCandidateResult::Queued) {
    auto record = std::make_unique<Record>();
    setLinkOwner(record->owner, &node);
    record->flags = flags;
    records_.push_back(std::move(record));
  }
  return result;
}

SceneCandidateResult
SceneCameraCandidates::submitGround(ResourceWorld &resources,
                                    ResourceNode &node, float threshold,
                                    float &scoreState, bool fullyInside) {
  if (resources.resolve(&node).packNode != &node || node.type != 1 ||
      !node.loaded() || !node.spatialPayload ||
      !node.spatialPayload->polygons || !node.polygonCount ||
      *node.polygonCount == 0 ||
      *node.polygonCount != node.spatialPayload->polygons->groups.size() ||
      !node.bounds || !node.field6c || !node.field70 ||
      !std::isfinite(*node.field6c) || !std::isfinite(*node.field70) ||
      !std::isfinite(threshold) || !std::isfinite(scoreState) ||
      node.bounds->mode > 2)
    return SceneCandidateResult::Unsupported;
  const auto sector = resources.resolve(node.sectorLink.owner);
  if (!sector.packNode || sector.type != 1 || !sector.packNode->loaded() ||
      !sector.packNode->groundFactory ||
      sector.packNode->groundFactory->city.owner != sector.packNode ||
      !sector.packNode->groundFactory->preparation)
    return SceneCandidateResult::Unsupported;
  const auto &ground = *sector.packNode->groundFactory->preparation;
  const auto render = resources.resolve(ground.renderResource.owner);
  if (!render.worldNode || render.type != 3 || !ground.pendingRender ||
      !ground.pendingMaterial || !ground.pendingMesh)
    return SceneCandidateResult::Unsupported;
  for (const auto &group : node.spatialPayload->polygons->groups) {
    if (group.kind >= ground.pendingMesh->materials.size())
      return SceneCandidateResult::Unsupported;
    for (const auto &triangle : group.triples)
      for (uint32_t index : triangle)
        if (index >= group.records.size())
          return SceneCandidateResult::Unsupported;
  }
  const float score =
      node.bounds->mode == 2 ? scoreState : 1.0f - *node.field70;
  if (!std::isfinite(score))
    return SceneCandidateResult::Unsupported;
  if (node.bounds->mode != 2 && !fullyInside)
    scoreState = score;
  if (*node.field6c < threshold)
    return SceneCandidateResult::BelowThreshold;
  records_.erase(std::remove_if(records_.begin(), records_.end(),
                                [](const auto &record) {
                                  return record->owner.owner == nullptr;
                                }),
                 records_.end());
  auto found =
      std::find_if(records_.begin(), records_.end(), [&](const auto &record) {
        return record->owner.owner == &node;
      });
  if (found != records_.end() && ((*found)->flags & 0x40000000u))
    return SceneCandidateResult::AlreadyPresent;
  if (!groundQueue_.push(node, score))
    return SceneCandidateResult::Unsupported;
  if (found == records_.end()) {
    auto record = std::make_unique<Record>();
    setLinkOwner(record->owner, &node);
    record->flags = 4;
    records_.push_back(std::move(record));
  } else {
    (*found)->flags = ((*found)->flags & 0xfffffffcu) | 4;
  }
  return SceneCandidateResult::Queued;
}

void SceneCameraCandidates::drain(
    uint32_t budget, bool unlimited,
    const std::function<uint32_t(LinkOwner &)> &create) {
  queue_.drain(budget, unlimited, create);
  groundQueue_.drain(budget, unlimited, create);
}

void SceneCameraCandidates::discardPending() {
  groundQueue_.drain(0, true, [](LinkOwner &) { return 0; });
  queue_.drain(0, true, [](LinkOwner &) { return 0; });
}

bool SceneCameraCandidates::markMaterialCreated(LinkOwner &node) {
  const auto found =
      std::find_if(records_.begin(), records_.end(), [&](const auto &record) {
        return record->owner.owner == &node;
      });
  if (found == records_.end())
    return false;
  (*found)->flags = ((*found)->flags & 0x0fffffffu) | 0x10000000u;
  return true;
}

bool SceneCameraCandidates::markLightCreated(LinkOwner &node) {
  const auto found =
      std::find_if(records_.begin(), records_.end(), [&](const auto &record) {
        return record->owner.owner == &node;
      });
  if (found == records_.end())
    return false;
  (*found)->flags = ((*found)->flags & 0x0fffffffu) | 0x20000000u;
  return true;
}

bool SceneCameraCandidates::markEmptyCreated(LinkOwner &node) {
  const auto found =
      std::find_if(records_.begin(), records_.end(), [&](const auto &record) {
        return record->owner.owner == &node;
      });
  if (found == records_.end())
    return false;
  (*found)->flags = ((*found)->flags & 0x0fffffffu) | 0x80000000u;
  return true;
}

bool SceneCameraCandidates::hasRecord(const LinkOwner &node) const {
  return std::any_of(records_.begin(), records_.end(), [&](const auto &record) {
    return record->owner.owner == &node;
  });
}

bool SceneCameraCandidates::remove(LinkOwner &node) {
  queue_.detach(node);
  groundQueue_.detach(node);
  const auto found =
      std::find_if(records_.begin(), records_.end(), [&](const auto &record) {
        return record->owner.owner == &node;
      });
  if (found == records_.end())
    return false;
  setLinkOwner((*found)->owner, nullptr);
  records_.erase(found);
  return true;
}

bool sceneRefreshTexture(ResourceWorld &world,
                         const SceneMaterialResources &material,
                         const std::filesystem::path &root,
                         ResourceTexturePixels &pixels, std::string &error,
                         bool requireCached) {
  pixels = {};
  if (!material.textureCount()) {
    error = "material has no texture";
    return false;
  }
  const uint32_t id = material.textureId(0);
  const auto node = world.lookup(id, 7);
  if (node.status != LookupStatus::Found || !node.node.packNode) {
    error = "linked texture unavailable";
    return false;
  }
  if (node.node.packNode->texture) {
    pixels = node.node.packNode->texture->pixels;
    error.clear();
    return true;
  }
  if (requireCached) {
    error = "material texture is not cached: " + std::to_string(id);
    return false;
  }
  PackResource resource;
  if (!world.packs().lookup(id, resource, error))
    return false;
  if (!resource.packBytes) {
    error = "texture pack unavailable";
    return false;
  }
  return resourceTextureLoad(*resource.packBytes,
                             static_cast<uint16_t>(id & 0xffff), root, pixels,
                             error);
}

bool sceneProjectMesh(const ScxMesh &mesh, const float projection[8],
                      std::vector<RenderVertex> &vertices, std::string &error) {
  vertices.clear();
  if (!projection ||
      mesh.positions.size() != static_cast<size_t>(mesh.vertexCount) * 3 ||
      mesh.uv1.size() != static_cast<size_t>(mesh.vertexCount) * 2 ||
      !mesh.vertexCount) {
    error = "unsupported mesh geometry";
    return false;
  }
  for (size_t i = 0; i < 8; ++i)
    if (!std::isfinite(projection[i])) {
      error = "invalid projection";
      return false;
    }
  std::vector<RenderVertex> projected;
  projected.reserve(mesh.vertexCount);
  for (size_t i = 0; i < mesh.vertexCount; ++i) {
    const float x = mesh.positions[i * 3], y = mesh.positions[i * 3 + 1],
                z = mesh.positions[i * 3 + 2];
    const float u = mesh.uv1[i * 2], v = mesh.uv1[i * 2 + 1];
    const float px = projection[0] * x + projection[1] * y + projection[2] * z +
                     projection[3];
    const float py = projection[4] * x + projection[5] * y + projection[6] * z +
                     projection[7];
    if (!std::isfinite(z) || !std::isfinite(px) || !std::isfinite(py) ||
        !std::isfinite(u) || !std::isfinite(v)) {
      error = "unsupported mesh vertex";
      return false;
    }
    projected.push_back({px, py, u, v});
  }
  vertices = std::move(projected);
  error.clear();
  return true;
}

bool sceneProjectTransformedMesh(const ScxMesh &mesh,
                                 const std::array<float, 16> &world,
                                 const float projection[8],
                                 std::vector<RenderVertex> &vertices,
                                 std::string &error) {
  vertices.clear();
  if (!projection ||
      !std::all_of(world.begin(), world.end(),
                   [](float value) { return std::isfinite(value); }) ||
      world[3] != 0 || world[7] != 0 || world[11] != 0 || world[15] != 1) {
    error = "unsupported world transform";
    return false;
  }
  float combined[8];
  for (size_t row = 0; row < 2; ++row) {
    const float *p = projection + row * 4;
    for (size_t col = 0; col < 3; ++col)
      combined[row * 4 + col] = p[0] * world[col * 4] +
                                p[1] * world[col * 4 + 1] +
                                p[2] * world[col * 4 + 2];
    combined[row * 4 + 3] =
        p[0] * world[12] + p[1] * world[13] + p[2] * world[14] + p[3];
  }
  return sceneProjectMesh(mesh, combined, vertices, error);
}

bool sceneDrawSingleBoneMesh(Render2D &renderer, size_t textureId,
                             const ScxMesh &mesh, SceneRenderInstance &instance,
                             const float projection[8], std::string &error) {
  instance.processRootBoneUpdate();
  instance.refreshSingleBoneDrawMatrix();
  std::vector<RenderVertex> vertices;
  if (!sceneProjectTransformedMesh(mesh, instance.drawMatrix(), projection,
                                   vertices, error))
    return false;
  return renderer.drawMesh(textureId, vertices, mesh.indices, error);
}

std::optional<float>
scenePerspectiveUvErrorBound(const std::array<CameraClipVertex, 3> &triangle) {
  const float lo = std::min({triangle[0].position[3], triangle[1].position[3],
                             triangle[2].position[3]});
  const float hi = std::max({triangle[0].position[3], triangle[1].position[3],
                             triangle[2].position[3]});
  if (!(lo > 0) || !std::isfinite(hi))
    return std::nullopt;
  float uvRange = 0;
  for (size_t axis = 0; axis < 2; ++axis) {
    for (const auto &vertex : triangle)
      if (!std::isfinite(vertex.uv[axis]))
        return std::nullopt;
    uvRange = std::max(uvRange,
                       std::max({triangle[0].uv[axis], triangle[1].uv[axis],
                                 triangle[2].uv[axis]}) -
                           std::min({triangle[0].uv[axis], triangle[1].uv[axis],
                                     triangle[2].uv[axis]}));
  }
  return (hi - lo) / lo * uvRange;
}

bool sceneDrawCameraMesh(Render2D &renderer, size_t textureId,
                         const ScxMesh &mesh, SceneRenderInstance &instance,
                         const std::array<float, 16> &view,
                         const std::array<float, 16> &projection,
                         const ViewportInstance &viewport, std::string &error,
                         const CameraFog *fog,
                         const ResourceTexturePixels *texturePixels) {
  if (!mesh.vertexCount ||
      mesh.positions.size() != static_cast<size_t>(mesh.vertexCount) * 3 ||
      mesh.uv1.size() != static_cast<size_t>(mesh.vertexCount) * 2 ||
      mesh.indices.size() % 3 != 0) {
    error = "unsupported camera mesh";
    return false;
  }
  int width, height;
  if (!renderer.outputSize(width, height, error))
    return false;
  instance.processRootBoneUpdate();
  instance.refreshSingleBoneDrawMatrix();
  const auto &world = instance.drawMatrix();
  std::vector<CameraClipVertex> input;
  std::vector<float> fogFactors;
  if (fog &&
      (!texturePixels || !std::isfinite(fog->nearDistance) ||
       !std::isfinite(fog->farDistance) ||
       fog->farDistance <= fog->nearDistance ||
       texturePixels->rgba.size() != static_cast<size_t>(texturePixels->width) *
                                         texturePixels->height * 4)) {
    error = "camera fog: unsupported texture or range";
    return false;
  }
  input.reserve(mesh.vertexCount);
  for (size_t i = 0; i < mesh.vertexCount; ++i) {
    const float x = mesh.positions[i * 3] * 0.1f,
                y = mesh.positions[i * 3 + 1] * 0.1f,
                z = mesh.positions[i * 3 + 2] * 0.1f;
    const float point[4] = {
        x * world[0] + y * world[4] + z * world[8] + world[12],
        x * world[1] + y * world[5] + z * world[9] + world[13],
        x * world[2] + y * world[6] + z * world[10] + world[14], 1};
    float camera[4]{};
    CameraClipVertex vertex{};
    for (size_t col = 0; col < 4; ++col)
      for (size_t row = 0; row < 4; ++row)
        camera[col] += point[row] * view[row * 4 + col];
    for (size_t col = 0; col < 4; ++col)
      for (size_t row = 0; row < 4; ++row)
        vertex.position[col] += camera[row] * projection[row * 4 + col];
    vertex.uv = {mesh.uv1[i * 2], mesh.uv1[i * 2 + 1]};
    if (fog) {
      const double distance =
          std::sqrt(static_cast<double>(camera[0]) * camera[0] +
                    static_cast<double>(camera[1]) * camera[1] +
                    static_cast<double>(camera[2]) * camera[2]);
      if (!std::isfinite(distance)) {
        error = "camera fog: invalid distance";
        return false;
      }
      fogFactors.push_back(static_cast<float>(
          std::clamp((fog->farDistance - distance) /
                         (fog->farDistance - fog->nearDistance),
                     0.0, 1.0)));
    }
    input.push_back(vertex);
  }
  std::vector<RenderVertex> vertices;
  std::vector<RenderVertex> fogVertices;
  std::vector<uint32_t> indices;
  // Bound the difference between affine and perspective UV interpolation by
  // (max(w) - min(w)) / min(w) times the UV range on each axis.
  constexpr float maxUvError = 1.0f / 65536.0f;
  constexpr size_t maxVertices = 1000000;
  const auto midpoint = [](const CameraClipVertex &a,
                           const CameraClipVertex &b) {
    CameraClipVertex result{};
    for (size_t i = 0; i < 4; ++i)
      result.position[i] = (a.position[i] + b.position[i]) * 0.5f;
    for (size_t i = 0; i < 2; ++i)
      result.uv[i] = (a.uv[i] + b.uv[i]) * 0.5f;
    return result;
  };
  const auto emit =
      [&](auto &&self, const std::array<CameraClipVertex, 3> &triangle,
          const std::array<float, 3> &factors, unsigned depth) -> bool {
    const auto bound = scenePerspectiveUvErrorBound(triangle);
    if (!bound) {
      error = "invalid clipped camera vertex";
      return false;
    }
    if (*bound > maxUvError) {
      if (depth == 8 || vertices.size() >= maxVertices) {
        error = "camera mesh perspective approximation limit";
        return false;
      }
      const auto ab = midpoint(triangle[0], triangle[1]);
      const auto bc = midpoint(triangle[1], triangle[2]);
      const auto ca = midpoint(triangle[2], triangle[0]);
      const float fab = (factors[0] + factors[1]) * 0.5f;
      const float fbc = (factors[1] + factors[2]) * 0.5f;
      const float fca = (factors[2] + factors[0]) * 0.5f;
      return self(self, {triangle[0], ab, ca}, {factors[0], fab, fca},
                  depth + 1) &&
             self(self, {ab, triangle[1], bc}, {fab, factors[1], fbc},
                  depth + 1) &&
             self(self, {ca, bc, triangle[2]}, {fca, fbc, factors[2]},
                  depth + 1) &&
             self(self, {ab, bc, ca}, {fab, fbc, fca}, depth + 1);
    }
    if (vertices.size() + 3 > maxVertices) {
      error = "camera mesh perspective approximation limit";
      return false;
    }
    const auto first = static_cast<uint32_t>(vertices.size());
    for (size_t n = 0; n < 3; ++n) {
      const auto &vertex = triangle[n];
      const float w = vertex.position[3];
      const float x = std::clamp(vertex.position[0] / w, -1.0f, 1.0f);
      const float y = std::clamp(vertex.position[1] / w, -1.0f, 1.0f);
      vertices.push_back(
          {(viewport.left + (x + 1) * viewport.width * 0.5f) * width,
           (viewport.top + (1 - y) * viewport.height * 0.5f) * height,
           vertex.uv[0], vertex.uv[1], instance.instanceColor()});
      if (fog) {
        const uint32_t f = static_cast<uint32_t>(
            std::clamp(factors[n], 0.0f, 1.0f) * 255.0f + 0.5f);
        vertices.back().color = sceneTint(0xff000000u | (f << 16) | (f << 8) | f,
                                         instance.instanceColor());
        auto overlay = vertices.back();
        overlay.color = ((255u - f) << 24) |
                        (static_cast<uint32_t>(fog->color) & 0xffffffu);
        fogVertices.push_back(overlay);
      }
    }
    indices.insert(indices.end(), {first, first + 1, first + 2});
    return true;
  };
  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    const auto a = mesh.indices[i], b = mesh.indices[i + 1],
               c = mesh.indices[i + 2];
    if (a >= input.size() || b >= input.size() || c >= input.size()) {
      error = "invalid camera mesh index";
      return false;
    }
    const auto polygon = clipCameraTriangle({input[a], input[b], input[c]});
    if (!polygon) {
      error = "invalid camera mesh vertex";
      return false;
    }
    std::optional<std::vector<CameraClipVertex>> fogPolygon;
    if (fog) {
      auto fa = input[a], fb = input[b], fc = input[c];
      fa.uv[0] = fogFactors[a];
      fb.uv[0] = fogFactors[b];
      fc.uv[0] = fogFactors[c];
      fogPolygon = clipCameraTriangle({fa, fb, fc});
      if (!fogPolygon || fogPolygon->size() != polygon->size()) {
        error = "camera fog: clipping failed";
        return false;
      }
    }
    for (size_t j = 1; j + 1 < polygon->size(); ++j)
      if (!emit(emit, {(*polygon)[0], (*polygon)[j], (*polygon)[j + 1]},
                fog ? std::array<float, 3>{(*fogPolygon)[0].uv[0],
                                           (*fogPolygon)[j].uv[0],
                                           (*fogPolygon)[j + 1].uv[0]}
                    : std::array<float, 3>{1, 1, 1},
                0))
        return false;
  }
  if (indices.empty()) {
    error.clear();
    return true;
  }
  if (!fog)
    return renderer.drawMesh(textureId, vertices, indices, error);
  std::vector<uint8_t> mask = texturePixels->rgba;
  for (size_t i = 0; i < mask.size(); i += 4)
    mask[i] = mask[i + 1] = mask[i + 2] = 255;
  const auto fogTexture =
      renderer.texture(texturePixels->width, texturePixels->height, mask.data(),
                       mask.size(), error);
  if (!fogTexture)
    return false;
  bool drawn = true;
  for (size_t i = 0; i < vertices.size() && drawn; i += 3) {
    const std::vector<uint32_t> tri{0, 1, 2};
    const std::vector<RenderVertex> base(vertices.begin() + i,
                                         vertices.begin() + i + 3);
    const std::vector<RenderVertex> overlay(fogVertices.begin() + i,
                                            fogVertices.begin() + i + 3);
    drawn = renderer.drawMesh(textureId, base, tri, error) &&
            renderer.drawMeshAdditive(*fogTexture, overlay, tri, error);
  }
  std::string releaseError;
  const bool released = renderer.releaseTexture(*fogTexture, releaseError);
  if (drawn && !released)
    error = releaseError;
  return drawn && released;
}

bool sceneRefreshMaterial(ResourceWorld &world,
                          const SceneMaterialResources &material,
                          const std::filesystem::path &root,
                          SceneMaterialCache &cache, std::string &error,
                          bool requireCached, bool textGeometry) {
  cache = {};
  if (material.textureCount() != 1) {
    error = "material texture count unsupported";
    return false;
  }
  SceneMaterialCache next;
  if (material.flags() & 0x8000u) {
    error = "material dynamic batch allocation unsupported";
    return false;
  }
  if (requireCached) {
    const auto mesh = world.lookup(material.meshId(), 5);
    if (mesh.status != LookupStatus::Found || !world.sceneMesh(mesh.node)) {
      error =
          "material mesh is not cached: " + std::to_string(material.meshId());
      return false;
    }
  }
  if (!sceneLoadMesh(world, material.meshId(), root, next.mesh, error,
                     textGeometry || material.flags() == 4100))
    return false;
  if (!sceneRefreshTexture(world, material, root, next.texture, error,
                           requireCached))
    return false;
  next.flags = material.flags();
  next.meshId = material.meshId();
  next.textureId = material.textureId(0);
  cache = std::move(next);
  return true;
}

static std::optional<bool>
sceneOpaqueMaterial(const SceneMaterialResources &material,
                    const ScxModel &model, std::string &error, bool cameraPlane) {
  if (material.textureCount() != 1 || model.meshes.size() != 1 ||
      model.materials.size() != 1 || !model.meshes[0].materialIndex ||
      *model.meshes[0].materialIndex != 0) {
    error = "render material shape unsupported";
    return std::nullopt;
  }
  const auto& mesh = model.meshes[0];
  // The stock cluster uses ColorMixTex with neutral-white color/emissive
  // streams. This reduces to its atlas layer, with the encoded alpha blend.
  const bool neutralColorMix = cameraPlane && model.materials[0].flags == 3 &&
      !mesh.color.empty() && !mesh.emissive.empty() &&
      std::all_of(mesh.color.begin(), mesh.color.end(), [](uint8_t x) { return x == 255; }) &&
      std::all_of(mesh.emissive.begin(), mesh.emissive.end(), [](uint8_t x) { return x == 255; });
  const auto function = model.materials[0].functionWord;
  if (((cameraPlane && material.flags() == 0) || material.flags() == 4096) &&
      ((model.materials[0].flags == 0 && function == 0x40000000u) ||
       (neutralColorMix && function == 0xc0000000u)))
    return false;
  if (material.flags() == 0 && model.materials[0].flags == 0 && function == 0)
    return true;
  error = "render material pass unsupported";
  return std::nullopt;
}

static bool sceneSpatialMesh(const ScxMesh &mesh, SceneRenderInstance &instance,
                             const std::array<float, 16> &view,
                             const std::array<float, 16> &projection) {
  if ((!instance.cameraPlane() && (mesh.vertexCount != 4 || mesh.indices.size() != 6)) ||
      mesh.vertexCount < 3 || mesh.positions.size() != size_t(mesh.vertexCount) * 3 ||
      mesh.indices.empty() || mesh.indices.size() % 3 != 0)
    return true;
  instance.processRootBoneUpdate();
  instance.refreshSingleBoneDrawMatrix();
  const auto &world = instance.drawMatrix();
  double firstZ = 0, firstW = 0;
  for (size_t i = 0; i < mesh.vertexCount; ++i) {
    const double x = mesh.positions[i * 3] * 0.1,
                 y = mesh.positions[i * 3 + 1] * 0.1,
                 z = mesh.positions[i * 3 + 2] * 0.1;
    const double point[4] = {
        x * world[0] + y * world[4] + z * world[8] + world[12],
        x * world[1] + y * world[5] + z * world[9] + world[13],
        x * world[2] + y * world[6] + z * world[10] + world[14], 1};
    double camera[4]{}, clipZ = 0, clipW = 0;
    for (size_t col = 0; col < 4; ++col)
      for (size_t row = 0; row < 4; ++row)
        camera[col] += point[row] * view[row * 4 + col];
    for (size_t row = 0; row < 4; ++row) {
      clipZ += camera[row] * projection[row * 4 + 2];
      clipW += camera[row] * projection[row * 4 + 3];
    }
    if (!std::isfinite(clipZ) || !std::isfinite(clipW) || clipW <= 0)
      return true;
    if (i == 0) {
      firstZ = clipZ;
      firstW = clipW;
      continue;
    }
    const double a = clipZ * firstW, b = firstZ * clipW;
    if (std::abs(a - b) > 1e-6 * std::max({1.0, std::abs(a), std::abs(b)}))
      return true;
  }
  return false;
}

struct SceneMeshView {
  std::shared_ptr<ScxModel> cached;
  ScxModel loaded;
  const ScxModel *model = nullptr;
};

static bool sceneMeshView(ResourceWorld &resources,
                          const SceneMaterialResources &material,
                          bool requireCached, SceneMeshView &view,
                          std::string &error) {
  if (material.textureCount() != 1) {
    error = "material texture count unsupported";
    return false;
  }
  if (material.flags() & 0x8000u) {
    error = "material dynamic batch allocation unsupported";
    return false;
  }
  const auto node = resources.lookup(material.meshId(), 5);
  if (node.status == LookupStatus::Found)
    view.cached = resources.sceneMesh(node.node);
  if (view.cached) {
    view.model = view.cached.get();
    return true;
  }
  if (requireCached) {
    error = "material mesh is not cached: " + std::to_string(material.meshId());
    return false;
  }
  if (!sceneLoadMesh(resources, material.meshId(), resources.packs().root(),
                     view.loaded, error, material.flags() == 4100))
    return false;
  view.model = &view.loaded;
  return true;
}

SceneMaterialRenderer::SceneMaterialRenderer(ResourceWorld &resources,
                                             Render2D &renderer)
    : resources_(resources), renderer_(renderer) {}
SceneMaterialRenderer::~SceneMaterialRenderer() {
  setLinkOwner(node_, nullptr);
}

std::unique_ptr<SceneMaterialRenderer>
SceneMaterialRenderer::create(ResourceWorld &resources, Render2D &renderer,
                              WorldNode &node, std::string &error,
                              bool requireCached) {
  const auto resolved = resources.resolve(&node);
  const auto instance = resolved && resolved.type == 3
                            ? resources.sceneRenderInstance(resolved)
                            : nullptr;
  if (!instance) {
    error = "render instance unavailable";
    return nullptr;
  }
  const auto type = resources.lookup(instance->typeId(), 14);
  if (requireCached && type.status == LookupStatus::Found &&
      (type.node.packNode || type.node.worldNode)) {
    const auto result = cacheDrawMaterial(resources, renderer, type.node);
    if (const auto *failure = std::get_if<std::string>(&result)) {
      error = *failure;
      return nullptr;
    }
  }
  const auto material = type.status == LookupStatus::Found
                            ? resources.sceneMaterial(type.node)
                            : nullptr;
  if (!material) {
    const auto anyType = resources.lookup(instance->typeId());
    error =
        "render material unavailable (type " +
        std::to_string(instance->typeId()) + ", typed lookup " +
        std::to_string(static_cast<int>(type.status)) + ", unfiltered " +
        std::to_string(static_cast<int>(anyType.status)) + " type " +
        std::to_string(anyType.node.type) + ", " +
        (anyType.node.packNode
             ? "pack " + std::to_string(anyType.node.packNode->payloadSlot) +
                   ":" + std::to_string(anyType.node.packNode->payloadOffset)
         : anyType.node.worldNode ? "world " + anyType.node.worldNode->name
                                  : "none") +
        ")";
    return nullptr;
  }
  if (material->flags() == 4100 && !instance->textState()) {
    error = "font render instance has no text state";
    return nullptr;
  }
  SceneMaterialCache cache;
  if (!sceneRefreshMaterial(resources, *material, resources.packs().root(),
                            cache, error, requireCached, instance->textState().has_value())) {
    if (instance->textState())
      error += " (font " + std::to_string(instance->typeId()) +
               ", text length " +
               std::to_string(instance->textState()->text.size()) + ")";
    return nullptr;
  }
  if (cache.mesh.meshes.size() != 1) {
    error = "multiple render meshes unsupported";
    return nullptr;
  }
  if (!instance->textState() &&
      !sceneOpaqueMaterial(*material, cache.mesh, error, instance->cameraPlane()))
    return nullptr;
  auto object = std::unique_ptr<SceneMaterialRenderer>(
      new SceneMaterialRenderer(resources, renderer));
  setLinkOwner(object->node_, &node);
  if (instance->textState())
    object->textMaterialId_ = instance->typeId();
  error.clear();
  return object;
}

bool SceneMaterialRenderer::draw(const std::array<float, 16> &view,
                                 const std::array<float, 16> &projection,
                                 const ViewportInstance &viewport,
                                 std::string &error, bool requireCached,
                                 const CameraFog *fog) {
  const auto resolved = resources_.resolve(node_.owner);
  const auto instance = resolved && resolved.type == 3
                            ? resources_.sceneRenderInstance(resolved)
                            : nullptr;
  if (!instance) {
    error = "render instance unavailable";
    return false;
  }
  const auto type = resources_.lookup(instance->typeId(), 14);
  if (requireCached && type.status == LookupStatus::Found &&
      (type.node.packNode || type.node.worldNode)) {
    const auto result = cacheDrawMaterial(resources_, renderer_, type.node);
    if (const auto *failure = std::get_if<std::string>(&result)) {
      error = *failure;
      return false;
    }
  }
  const auto material = type.status == LookupStatus::Found
                            ? resources_.sceneMaterial(type.node)
                            : nullptr;
  if (!material) {
    error = "render material unavailable";
    return false;
  }
  SceneMaterialCache cache;
  if (!sceneRefreshMaterial(resources_, *material, resources_.packs().root(),
                            cache, error, requireCached, instance->textState().has_value()))
    return false;
  if (cache.mesh.meshes.size() != 1) {
    error = "multiple render meshes unsupported";
    return false;
  }
  if (!instance->textState() &&
      !sceneOpaqueMaterial(*material, cache.mesh, error, instance->cameraPlane()))
    return false;
  if (!instance->textState() && cache.flags == 0 &&
      sceneSpatialMesh(cache.mesh.meshes[0], *instance, view, projection)) {
    error = "spatial mesh requires a camera depth buffer";
    return false;
  }
  const auto textureNode = resources_.lookup(cache.textureId, 7);
  const auto shared =
      textureNode.status == LookupStatus::Found && textureNode.node.packNode
          ? textureNode.node.packNode->texture
          : nullptr;
  const bool reuse = shared && shared->renderer == &renderer_ && shared->handle;
  if (requireCached && !reuse) {
    error = "material texture belongs to another renderer";
    return false;
  }
  const auto texture =
      reuse ? std::optional<size_t>{*shared->handle}
            : renderer_.texture(cache.texture.width, cache.texture.height,
                                cache.texture.rgba.data(),
                                cache.texture.rgba.size(), error);
  if (!texture)
    return false;
  bool drawn = false;
  // FUN_0048c4b0/FUN_0048c670 select text on the instance, not material flags.
  // Stock cars.rpk HUD fonts have material flags 4, unlike scripted fonts 4100.
  if (instance->textState()) {
    const auto &text = instance->textState();
    {
      static std::vector<std::string> seenText;
      const bool fresh =
          text && !text->text.empty() && seenText.size() < 40 &&
          std::find(seenText.begin(), seenText.end(), text->text) ==
              seenText.end();
      if (fresh) {
        seenText.push_back(text->text);
        runlog::event(runlog::EventKind::Note,
                      "text draw: '%s' align=%d color=0x%x scale=%.2f",
                      text->text.c_str(), text->alignment, text->color,
                      static_cast<double>(text->advanceScale));
      }
    }
    if (!text)
      error = "text render: unsupported state";
    else if (text->text.empty())
      drawn = true;
    else {
      instance->processRootBoneUpdate();
      const auto &matrix = instance->derivedRootBoneMatrix();
      int width = 0, height = 0;
      std::vector<RenderVertex> vertices;
      std::vector<uint32_t> indices;
      if (renderer_.outputSize(width, height, error)) {
        const float x = viewport.left * width + (matrix[12] * 0.1f + 1.0f) *
                                                    viewport.width * width *
                                                    0.5f;
        const float y = viewport.top * height + (matrix[13] * 0.1f + 1.0f) *
                                                    viewport.height * height *
                                                    0.5f;
        if (fontGlyphGeometry(cache.mesh, text->text, x, y, text->alignment,
                              text->advanceScale, vertices, indices, error,
                              text->color)) {
          if (instance->instanceColor() != 0xffffffffu)
            for (auto& vertex : vertices)
              vertex.color = sceneTint(vertex.color, instance->instanceColor());
          drawn = vertices.empty() ||
                  renderer_.drawMesh(*texture, vertices, indices, error);
        }
      }
    }
  } else
    drawn = sceneDrawCameraMesh(renderer_, *texture, cache.mesh.meshes[0],
                                *instance, view, projection, viewport, error,
                                fog, &cache.texture);
  std::string releaseError;
  const bool released =
      reuse || renderer_.releaseTexture(*texture, releaseError);
  if (drawn && !released)
    error = releaseError;
  return drawn && released;
}

std::optional<uint32_t> SceneMaterialRenderer::textMaterialId() const {
  return textMaterialId_;
}

std::optional<bool> SceneMaterialRenderer::opaque(std::string &error,
                                                  bool requireCached) {
  const auto resolved = resources_.resolve(node_.owner);
  const auto instance = resolved && resolved.type == 3
                            ? resources_.sceneRenderInstance(resolved)
                            : nullptr;
  if (!instance) {
    error = "render instance unavailable";
    return std::nullopt;
  }
  const auto type = resources_.lookup(instance->typeId(), 14);
  const auto material = type.status == LookupStatus::Found
                            ? resources_.sceneMaterial(type.node)
                            : nullptr;
  if (!material) {
    error = "render material unavailable";
    return std::nullopt;
  }
  if (requireCached) {
    const auto result = cacheDrawMaterial(resources_, renderer_, type.node);
    if (const auto *failure = std::get_if<std::string>(&result)) {
      error = *failure;
      return std::nullopt;
    }
  }
  SceneMeshView mesh;
  if (!sceneMeshView(resources_, *material, requireCached, mesh, error))
    return std::nullopt;
  return sceneOpaqueMaterial(*material, *mesh.model, error, instance->cameraPlane());
}

std::optional<bool>
SceneMaterialRenderer::spatial(const std::array<float, 16> &view,
                               const std::array<float, 16> &projection,
                               std::string &error, bool requireCached) {
  const auto resolved = resources_.resolve(node_.owner);
  const auto instance = resolved && resolved.type == 3
                            ? resources_.sceneRenderInstance(resolved)
                            : nullptr;
  if (!instance) {
    error = "render instance unavailable";
    return std::nullopt;
  }
  const auto type = resources_.lookup(instance->typeId(), 14);
  const auto material = type.status == LookupStatus::Found
                            ? resources_.sceneMaterial(type.node)
                            : nullptr;
  if (!material) {
    error = "render material unavailable";
    return std::nullopt;
  }
  SceneMeshView mesh;
  if (!sceneMeshView(resources_, *material, requireCached, mesh, error))
    return std::nullopt;
  if (!sceneOpaqueMaterial(*material, *mesh.model, error, instance->cameraPlane()))
    return std::nullopt;
  return material->flags() == 0 &&
         sceneSpatialMesh(mesh.model->meshes[0], *instance, view, projection);
}

bool SceneMaterialRenderer::drawSpatial(
    Raster3D &raster, SceneSpatialTextures &textures,
    const std::array<float, 16> &view, const std::array<float, 16> &projection,
    const ViewportInstance &viewport, std::string &error, bool requireCached) {
  const auto resolved = resources_.resolve(node_.owner);
  const auto instance = resolved && resolved.type == 3
                            ? resources_.sceneRenderInstance(resolved)
                            : nullptr;
  if (!instance) {
    error = "render instance unavailable";
    return false;
  }
  const auto type = resources_.lookup(instance->typeId(), 14);
  const auto material = type.status == LookupStatus::Found
                            ? resources_.sceneMaterial(type.node)
                            : nullptr;
  if (!material) {
    error = "render material unavailable";
    return false;
  }
  SceneMeshView model;
  if (!sceneMeshView(resources_, *material, requireCached, model, error))
    return false;
  const auto opaque = sceneOpaqueMaterial(*material, *model.model, error, instance->cameraPlane());
  if (!opaque || !*opaque) {
    if (opaque)
      error = "spatial transparent material unsupported";
    return false;
  }
  const auto &mesh = model.model->meshes[0];
  const auto &scxMaterial = model.model->materials[0];
  if (!mesh.normals.empty() || !mesh.color.empty() || !mesh.emissive.empty() ||
      !mesh.boneIndices.empty() || !mesh.weights.empty() || !mesh.uv2.empty() ||
      !mesh.uv3.empty() || !mesh.tangents.empty()) {
    error = "spatial mesh lighting, skinning or extra streams unsupported";
    return false;
  }
  if (scxMaterial.maps.size() != 1 || scxMaterial.maps[0].slot != 0 ||
      scxMaterial.maps[0].textureIndex != 0 ||
      scxMaterial.maps[0].channel != 0 || scxMaterial.maps[0].tiling != 0 ||
      scxMaterial.maps[0].scaleU != 1 || scxMaterial.maps[0].scaleV != 1 ||
      scxMaterial.maps[0].offsetU != 0 || scxMaterial.maps[0].offsetV != 0 ||
      !scxMaterial.properties.empty() ||
      scxMaterial.diffuse != std::array<float, 4>{1, 1, 1, 1}) {
    error = "spatial material mapping or lighting unsupported";
    return false;
  }
  if (!mesh.vertexCount ||
      mesh.positions.size() != static_cast<size_t>(mesh.vertexCount) * 3 ||
      mesh.uv1.size() != static_cast<size_t>(mesh.vertexCount) * 2) {
    error = "spatial mesh vertex data unsupported";
    return false;
  }
  const uint32_t textureId = material->textureId(0);
  const auto textureNode = resources_.lookup(textureId, 7);
  const auto shared =
      textureNode.status == LookupStatus::Found && textureNode.node.packNode
          ? textureNode.node.packNode->texture
          : nullptr;
  if (requireCached &&
      (!shared || shared->renderer != &renderer_ || !shared->handle)) {
    error = "material texture belongs to another renderer";
    return false;
  }
  ResourceTexturePixels loadedPixels;
  const ResourceTexturePixels *pixels = nullptr;
  if (shared)
    pixels = &shared->pixels;
  else {
    if (!sceneRefreshTexture(resources_, *material, resources_.packs().root(),
                             loadedPixels, error, requireCached))
      return false;
    pixels = &loadedPixels;
  }
  auto &entry = textures[textureId];
  if (entry.texture.levels.empty() ||
      entry.texture.levels[0].width != pixels->width ||
      entry.texture.levels[0].height != pixels->height ||
      entry.texture.levels[0].rgba != pixels->rgba) {
    Raster3DTexture replacement;
    if (!Raster3DTexture::build(pixels->width, pixels->height,
                                pixels->rgba.data(), pixels->rgba.size(),
                                replacement, error))
      return false;
    entry.texture = std::move(replacement);
  }
  entry.used = true;
  instance->processRootBoneUpdate();
  instance->refreshSingleBoneDrawMatrix();
  Raster3DDraw draw;
  draw.positions = mesh.positions.data();
  draw.vertexCount = mesh.vertexCount;
  draw.uvs = mesh.uv1.data();
  draw.indices = mesh.indices.data();
  draw.indexCount = mesh.indices.size();
  draw.positionScale = 0.1f;
  draw.model = instance->drawMatrix();
  draw.view = view;
  draw.projection = projection;
  draw.viewport = {viewport.left, viewport.top, viewport.width,
                   viewport.height};
  draw.material.texture = &entry.texture;
  return raster.draw(draw, error);
}

std::optional<float> SceneMaterialRenderer::transparentDepth(
    const std::array<float, 16> &cameraWorld, std::string &error,
    bool requireCached) {
  const auto resolved = resources_.resolve(node_.owner);
  const auto instance = resolved && resolved.type == 3
                            ? resources_.sceneRenderInstance(resolved)
                            : nullptr;
  if (!instance) {
    error = "render instance unavailable";
    return std::nullopt;
  }
  const auto type = resources_.lookup(instance->typeId(), 14);
  const auto material = type.status == LookupStatus::Found
                            ? resources_.sceneMaterial(type.node)
                            : nullptr;
  if (!material) {
    error = "render material unavailable";
    return std::nullopt;
  }
  if (requireCached) {
    const auto result = cacheDrawMaterial(resources_, renderer_, type.node);
    if (const auto *failure = std::get_if<std::string>(&result)) {
      error = *failure;
      return std::nullopt;
    }
  }
  SceneMaterialCache cache;
  if (!sceneRefreshMaterial(resources_, *material, resources_.packs().root(),
                            cache, error, requireCached))
    return std::nullopt;
  const auto opaque = sceneOpaqueMaterial(*material, cache.mesh, error, instance->cameraPlane());
  if (!opaque || *opaque) {
    error = "multiple-object render pass unsupported";
    return std::nullopt;
  }
  instance->processRootBoneUpdate();
  instance->refreshSingleBoneDrawMatrix();
  const auto depth = sceneTransparentDepth(instance->drawMatrix(), cameraWorld);
  if (!depth)
    error = "invalid transparent depth";
  return depth;
}

SceneCameraRenderer::~SceneCameraRenderer() {
  if (spatialUpload_) {
    std::string ignored;
    renderer_.releaseTexture(*spatialUpload_, ignored);
  }
}

bool SceneCameraRenderer::draw(const std::array<float, 16> &view,
                               const std::array<float, 16> &projection,
                               const ViewportInstance &viewport,
                               std::string &error, bool requireCached,
                               const CameraFog *fog, uint32_t globalEnvmapId) {
  pendingGround_.clear();
  bool ok = true;
  candidates_.drain(0, true, [&](LinkOwner &owner) -> uint32_t {
    if (!ok)
      return 0;
    const auto node = resources_.resolve(&owner);
    if (node && node.type == 1 && node.packNode) {
      pendingGround_.push_back(node.packNode);
      return 0;
    }
    if (!node || !node.worldNode || node.type != 3) {
      error = "camera candidate unavailable";
      ok = false;
      return 0;
    }
    const auto instance = resources_.sceneRenderInstance(node);
    if (!instance) {
      // Object LOD teardown can destroy the render child between traversal
      // and this drain; the candidate is dead, not the frame.
      candidates_.remove(owner);
      return 0;
    }
    if (instance->typeId() == 0) {
      if (!candidates_.markEmptyCreated(owner)) {
        error = "camera empty-type record unavailable";
        ok = false;
      }
      return 0;
    }
    const auto linked = resources_.lookup(instance->typeId());
    if (linked.status == LookupStatus::Found && linked.node.type == 12)
      return 0;
    if (linked.status == LookupStatus::Found && linked.node.type == 13) {
      if (linked.node.packNode &&
          !resources_.loadSceneLight(*linked.node.packNode, error)) {
        ok = false;
        return 0;
      }
      if (!resources_.sceneLight(linked.node)) {
        error = "camera light source unavailable";
        ok = false;
        return 0;
      }
      auto light = std::make_unique<CameraLight>();
      setLinkOwner(light->instance, &owner);
      setLinkOwner(light->type, linked.node.owner);
      if (!candidates_.markLightCreated(owner)) {
        error = "camera light record unavailable";
        ok = false;
        return 0;
      }
      lights_.push_back(std::move(light));
      return 0;
    }
    if (linked.status == LookupStatus::Found && linked.node.type == 14 &&
        !resources_.sceneMaterial(linked.node)) {
      // Try to bind the material lazily; ground and RenderRef creation bind
      // eagerly, script-created refs may not. If the payload cannot resolve
      // (e.g. the skydome clones), record the instance without a renderer:
      // a missing sky/material is a visual gap, not a failed frame.
      auto loaded = sceneLoadMaterial(resources_, linked.node.id);
      if (loaded.material) {
        resources_.bindSceneMaterial(linked.node,
                                     std::shared_ptr<SceneMaterialResources>(
                                         std::move(loaded.material)));
      }
      if (!resources_.sceneMaterial(linked.node)) {
        if (!candidates_.markEmptyCreated(owner)) {
          error = "camera unbound-material record unavailable";
          ok = false;
        }
        return 0;
      }
    }
    if (requireCached && instance->textState() &&
        linked.status == LookupStatus::Found && linked.node.type == 14 &&
        linked.node.packNode && !linked.node.packNode->loaded()) {
      if (!resources_.queueFontLoad(linked.node, error))
        ok = false;
      return 0;
    }
    // Spatial meshes outside the bounded single-quad path belong to the
    // shared world rasterizer, not the UI renderer.
    if (!instance->textState() && linked.status == LookupStatus::Found &&
        linked.node.type == 14) {
      const auto material = resources_.sceneMaterial(linked.node);
      SceneMeshView mesh;
      std::string meshError;
      if (material &&
          sceneMeshView(resources_, *material, requireCached, mesh,
                        meshError) &&
          !mesh.model->meshes.empty() &&
          sceneSpatialMesh(mesh.model->meshes[0], *instance, view,
                           projection)) {
        const auto &m = mesh.model->meshes[0];
        const bool simpleMaterial =
            mesh.model->materials.size() == 1 &&
            mesh.model->materials[0].maps.size() == 1 &&
            mesh.model->materials[0].maps[0].slot == 0 &&
            mesh.model->materials[0].maps[0].textureIndex == 0 &&
            mesh.model->materials[0].maps[0].channel == 0 &&
            mesh.model->materials[0].maps[0].tiling == 0 &&
            mesh.model->materials[0].maps[0].scaleU == 1 &&
            mesh.model->materials[0].maps[0].scaleV == 1 &&
            mesh.model->materials[0].maps[0].offsetU == 0 &&
            mesh.model->materials[0].maps[0].offsetV == 0 &&
            mesh.model->materials[0].properties.empty() &&
            mesh.model->materials[0].diffuse ==
                std::array<float, 4>{1, 1, 1, 1};
        if (!simpleMaterial || !m.normals.empty() || !m.color.empty() ||
            !m.emissive.empty() || !m.boneIndices.empty() ||
            !m.weights.empty() || !m.uv2.empty() || !m.uv3.empty() ||
            !m.tangents.empty()) {
          worldRaster_.addObject(owner);
          candidates_.markEmptyCreated(owner);
          return 0;
        }
      }
    }
    auto object = SceneMaterialRenderer::create(
        resources_, renderer_, *node.worldNode, error, requireCached);
    if (!object) {
      if (!instance->textState() && linked.status == LookupStatus::Found &&
          linked.node.type == 14) {
        worldRaster_.addObject(owner);
        error.clear();
        candidates_.markEmptyCreated(owner);
        return 0;
      }
      // The bounded single-material renderer supports two exact passes.
      // World visuals with other passes (e.g. the skydome) are recorded
      // without a renderer; the raster3d scene path owns them.
      if (error.rfind("render material pass unsupported", 0) == 0 ||
          error.rfind("render material shape unsupported", 0) == 0 ||
          error.rfind("material texture count unsupported", 0) == 0 ||
          error.rfind("material dynamic batch allocation unsupported", 0) ==
              0) {
        error.clear();
        if (!candidates_.markEmptyCreated(owner)) {
          error = "camera skipped-pass record unavailable";
          ok = false;
        }
        return 0;
      }
      ok = false;
      return 0;
    }
    if (!candidates_.markMaterialCreated(owner)) {
      error = "camera candidate record unavailable";
      ok = false;
      return 0;
    }
    objects_.push_back(std::move(object));
    return 0;
  });
  if (!ok)
    return false;
  {
    static double lastViewLog = -1e9;
    const double nowView = runlog::monotonicSeconds();
    if (nowView - lastViewLog >= 5.0) {
      lastViewLog = nowView;
      float carX = 0, carY = 0, carZ = 0;
      for (auto *g : pendingGround_)
        (void)g;
      runlog::event(runlog::EventKind::Note,
                    "renderer view: t=(%.1f,%.1f,%.1f) det=%.3f",
                    view[12], view[13], view[14],
                    view[0] * (view[5] * view[10] - view[6] * view[9]) -
                        view[1] * (view[4] * view[10] - view[6] * view[8]) +
                        view[2] * (view[4] * view[9] - view[5] * view[8]));
      (void)carX;
      (void)carY;
      (void)carZ;
    }
  }
  // Feed the scene's directional light into the world raster pass. Stock
  // lights every world vertex in software (FUN_004bb7f0: per-vertex ambient +
  // N.L-scaled diffuse from the active light records) while the D3D
  // fixed-function path multiplies it with the material/texture. Only a
  // directional source (kind 3) is supported here; point/spot lights need
  // per-light positions (logged gap above for kinds 0/4/5 selection).
  Raster3DLight worldLight;
  uint32_t kindScratch = 0;
  for (auto& cameraLightPtr : lights_) {
    auto& cameraLight = *cameraLightPtr;
    const auto node = resources_.resolve(cameraLight.instance.owner);
    const auto instance = node ? resources_.sceneRenderInstance(node) : nullptr;
    if (!instance || !cameraLight.type.owner)
      continue;
    const auto type = resources_.resolve(cameraLight.type.owner);
    const auto source = type ? resources_.sceneLight(type) : nullptr;
    if (!source || source->sourceKind != 3)
      continue;
    worldLight = sceneWorldRasterLight(*source);
    break;
  }
  if (!worldLight.enabled) {
    // No camera light candidate carried a directional source; fall back to
    // the first bound scene light (the cfg's RenderRef.setLight target). The
    // camera-candidate path stays preferred because stock also positions
    // point/spot lights per object there.
    if (const auto* fallback = resources_.firstSceneLight(kindScratch)) {
      worldLight = sceneWorldRasterLight(*fallback);
      static bool fallbackLogged = false;
      if (!fallbackLogged) {
        fallbackLogged = true;
        runlog::event(
            runlog::EventKind::Note,
            "world raster light fallback: light=0x%x kind=%u diffuse=(%.2f,"
            "%.2f,%.2f) ambient=(%.2f,%.2f,%.2f)",
            kindScratch, fallback->sourceKind, fallback->at10[0],
            fallback->at10[1], fallback->at10[2], fallback->at30[0],
            fallback->at30[1], fallback->at30[2]);
      }
    }
  }
  worldRaster_.draw(resources_, renderer_, pendingGround_, view, projection,
                    viewport, fog, worldLight.enabled ? &worldLight : nullptr,
                    globalEnvmapId);
  pendingGround_.clear();
  for (size_t i = 0; i < objects_.size();) {
    bool stale = !objects_[i]->owner();
    if (!stale) {
      const auto resolved = resources_.resolve(objects_[i]->owner());
      const auto instance = resolved && resolved.type == 3
                                ? resources_.sceneRenderInstance(resolved)
                                : nullptr;
      // Object LOD teardown can drop the render binding while the world
      // node link lingers; treat both as removal instead of a failed frame.
      stale = !instance;
    }
    if (stale)
      eraseObject(i);
    else
      ++i;
  }
  for (size_t i = 0; i < lights_.size();) {
    auto &light = *lights_[i];
    const auto node = resources_.resolve(light.instance.owner);
    const auto instance = node ? resources_.sceneRenderInstance(node) : nullptr;
    if (!instance || !light.type.owner ||
        instance->typeId() != light.type.cachedValue) {
      if (light.instance.owner)
        candidates_.remove(*light.instance.owner);
      lights_.erase(lights_.begin() + i);
      continue;
    }
    const auto type = resources_.resolve(light.type.owner);
    const auto source = type ? resources_.sceneLight(type) : nullptr;
    if (!source) {
      error = "camera light source unavailable";
      return false;
    }
    // FUN_00508890 marks source kinds 0, 4 and 5 inactive (+0x3c).
    // Other kinds require per-object selection and the lighting pass.
    if (source->sourceKind != 0 && source->sourceKind != 4 &&
        source->sourceKind != 5) {
      static bool logged = false;
      if (!logged) {
        runlog::eventText(
            runlog::EventKind::Note,
            "world raster: per-object light selection unsupported");
        logged = true;
      }
    }
    ++i;
  }
  std::vector<SceneMaterialRenderer *> opaqueObjects, rectangles;
  std::vector<std::pair<uint32_t, std::vector<SceneMaterialRenderer *>>>
      textGroups;
  for (auto &object : objects_) {
    const auto resolved = resources_.resolve(object->owner());
    bool hiddenByLeaf = false;
    for (const WorldNode* ancestor = resolved.worldNode ? resources_.world().parent(*resolved.worldNode) : nullptr;
         ancestor; ancestor = resources_.world().parent(*ancestor))
      if (ancestor->type == 1 && (ancestor->flags & 0x40u)) { hiddenByLeaf = true; break; }
    // FUN_00511180 stops at WORLDTREELEAF. Cached material records survive
    // the hidden subtree cleanup (00511e60), but must not draw its descendants.
    if (hiddenByLeaf) continue;
    const auto font = object->textMaterialId();
    if (!font) {
      const auto opaque = object->opaque(error, requireCached);
      if (!opaque)
        return false;
      (*opaque ? opaqueObjects : rectangles).push_back(object.get());
      continue;
    }
    auto group =
        std::find_if(textGroups.begin(), textGroups.end(),
                     [&](const auto &item) { return item.first == *font; });
    if (group == textGroups.end()) {
      textGroups.push_back({*font, {}});
      group = std::prev(textGroups.end());
    }
    group->second.push_back(object.get());
  }
  bool hasSpatial = false;
  for (auto *object : opaqueObjects) {
    const auto spatial =
        object->spatial(view, projection, error, requireCached);
    if (!spatial)
      return false;
    hasSpatial = hasSpatial || *spatial;
  }
  if (hasSpatial) {
    // Fog is not applied in the composited spatial pass (a bounded visual
    // gap until the raster3d scene pass owns the world); transparent
    // rectangles already composite after the spatial upload below.
    int width = 0, height = 0;
    if (!renderer_.outputSize(width, height, error))
      return false;
    if (width <= 0 || height <= 0) {
      error = "invalid framebuffer size";
      return false;
    }
    const auto w = static_cast<uint32_t>(width),
               h = static_cast<uint32_t>(height);
    if ((raster_.width() != w || raster_.height() != h) &&
        !raster_.resize(w, h, error))
      return false;
    raster_.clear(0, 0, 0, 0);
    for (auto &item : spatialTextures_)
      item.second.used = false;
    for (auto *object : opaqueObjects)
      if (!object->drawSpatial(raster_, spatialTextures_, view, projection,
                               viewport, error, requireCached))
        return false;
    for (auto it = spatialTextures_.begin(); it != spatialTextures_.end();) {
      if (!it->second.used)
        it = spatialTextures_.erase(it);
      else
        ++it;
    }
    if (spatialUpload_ &&
        (spatialUploadWidth_ != w || spatialUploadHeight_ != h)) {
      const auto old = *spatialUpload_;
      spatialUpload_.reset();
      if (!renderer_.releaseTexture(old, error))
        return false;
    }
    if (!spatialUpload_) {
      spatialUpload_ = renderer_.texture(w, h, raster_.pixels().data(),
                                         raster_.pixels().size(), error);
      if (!spatialUpload_)
        return false;
      spatialUploadWidth_ = w;
      spatialUploadHeight_ = h;
    } else if (!renderer_.updateTexture(*spatialUpload_, w, h,
                                        raster_.pixels().data(),
                                        raster_.pixels().size(), error)) {
      return false;
    }
    if (!renderer_.draw(
            *spatialUpload_,
            {0, 0, static_cast<float>(width), static_cast<float>(height)},
            error))
      return false;
  } else {
    for (auto *object : opaqueObjects)
      if (!object->draw(view, projection, viewport, error, requireCached, fog))
        return false;
  }
  if (rectangles.size() <= 1) {
    for (auto *object : rectangles)
      if (!object->draw(view, projection, viewport, error, requireCached, fog))
        return false;
  } else {
    const auto cameraWorld = cameraViewMatrix(view);
    if (!cameraWorld) {
      error = "invalid camera view";
      return false;
    }
    SceneVisibleQueue order;
    for (auto *object : rectangles) {
      const auto depth =
          object->transparentDepth(*cameraWorld, error, requireCached);
      if (!depth || !order.push(*object->owner(), *depth))
        return false;
    }
    order.drain(0, true, [&](LinkOwner &owner) -> uint32_t {
      if (!ok)
        return 0;
      const auto object = std::find_if(
          rectangles.begin(), rectangles.end(),
          [&](const auto *item) { return item->owner() == &owner; });
      if (object == rectangles.end() ||
          !(*object)->draw(view, projection, viewport, error, requireCached,
                           fog))
        ok = false;
      return 0;
    });
    if (!ok)
      return false;
  }
  for (auto &group : textGroups)
    for (auto *object : group.second)
      if (!object->draw(view, projection, viewport, error, requireCached))
        return false;
  error.clear();
  return true;
}

bool SceneCameraRenderer::remove(LinkOwner &node) {
  worldRaster_.remove(node);
  const bool removed = candidates_.remove(node);
  lights_.erase(std::remove_if(lights_.begin(), lights_.end(),
                               [&](const auto &light) {
                                 return light->instance.owner == &node;
                               }),
                lights_.end());
  for (size_t i = 0; i < objects_.size();) {
    if (objects_[i]->owner() == &node)
      eraseObject(i);
    else
      ++i;
  }
  return removed;
}

void SceneCameraRenderer::eraseObject(size_t index) {
  const auto group = objects_[index]->textMaterialId();
  if (group) {
    for (size_t i = objects_.size(); i-- > index + 1;) {
      if (objects_[i]->textMaterialId() == group) {
        std::swap(objects_[index], objects_[i]);
        index = i;
        break;
      }
    }
  }
  objects_.erase(objects_.begin() + static_cast<std::ptrdiff_t>(index));
}
