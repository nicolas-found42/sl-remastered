#include "camera_traversal.hpp"
#include "resource_world.hpp"
#include <algorithm>
#include <cmath>

std::optional<CameraTraversalParams>
cameraTraversalParams(const CameraResource &camera, const CameraViewState &state,
                      const std::array<float, 16> &worldMatrix,
                      float worldScale, float threshold, bool frustumChecks) {
  if (!std::isfinite(worldScale) || !std::isfinite(threshold)) return std::nullopt;
  ResourceBounds bound;
  if (!sceneCameraBound(state.fields.farMetric, worldMatrix, bound)) return std::nullopt;
  const auto planes = cameraFrustumPlanes(state.fields, worldMatrix);
  if (!planes) return std::nullopt;
  const float radians = state.fields.fieldOfView * 0x1.1df46ap-6f;
  const double halfAngle = static_cast<double>(radians) * 0.5;
  const float factor = static_cast<float>(std::cos(halfAngle) / std::sin(halfAngle));
  if (!std::isfinite(factor)) return std::nullopt;
  CameraTraversalParams params{};
  for (size_t i = 0; i < 3; ++i) params.cameraPosition[i] = worldMatrix[12 + i] * 0.1f;
  params.cameraFactor = factor;
  params.lodBias = state.fields.lodBias;
  params.lodAmplitude = state.fields.lodAmplitude;
  params.worldScale = worldScale;
  params.threshold = threshold;
  params.planeTest = camera.planeTest;
  params.frustumChecks = frustumChecks;
  params.cameraBound = bound;
  params.frustumPlanes = *planes;
  return params;
}

CameraType1Records::~CameraType1Records() {
  for (auto &record : records_) setLinkOwner(record->owner, nullptr);
}

bool CameraType1Records::beginVisit(LinkOwner &owner, bool isRoot, bool rootDirty, bool fullyInside) {
  records_.erase(std::remove_if(records_.begin(), records_.end(), [](const auto &r) {
    return !r->owner.owner;
  }), records_.end());
  for (const auto &record : records_)
    if (record->owner.owner == &owner) {
      if (fullyInside && isRoot && !rootDirty && (record->flags & 1u)) return true;
      record->flags = fullyInside ? (record->flags & ~6u) | 1u : (record->flags & ~3u) | 4u;
      return false;
    }
  auto record = std::make_unique<Record>();
  setLinkOwner(record->owner, &owner);
  if (!fullyInside) record->flags = 4u;
  records_.push_back(std::move(record));
  return false;
}

void CameraType1Records::finishVisit(LinkOwner &owner, bool allComplete) {
  for (auto &record : records_)
    if (record->owner.owner == &owner && !allComplete)
      record->flags = (record->flags & ~3u) | 4u;
}

bool CameraType1Records::hasRecord(const LinkOwner &owner) const {
  return std::any_of(records_.begin(), records_.end(), [&](const auto &record) { return record->owner.owner == &owner; });
}

template <typename Node>
bool updateType1Visibility(Node &node, uint32_t frame, float input) {
  if (node.type != 1 || !std::isfinite(input) ||
      !std::isfinite(node.visibilityGain) || !std::isfinite(node.visibilityBias))
    return false;
  const float value = input * node.visibilityGain + node.visibilityBias;
  if (!std::isfinite(value) ||
      (frame <= node.stamp78 && (!node.field6c || !std::isfinite(*node.field6c))) ||
      (frame <= node.stamp7c && (!node.field70 || !std::isfinite(*node.field70))))
    return false;
  if (frame > node.stamp78 || value > *node.field6c) node.field6c = value;
  if (frame > node.stamp7c || value > *node.field70) node.field70 = value;
  node.stamp78 = frame;
  node.stamp7c = frame;
  if (input == 0.0f) node.loadPriority = 0.0f;
  return true;
}

bool updateCameraType1Visibility(WorldNode &node, uint32_t frame, float input) {
  return updateType1Visibility(node, frame, input);
}

