#include "resource_sound.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"
#include "scene_2d.hpp"

#include <algorithm>
#include <cmath>

bool ResourceWorld::queueSoundPrecache(ResourceNode &node, std::string &error) {
  if (resolve(&node).packNode != &node || node.type != 6 || !node.payloadSize ||
      node.loaded()) {
    error = "unsupported asynchronous sound request";
    return false;
  }
  if (node.flags & 0x2000000u) {
    for (const auto &pending : pendingPackLoads_)
      if (pending->owner.owner == &node) {
        error.clear();
        return true;
      }
    error = "sound has an unknown pending load";
    return false;
  }
  PackResource resource;
  if (!packs_.lookup(node.value, resource, error) || !resource.packBytes ||
      resource.entry.payloadOffset > resource.packBytes->size() ||
      resource.entry.payloadSize >
          resource.packBytes->size() - resource.entry.payloadOffset) {
    if (error.empty())
      error = "sound payload unavailable";
    return false;
  }
  auto pending = std::make_unique<PendingPackLoad>();
  setLinkOwner(pending->owner, &node);
  pendingPackLoads_.push_back(std::move(pending));
  node.flags |= 0x2000000u;
  error.clear();
  return true;
}

bool ResourceWorld::queueFontLoad(const ResolvedNode &node,
                                  std::string &error) {
  if (!node || node.type != 14 || !node.packNode || node.packNode->loaded() ||
      !node.packNode->payloadSize ||
      resolve(node.owner).packNode != node.packNode) {
    error = "unsupported asynchronous font request";
    return false;
  }
  auto &packed = *node.packNode;
  if (!std::isfinite(packed.visibilityBias) ||
      !std::isfinite(packed.visibilityGain)) {
    error = "font demand unavailable";
    return false;
  }
  const float demand =
      std::clamp(packed.visibilityBias + packed.visibilityGain, 0.0f, 1.0f);
  if (packed.stamp78 < world_.frameCounter() ||
      (packed.field6c && *packed.field6c < demand))
    packed.field6c = demand;
  else if (!packed.field6c) {
    error = "font demand unavailable";
    return false;
  }
  if (packed.flags & 0x2000000u) {
    for (const auto &pending : pendingPackLoads_)
      if (pending->owner.owner == node.owner) {
        error.clear();
        return true;
      }
    error = "font has an unknown pending load";
    return false;
  }
  PackResource resource;
  if (!packs_.lookup(node.id, resource, error) || !resource.packBytes ||
      resource.entry.payloadOffset > resource.packBytes->size() ||
      resource.entry.payloadSize >
          resource.packBytes->size() - resource.entry.payloadOffset) {
    if (error.empty())
      error = "font payload unavailable";
    return false;
  }
  auto pending = std::make_unique<PendingPackLoad>();
  setLinkOwner(pending->owner, node.owner);
  pendingPackLoads_.push_back(std::move(pending));
  packed.flags |= 0x2000000u;
  error.clear();
  return true;
}
void ResourceWorld::resetLoadingHistory(uint32_t loadRate) {
  loadingHistory_.fill(loadRate);
  loadingHistorySum_ = loadRate << 5;
}

bool ResourceWorld::completePendingPackLoads(uint32_t loadRate,
                                             std::string &error) {
  // Stock counts the entry that reaches the rate limit even though it is
  // deferred; a nonempty queue therefore contributes at least one.
  const uint32_t visited =
      pendingPackLoads_.empty()
          ? 0
          : static_cast<uint32_t>(std::min<size_t>(
                pendingPackLoads_.size(), std::max<uint32_t>(1, loadRate)));
  // 005378d0 increments its count before comparing with resource_loadrate.
  for (uint32_t completed = 1;
       completed < loadRate && !pendingPackLoads_.empty(); ++completed) {
    auto *owner = pendingPackLoads_.front()->owner.owner;
    const auto node = resolve(owner);
    if (!node || (node.type != 6 && node.type != 14) || !node.packNode ||
        !(node.packNode->flags & 0x2000000u)) {
      error = "pending resource owner unavailable";
      return false;
    }
    auto &packed = *node.packNode;
    PackResource resource;
    if (!packs_.lookup(node.id, resource, error) || !resource.packBytes)
      return false;
    if (node.type == 6) {
      auto sound = loadSoundSample(*resource.packBytes, resource.entry,
                                   packs_.root(), error);
      if (!sound)
        return false;
      packed.soundBufferBytes = sound->stockBufferBytes;
      runlog::event(runlog::EventKind::Resource, "sound 0x%08x precached",
                    static_cast<unsigned>(node.id));
      packed.sound = std::move(sound);
    } else {
      auto material = sceneMaterial(node);
      if (!material) {
        auto parsed = sceneLoadMaterial(*this, node.id);
        if (!parsed.material || parsed.flags != 4100 ||
            parsed.material->textureCount() != 1) {
          error = "unsupported pending font material: " + parsed.error;
          return false;
        }
        material = std::move(parsed.material);
        if (!bindSceneMaterial(node, material)) {
          error = "font material binding failed";
          return false;
        }
      }
      if (material->flags() != 4100 || material->textureCount() != 1) {
        error = "unsupported pending font material";
        return false;
      }
    }

    packed.stateWord = (packed.stateWord & ~uint32_t{0x50}) | 0x21;
    packed.loadPriority = 1.0f;
    world_.trackLoadState(*node.owner, node.type, true);
    if (packed.stamp74 != world_.frameCounter() && !packed.recentUseQueued)
      packed.stateWord = (packed.stateWord & ~uint32_t{0x60}) | 0x10;
    world_.noteRecentUse(*node.owner, node.type, packed.stamp74,
                         packed.recentUseQueued);
    packed.flags &= ~uint32_t{0x2000000};
    pendingPackLoads_.erase(pendingPackLoads_.begin());
  }
  loadingHistorySum_ += visited - loadingHistory_[loadingHistoryIndex_];
  loadingHistory_[loadingHistoryIndex_] = visited;
  loadingHistoryIndex_ = (loadingHistoryIndex_ + 1) & 31;
  const bool wasLoading = loading_;
  loading_ = static_cast<float>(loadingHistorySum_) * 0.03125f > 4.0f;
  if (loading_ != wasLoading)
    runlog::event(runlog::EventKind::Note,
                  "loading pack-queue flag %d->%d (visited=%u history_sum=%u "
                  "rate=%u queued=%zu)",
                  wasLoading, loading_, visited, loadingHistorySum_, loadRate,
                  pendingPackLoads_.size());
  error.clear();
  return true;
}
