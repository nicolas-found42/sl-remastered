#include "camera_traversal.hpp"
#include "natives/game_type.hpp"
#include "object_runtime.hpp"
#include "resource_world.hpp"
#include "vm.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

std::optional<CameraTraversalParams>
cameraTraversalParams(const CameraResource &camera,
                      const CameraViewState &state,
                      const std::array<float, 16> &worldMatrix,
                      float worldScale, float threshold, bool frustumChecks) {
  if (!std::isfinite(worldScale) || !std::isfinite(threshold))
    return std::nullopt;
  ResourceBounds bound;
  if (!sceneCameraBound(state.fields.farMetric, worldMatrix, bound))
    return std::nullopt;
  const auto planes = cameraFrustumPlanes(state.fields, worldMatrix);
  if (!planes)
    return std::nullopt;
  const float radians = state.fields.fieldOfView * 0x1.1df46ap-6f;
  const double halfAngle = static_cast<double>(radians) * 0.5;
  const float factor =
      static_cast<float>(std::cos(halfAngle) / std::sin(halfAngle));
  if (!std::isfinite(factor))
    return std::nullopt;
  CameraTraversalParams params{};
  for (size_t i = 0; i < 3; ++i)
    params.cameraPosition[i] = worldMatrix[12 + i] * 0.1f;
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
  for (auto &record : records_)
    setLinkOwner(record->owner, nullptr);
}

bool CameraType1Records::beginVisit(LinkOwner &owner, bool isRoot,
                                    bool rootDirty, bool fullyInside) {
  records_.erase(std::remove_if(records_.begin(), records_.end(),
                                [](const auto &r) { return !r->owner.owner; }),
                 records_.end());
  for (const auto &record : records_)
    if (record->owner.owner == &owner) {
      if (fullyInside && isRoot && !rootDirty && (record->flags & 1u))
        return true;
      record->flags =
          fullyInside ? (record->flags & ~6u) | 1u : (record->flags & ~3u) | 4u;
      return false;
    }
  auto record = std::make_unique<Record>();
  setLinkOwner(record->owner, &owner);
  if (!fullyInside)
    record->flags = 4u;
  records_.push_back(std::move(record));
  return false;
}

void CameraType1Records::finishVisit(LinkOwner &owner, bool allComplete) {
  for (auto &record : records_)
    if (record->owner.owner == &owner && !allComplete)
      record->flags = (record->flags & ~3u) | 4u;
}

bool CameraType1Records::hasRecord(const LinkOwner &owner) const {
  return std::any_of(records_.begin(), records_.end(), [&](const auto &record) {
    return record->owner.owner == &owner;
  });
}

bool CameraType1Records::removeRecord(const LinkOwner &owner) {
  const auto it =
      std::find_if(records_.begin(), records_.end(),
                   [&](const auto &r) { return r->owner.owner == &owner; });
  if (it == records_.end())
    return false;
  setLinkOwner((*it)->owner, nullptr);
  if (it + 1 != records_.end())
    *it = std::move(records_.back());
  records_.pop_back();
  return true;
}