bool traverseCameraCandidates(GameWorld &world, LinkOwner &root,
                              SceneCameraCandidates &candidates,
                              const CameraTraversalParams &params,
                              std::string &error,
                              CameraType1Records *type1Records,
                              ResourceWorld *resources) {
  error.clear();
  if (!params.planeTest || *params.planeTest != 0 || !params.frustumChecks ||
      (*params.frustumChecks && !params.frustumPlanes)) {
    error = "camera traversal: plane-test root updates unsupported";
    return false;
  }
  float score = 0.0f; // 00510fc0 clears DAT_0065c2d0 before traversal.
  auto *rootNode = world.nodeFor(&root);
  auto *rootPack = resources ? resources->resolve(&root).packNode : nullptr;
  const bool rootDirty = (rootNode && (rootNode->flags & 0x40000u)) ||
                         (rootPack && (rootPack->flags & 0x40000u));
  if (rootNode) rootNode->flags &= ~0x40000u;
  if (rootPack) rootPack->flags &= ~0x40000u;
  const auto inside = [&](const ResourceBounds &bounds, bool fullyInside) {
    if (fullyInside) return bounds.mode == 1 || bounds.mode == 2;
    uint32_t relation = bounds.mode == 2 ? 0xeu : 0u;
    if (bounds.mode == 1 && params.cameraBound)
      relation = sceneBoxRelation(*params.cameraBound,
          {bounds.values[0], bounds.values[1], bounds.values[2]},
          {bounds.values[3], bounds.values[4], bounds.values[5]});
    if (relation == 0 || (relation & 1u)) return false;
    if (*params.frustumChecks) {
      for (const auto &plane : *params.frustumPlanes)
        if (sceneBoundPlaneRelation(bounds, plane) != 4) return false;
      return true;
    }
    return (relation & 8u) != 0;
  };
  const auto cleanupFreshHidden = [&](WorldNode &hidden) {
    if (!type1Records) { error = "camera cleanup: records unavailable"; return false; }
    std::vector<WorldNode*> nodes;
    std::function<bool(WorldNode&, size_t)> collect = [&](WorldNode &node, size_t depth) {
      if (depth > 256 || (node.type != 1 && node.type != 2 && node.type != 3) ||
          type1Records->hasRecord(node) || candidates.hasRecord(node)) {
        error = "camera cleanup: existing record or unsupported node"; return false;
      }
      const auto frame = world.frameCounter();
      if (node.type == 1) {
        if (!std::isfinite(node.visibilityGain) || !std::isfinite(node.visibilityBias) ||
            (frame <= node.stamp78 && (!node.field6c || !std::isfinite(*node.field6c))) ||
            (frame <= node.stamp7c && (!node.field70 || !std::isfinite(*node.field70)))) {
          error = "camera cleanup: unknown type-1 visibility"; return false;
        }
      } else if (node.type == 2) {
        if (frame <= node.stamp7c && (!node.field6c || !std::isfinite(*node.field6c))) {
          error = "camera cleanup: unknown type-2 visibility"; return false;
        }
      } else {
        SceneVisibilityFields fields{node.field6c, node.field70, node.stamp78, node.stamp7c};
        if (!sceneUpdateType3Visibility(fields, frame, 0, node.visibilityBias, node.visibilityGain)) {
          error = "camera cleanup: unknown type-3 visibility"; return false;
        }
      }
      nodes.push_back(&node);
      if (!node.loaded) return true;
      const auto children = world.childOwners(node.handle);
      if (!children) { error = "camera cleanup: invalid tree"; return false; }
      for (auto* owner : *children) {
        auto* child = owner ? world.nodeFor(owner) : nullptr;
        if (!child || !collect(*child, depth + 1)) return false;
      }
      return true;
    };
    if (!collect(hidden, 0)) return false;
    const auto frame = world.frameCounter();
    for (auto* node : nodes) {
      if (node->type == 1) {
        if (!updateType1Visibility(*node, frame, 0)) { error = "camera cleanup: type-1 visibility failed"; return false; }
      } else if (node->type == 2) {
        if (frame > node->stamp7c || *node->field6c < 0) node->field6c = 0;
        node->field70 = 0; node->stamp7c = frame; node->stamp78 = frame;
        if (*node->field6c < 1) node->field6c = 1;
      } else {
        if (!sceneUpdateType3NodeVisibility(*node, frame, 0)) { error = "camera cleanup: type-3 visibility failed"; return false; }
        const float value = std::clamp(node->visibilityGain + node->visibilityBias, 0.0f, 1.0f);
        if (*node->field6c < value) node->field6c = value;
      }
    }
    return true;
  };
  std::function<std::optional<bool>(LinkOwner &, bool, size_t)> visit =
      [&](LinkOwner &owner, bool fullyInside, size_t depth) -> std::optional<bool> {
    if (depth > 256) { error = "camera traversal: depth limit"; return std::nullopt; }
    auto *node = world.nodeFor(&owner);
    auto *pack = resources ? resources->resolve(&owner).packNode : nullptr;
    if (!node && pack && pack->type == 1) {
      const ResourceBounds bounds = pack->bounds.value_or(ResourceBounds{});
      const auto input = sceneVisibilityInput(bounds, params.cameraPosition,
          params.cameraFactor, params.lodBias, params.lodAmplitude, params.worldScale);
      if (!input || !updateType1Visibility(*pack, world.frameCounter(), *input)) {
        error = "camera traversal: mounted visibility update unsupported";
        return std::nullopt;
      }
      if (!fullyInside && bounds.mode != 2 && pack->field70)
        score = 1.0f - *pack->field70;
      const bool partial = !fullyInside && bounds.mode == 2 && *params.frustumChecks;
      if (!type1Records || !pack->loaded() || !pack->polygonCount ||
          *pack->polygonCount != 0 || (!partial && !inside(bounds, fullyInside)) ||
          (pack->flags & (0x40u | 0x100u)) || !pack->field6c ||
          *pack->field6c < params.threshold) {
        error = "camera traversal: mounted type-1 load, bounds or cleanup unsupported";
        return std::nullopt;
      }
      if (type1Records->beginVisit(*pack, depth == 0, rootDirty, !partial)) return true;
      const auto children = world.childOwners(pack->handle);
      if (!children) {
        type1Records->finishVisit(*pack, false);
        error = "camera traversal: invalid mounted tree";
        return std::nullopt;
      }
      bool allComplete = true;
      for (auto *child : *children) {
        if (!child) {
          type1Records->finishVisit(*pack, false);
          error = "camera traversal: invalid child";
          return std::nullopt;
        }
        const auto complete = visit(*child, !partial, depth + 1);
        if (!complete) {
          type1Records->finishVisit(*pack, false);
          return std::nullopt;
        }
        allComplete = allComplete && *complete;
      }
      type1Records->finishVisit(*pack, allComplete);
      return allComplete;
    }
    if (!node) {
      error = "camera traversal: mounted or root node unsupported";
      return std::nullopt;
    }
    if (node->type == 2) return false;
    if (node->type != 1 && node->type != 3) {
      error = "camera traversal: node type unsupported";
      return std::nullopt;
    }
    const auto input = sceneVisibilityInput(node->bounds, params.cameraPosition,
        params.cameraFactor, params.lodBias, params.lodAmplitude, params.worldScale);
    const bool updated = input && (node->type == 1
        ? updateCameraType1Visibility(*node, world.frameCounter(), *input)
        : sceneUpdateType3NodeVisibility(*node, world.frameCounter(), *input));
    if (!updated) {
      error = "camera traversal: visibility update unsupported";
      return std::nullopt;
    }
    if (!fullyInside && node->bounds.mode != 2 && node->field70)
      score = 1.0f - *node->field70;
    if (node->type == 1) {
      const bool partial = !fullyInside && node->bounds.mode == 2 && *params.frustumChecks;
      if ((node->flags & 0x40u) && node->loaded && node->polygonCount && *node->polygonCount == 0 &&
          (partial || inside(node->bounds, fullyInside)) && node->field6c && *node->field6c >= params.threshold) {
        if (!cleanupFreshHidden(*node)) return std::nullopt;
        return true;
      }
      if (!type1Records || !node->loaded || !node->polygonCount ||
          *node->polygonCount != 0 || (!partial && !inside(node->bounds, fullyInside)) ||
          (node->flags & (0x40u | 0x100u)) || !node->field6c ||
          *node->field6c < params.threshold) {
        error = "camera traversal: type-1 " + node->name + ": ";
        if (!type1Records) error += "records unavailable";
        else if (!node->loaded) error += "not loaded";
        else if (!node->polygonCount) error += "polygon count unknown";
        else if (*node->polygonCount != 0) error += "nonempty polygons";
        else if (!partial && !inside(node->bounds, fullyInside)) error += "bound classification unsupported (mode " + std::to_string(node->bounds.mode) + ")";
        else if (node->flags & (0x40u | 0x100u)) {
          const auto children = world.childOwners(node->handle);
          error += "node flags unsupported (" + std::to_string(node->flags) + ", children " +
                   (children ? std::to_string(children->size()) : std::string("unknown")) + ")";
          if (children && resources) {
            std::array<unsigned, 22> counts{};
            for (const auto* child : *children) {
              const auto resolved = resources->resolve(child);
              if (resolved && resolved.type < counts.size()) ++counts[resolved.type];
            }
            for (size_t type = 0; type < counts.size(); ++type)
              if (counts[type]) error += " type" + std::to_string(type) + "=" + std::to_string(counts[type]);
          }
        }
        else if (!node->field6c) error += "visibility unknown";
        else error += "below visibility threshold";
        return std::nullopt;
      }
      if (type1Records->beginVisit(*node, depth == 0, rootDirty, !partial)) return true;
      const auto children = world.childOwners(node->handle);
      if (!children) {
        type1Records->finishVisit(*node, false);
        error = "camera traversal: invalid tree";
        return std::nullopt;
      }
      bool allComplete = true;
      for (auto *child : *children) {
        if (!child) {
          type1Records->finishVisit(*node, false);
          error = "camera traversal: invalid child";
          return std::nullopt;
        }
        const auto complete = visit(*child, !partial, depth + 1);
        if (!complete) {
          type1Records->finishVisit(*node, false);
          return std::nullopt;
        }
        allComplete = allComplete && *complete;
      }
      type1Records->finishVisit(*node, allComplete);
      return allComplete;
    }
    const auto result = candidates.submit(*node, params.threshold, score, fullyInside);
    if (result == SceneCandidateResult::Unsupported ||
        result == SceneCandidateResult::BelowThreshold) {
      error = "camera traversal: candidate admission or cleanup unsupported";
      return std::nullopt;
    }
    // Stock queues type-3 nodes and returns without visiting their children.
    return result == SceneCandidateResult::AlreadyPresent;
  };
  return visit(root, false, 0).has_value();
}