template <typename Node>
bool updateType1Visibility(Node &node, uint32_t frame, float input) {
  if (node.type != 1 || !std::isfinite(input) ||
      !std::isfinite(node.visibilityGain) ||
      !std::isfinite(node.visibilityBias))
    return false;
  const float value = input * node.visibilityGain + node.visibilityBias;
  if (!std::isfinite(value) ||
      (frame <= node.stamp78 &&
       (!node.field6c || !std::isfinite(*node.field6c))) ||
      (frame <= node.stamp7c &&
       (!node.field70 || !std::isfinite(*node.field70))))
    return false;
  if (frame > node.stamp78 || value > *node.field6c)
    node.field6c = value;
  if (frame > node.stamp7c || value > *node.field70)
    node.field70 = value;
  node.stamp78 = frame;
  node.stamp7c = frame;
  if (input == 0.0f)
    node.loadPriority = 0.0f;
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
                              ResourceWorld *resources, VmArena *arena,
                              const std::vector<VmStaticFields> *staticFields) {
  error.clear();
  const std::optional<int32_t> gameTime =
      arena && arena->gameState
          ? std::optional<int32_t>{arena->gameState->gameTimeOfDay}
          : std::nullopt;
  if (!params.planeTest || !params.frustumChecks) {
    error = "camera traversal: plane-test inputs unavailable";
    return false;
  }
  // FUN_00510fc0: a non-zero plane-test mode (stock modes 1 and 2 are
  // indistinguishable — a plain == 0 test) skips the per-frame plane
  // dilation, resets the root camera instance bounds to {mode 0, extents
  // 100}, forces a full re-walk and forces the five-plane frustum test on.
  // Mode 0 instead dilates the six plane distance terms by 2.0 (float at VA
  // 0x5f0ab8) every frame; that widening is not modeled here (the planes are
  // rebuilt per call), a bounded divergence in streaming aggressiveness.
  const bool planeTest = *params.planeTest != 0;
  const bool frustumChecks = planeTest || *params.frustumChecks;
  if (frustumChecks && !params.frustumPlanes) {
    error = "camera traversal: frustum planes unavailable";
    return false;
  }
  float score = 0.0f; // 00510fc0 clears DAT_0065c2d0 before traversal.
  auto *rootNode = world.nodeFor(&root);
  auto *rootPack = resources ? resources->resolve(&root).packNode : nullptr;
  // 00510fc0 also resets the camera render instance node's bounds block to
  // {mode 0, extents 100}; that node is not part of this walk, so the reset
  // happens in CameraFrame::draw on the camera instance itself.
  bool rootDirty = (rootNode && (rootNode->flags & 0x40000u)) ||
                   (rootPack && (rootPack->flags & 0x40000u));
  if (planeTest)
    rootDirty = true;
  if (rootNode)
    rootNode->flags &= ~0x40000u;
  if (rootPack)
    rootPack->flags &= ~0x40000u;
  const auto inside = [&](const ResourceBounds &bounds, bool fullyInside) {
    if (fullyInside)
      return bounds.mode == 1 || bounds.mode == 2;
    uint32_t relation = bounds.mode == 2 ? 0xeu : 0u;
    if (bounds.mode == 1 && params.cameraBound)
      relation = sceneBoxRelation(
          *params.cameraBound,
          {bounds.values[0], bounds.values[1], bounds.values[2]},
          {bounds.values[3], bounds.values[4], bounds.values[5]});
    if (relation == 0 || (relation & 1u))
      return false;
    if (frustumChecks) {
      for (const auto &plane : *params.frustumPlanes)
        if (sceneBoundPlaneRelation(bounds, plane) != 4)
          return false;
      return true;
    }
    return (relation & 8u) != 0;
  };
  // Outside type-1 nodes require FUN_00511e60 cleanup, not just pruning.
  const auto outside = [&](const ResourceBounds &bounds, bool fullyInside) {
    if (fullyInside || bounds.mode == 2 || !params.cameraBound)
      return false;
    if (sceneBoxRelation(
            *params.cameraBound,
            {bounds.values[0], bounds.values[1], bounds.values[2]},
            {bounds.values[3], bounds.values[4], bounds.values[5]}) == 1)
      return true;
    if (frustumChecks)
      for (const auto &plane : *params.frustumPlanes)
        if (sceneBoundPlaneRelation(bounds, plane) == 1)
          return true;
    return false;
  };
  // FUN_00511e60 visits loaded descendants after resetting visibility. This
  // bounded path preflights the whole record-free subtree before modifying it.
  const auto cleanupFreshHidden = [&](LinkOwner &hidden) {
    if (!type1Records) {
      error = "camera cleanup: records unavailable";
      return false;
    }
    struct Entry {
      WorldNode *runtime;
      ResourceNode *mounted;
    };
    std::vector<Entry> nodes;
    std::vector<LinkOwner *> recorded;
    const auto frame = world.frameCounter();
    const auto validType1 = [&](const auto &node) {
      return std::isfinite(node.visibilityGain) &&
             std::isfinite(node.visibilityBias) &&
             (frame > node.stamp78 ||
              (node.field6c && std::isfinite(*node.field6c))) &&
             (frame > node.stamp7c ||
              (node.field70 && std::isfinite(*node.field70)));
    };
    std::function<bool(LinkOwner &, size_t)> collect = [&](LinkOwner &owner,
                                                           size_t depth) {
      auto *node = world.nodeFor(&owner);
      auto *pack = resources ? resources->resolve(&owner).packNode : nullptr;
      const char *why = nullptr;
      if (depth > 256)
        why = "tree deeper than 256";
      else if (!node && (!pack || pack->type != 1))
        why = "no runtime node and no type-1 pack node";
      else if (node && node->type != 1 && node->type != 2 && node->type != 3)
        why = "runtime node type is not 1, 2 or 3";
      if (why) {
        error = std::string("camera cleanup: unsupported node (") + why +
                "; depth=" + std::to_string(depth) + ", runtime type=" +
                (node ? std::to_string(node->type) : std::string("none")) +
                ", pack type=" +
                (pack ? std::to_string(pack->type) : std::string("none")) + ")";
        return false;
      }
      if (node) {
        if (node->type == 1 && !validType1(*node)) {
          error = "camera cleanup: unknown type-1 visibility";
          return false;
        }
        if (node->type == 2 && frame <= node->stamp7c &&
            (!node->field6c || !std::isfinite(*node->field6c))) {
          error = "camera cleanup: unknown type-2 visibility";
          return false;
        }
        if (node->type == 3) {
          SceneVisibilityFields fields{node->field6c, node->field70,
                                       node->stamp78, node->stamp7c};
          if (!sceneUpdateType3Visibility(fields, frame, 0,
                                          node->visibilityBias,
                                          node->visibilityGain)) {
            error = "camera cleanup: unknown type-3 visibility";
            return false;
          }
        }
      } else if (!validType1(*pack)) {
        error = "camera cleanup: unknown type-1 visibility";
        return false;
      }
      if (type1Records->hasRecord(owner))
        recorded.push_back(&owner);
      nodes.push_back({node, node ? nullptr : pack});
      const bool loaded = node ? node->loaded : pack->loaded();
      if (!loaded)
        return true;
      const auto children =
          world.childOwners(node ? node->handle : pack->handle);
      if (!children) {
        error = "camera cleanup: invalid tree";
        return false;
      }
      for (auto *child : *children)
        if (!child || !collect(*child, depth + 1))
          return false;
      return true;
    };
    if (!collect(hidden, 0))
      return false;
    // FUN_00511e60 destroys the camera's record for each visited node.
    for (auto *owner : recorded)
      type1Records->removeRecord(*owner);
    for (const auto &entry : nodes) {
      if (entry.mounted) {
        if (!updateType1Visibility(*entry.mounted, frame, 0)) {
          error = "camera cleanup: type-1 visibility failed";
          return false;
        }
        continue;
      }
      auto &node = *entry.runtime;
      if (node.type == 1) {
        if (!updateType1Visibility(node, frame, 0)) {
          error = "camera cleanup: type-1 visibility failed";
          return false;
        }
      } else if (node.type == 2) {
        if (frame > node.stamp7c || *node.field6c < 0)
          node.field6c = 0;
        node.field70 = 0;
        node.stamp7c = frame;
        node.stamp78 = frame;
        if (*node.field6c < 1)
          node.field6c = 1;
      } else {
        if (!sceneUpdateType3NodeVisibility(node, frame, 0)) {
          error = "camera cleanup: type-3 visibility failed";
          return false;
        }
        const float value =
            std::clamp(node.visibilityGain + node.visibilityBias, 0.0f, 1.0f);
        if (*node.field6c < value)
          node.field6c = value;
      }
    }
    // Reconcile only after walking the captured subtree: LOD changes can
    // destroy render children referenced by that snapshot.
    for (const auto &entry : nodes) {
      if (entry.mounted && entry.mounted->objectRuntime &&
          !object::updateObjectLod(*resources, *entry.mounted,
                                   *entry.mounted->field70, error, gameTime)) {
        error = "camera cleanup: object LOD: " + error;
        return false;
      }
    }
    return true;
  };
  std::function<std::optional<bool>(LinkOwner &, bool, size_t)> visit =
      [&](LinkOwner &owner, bool fullyInside,
          size_t depth) -> std::optional<bool> {
    if (depth > 256) {
      error = "camera traversal: depth limit";
      return std::nullopt;
    }
    auto *node = world.nodeFor(&owner);
    auto *pack = resources ? resources->resolve(&owner).packNode : nullptr;
    if (!node && pack && pack->type == 1) {
      if (!pack->bounds) {
        error = "camera traversal: mounted type-1 bounds unavailable";
        return std::nullopt;
      }
      const ResourceBounds &bounds = *pack->bounds;
      const auto input = sceneVisibilityInput(
          bounds, params.cameraPosition, params.cameraFactor, params.lodBias,
          params.lodAmplitude, params.worldScale);
      if (!input ||
          !updateType1Visibility(*pack, world.frameCounter(), *input)) {
        error = "camera traversal: mounted visibility update unsupported";
        return std::nullopt;
      }
      if (!fullyInside && bounds.mode != 2 && pack->field70)
        score = 1.0f - *pack->field70;
      bool partial = !fullyInside && bounds.mode == 2 && frustumChecks;
      if (!pack->loaded()) {
        // 00511180 requests resource work through 005447d0. Stock can
        // defer it; these bounded factories instead construct supported
        // payloads synchronously before the host treats them as loaded.
        if (!type1Records) {
          error =
              "camera traversal: unloaded mounted type-1 records unavailable";
          return std::nullopt;
        }
        if (pack->field6c && *pack->field6c < params.threshold) {
          if (!cleanupFreshHidden(*pack))
            return std::nullopt;
          return false;
        }
        if (!fullyInside && bounds.mode != 1 && bounds.mode != 2) {
          error = "camera traversal: mounted type-1 bounds unsupported";
          return std::nullopt;
        }
        if (!fullyInside && bounds.mode == 1 && !params.cameraBound) {
          error = "camera traversal: mounted type-1 camera bound unavailable";
          return std::nullopt;
        }
        if (outside(bounds, fullyInside)) {
          if (!cleanupFreshHidden(*pack))
            return std::nullopt;
          return true;
        }
        std::string loadError;
        if (!resources->loadEmptyGameParent(*pack, loadError) &&
            !resources->loadPassiveSpatialGame(*pack, loadError)) {
          GameResourcePayload payload;
          std::string inspectError;
          if (resources->inspectGamePayload(*pack, payload, inspectError) &&
              payload.runtimeGameType && *payload.runtimeGameType != 0) {
            if (!arena) {
              loadError = "factory requires VM context";
            } else if (const auto failure = loadGroundGameResource(
                           *arena, *pack, staticFields)) {
              loadError = *failure;
            } else if (pack->loaded()) {
              loadError.clear();
            } else {
              loadError = "factory returned without loading resource";
            }
          }
          if (!loadError.empty()) {
            error = "camera traversal: mounted type-1 " +
                    std::to_string(pack->value) + " (" + pack->name +
                    ") load unsupported: " + loadError;
            return std::nullopt;
          }
        }
        if (!pack->loaded()) {
          error =
              "camera traversal: mounted type-1 loader did not load resource";
          return std::nullopt;
        }
      }
      if (pack->objectRuntime) {
        const int previousLod = pack->objectRuntime->lod;
        const size_t previousRenderCount = pack->objectRuntime->renders.size();
        if (!object::updateObjectLod(*resources, *pack, *pack->field70, error,
                                     gameTime)) {
          error = "camera traversal: object LOD: " + error;
          return std::nullopt;
        }
        if (previousLod != pack->objectRuntime->lod ||
            previousRenderCount != pack->objectRuntime->renders.size())
          rootDirty = true;
      }
      if (pack->field6c && *pack->field6c < params.threshold) {
        if (!cleanupFreshHidden(*pack))
          return std::nullopt;
        return false;
      }
      if (!type1Records || !pack->polygonCount || !pack->field6c) {
        const char *reason = !type1Records         ? "records unavailable"
                             : !pack->polygonCount ? "unknown polygon count"
                                                   : "unknown visibility";
        error = std::string{"camera traversal: mounted type-1 0x"} +
                [](uint32_t v) {
                  char b[9];
                  std::snprintf(b, sizeof b, "%08x", static_cast<unsigned>(v));
                  return std::string{b};
                }(pack->value) +
                ": " + reason;
        return std::nullopt;
      }
      if (!fullyInside && bounds.mode != 1 && bounds.mode != 2) {
        error = "camera traversal: mounted type-1 bounds unsupported";
        return std::nullopt;
      }
      if (!fullyInside && bounds.mode == 1 && !params.cameraBound) {
        error = "camera traversal: mounted type-1 camera bound unavailable";
        return std::nullopt;
      }
      if (!partial && !inside(bounds, fullyInside)) {
        if (outside(bounds, fullyInside)) {
          if (!cleanupFreshHidden(*pack))
            return std::nullopt;
          return true;
        }
        // FUN_00511180 classifies each child on the partial walk.
        partial = true;
      }
      // 00511180 queues nonempty POLY nodes instead of traversing their
      // children. The candidate consumer resolves the sector ground material.
      if (*pack->polygonCount != 0) {
        const auto result = candidates.submitGround(
            *resources, *pack, params.threshold, score, fullyInside);
        if (result == SceneCandidateResult::Unsupported) {
          error =
              "camera traversal: mounted type-1 ground candidate unsupported";
          return std::nullopt;
        }
        if (result == SceneCandidateResult::BelowThreshold)
          return false;
        return result == SceneCandidateResult::AlreadyPresent;
      }
      if (pack->flags & (0x40u | 0x100u)) {
        error = "camera traversal: mounted type-1 node flags unsupported";
        return std::nullopt;
      }
      if (type1Records->beginVisit(*pack, depth == 0, rootDirty, !partial))
        return true;
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
    if (node->type == 2)
      return false;
    if (node->type != 1 && node->type != 3) {
      error = "camera traversal: node type unsupported";
      return std::nullopt;
    }
    const auto input = sceneVisibilityInput(
        node->bounds, params.cameraPosition, params.cameraFactor,
        params.lodBias, params.lodAmplitude, params.worldScale);
    const bool updated =
        input &&
        (node->type == 1
             ? updateCameraType1Visibility(*node, world.frameCounter(), *input)
             : sceneUpdateType3NodeVisibility(*node, world.frameCounter(),
                                              *input));
    if (!updated) {
      error = "camera traversal: visibility update unsupported";
      return std::nullopt;
    }
    if (!fullyInside && node->bounds.mode != 2 && node->field70)
      score = 1.0f - *node->field70;
    if (node->type == 1) {
      bool partial = !fullyInside && node->bounds.mode == 2 && frustumChecks;
      if ((node->flags & 0x40u) && node->loaded && node->polygonCount &&
          *node->polygonCount == 0 &&
          (partial || inside(node->bounds, fullyInside)) && node->field6c &&
          *node->field6c >= params.threshold) {
        if (!cleanupFreshHidden(*node))
          return std::nullopt;
        return true;
      }
      if (!type1Records || !node->loaded || !node->polygonCount ||
          *node->polygonCount != 0 || (node->flags & (0x40u | 0x100u)) ||
          !node->field6c || *node->field6c < params.threshold) {
        error = "camera traversal: type-1 " + node->name + ": ";
        if (!type1Records)
          error += "records unavailable";
        else if (!node->loaded)
          error += "not loaded";
        else if (!node->polygonCount)
          error += "polygon count unknown";
        else if (*node->polygonCount != 0)
          error += "nonempty polygons";
        else if (node->flags & (0x40u | 0x100u)) {
          const auto children = world.childOwners(node->handle);
          error += "node flags unsupported (" + std::to_string(node->flags) +
                   ", children " +
                   (children ? std::to_string(children->size())
                             : std::string("unknown")) +
                   ")";
          if (children && resources) {
            std::array<unsigned, 22> counts{};
            for (const auto *child : *children) {
              const auto resolved = resources->resolve(child);
              if (resolved && resolved.type < counts.size())
                ++counts[resolved.type];
            }
            for (size_t type = 0; type < counts.size(); ++type)
              if (counts[type])
                error += " type" + std::to_string(type) + "=" +
                         std::to_string(counts[type]);
          }
        } else if (!node->field6c)
          error += "visibility unknown";
        else
          error += "below visibility threshold";
        return std::nullopt;
      }
      if (!fullyInside && node->bounds.mode != 1 && node->bounds.mode != 2) {
        error = "camera traversal: type-1 bounds unsupported";
        return std::nullopt;
      }
      if (!fullyInside && node->bounds.mode == 1 && !params.cameraBound) {
        error = "camera traversal: type-1 camera bound unavailable";
        return std::nullopt;
      }
      if (!partial && !inside(node->bounds, fullyInside)) {
        if (outside(node->bounds, fullyInside)) {
          if (!cleanupFreshHidden(*node))
            return std::nullopt;
          return true;
        }
        // FUN_00511180 classifies each child on the partial walk.
        partial = true;
      }
      if (type1Records->beginVisit(*node, depth == 0, rootDirty, !partial))
        return true;
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
    const auto result =
        candidates.submit(*node, params.threshold, score, fullyInside);
    if (result == SceneCandidateResult::Unsupported) {
      error = "camera traversal: type-3 " + node->name +
              ": candidate admission unsupported (mode " +
              std::to_string(node->bounds.mode) + ", field6c " +
              (node->field6c ? std::to_string(*node->field6c)
                             : std::string("unknown")) +
              ", field70 " +
              (node->field70 ? std::to_string(*node->field70)
                             : std::string("unknown")) +
              ")";
      return std::nullopt;
    }
    // BelowThreshold means the node is not admitted this frame; the walk
    // continues without it (stock prunes it from this frame's queue).
    if (result == SceneCandidateResult::BelowThreshold)
      return false;
    // Stock queues type-3 nodes and returns without visiting their children.
    return result == SceneCandidateResult::AlreadyPresent;
  };
  return visit(root, false, 0).has_value();
}
