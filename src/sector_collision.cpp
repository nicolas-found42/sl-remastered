#include "sector_collision.hpp"

#include "resource_world.hpp"

#include <algorithm>
#include <cmath>

namespace {
struct NodeState {
  LinkOwner *sector = nullptr;
  const ResourceBounds *bounds = nullptr;
  uint32_t flags = 0;
  uint32_t state = 0;
};

NodeState stateOf(const ResolvedNode &node) {
  if (node.packNode)
    return {node.packNode->sectorLink.owner,
            node.packNode->bounds ? &*node.packNode->bounds : nullptr,
            node.packNode->flags, node.packNode->stateWord};
  if (node.worldNode)
    return {node.worldNode->sectorLink.owner, &node.worldNode->bounds,
            node.worldNode->flags, node.worldNode->loaded ? 1u : 0u};
  return {};
}

bool validBounds(const ResourceBounds *bounds) {
  if (!bounds || bounds->mode != 1)
    return false;
  for (size_t axis : {size_t(0), size_t(2)}) {
    float center = bounds->values[axis];
    float extent = bounds->values[axis + 3];
    if (!std::isfinite(center) || !std::isfinite(extent) || extent < 0 ||
        !std::isfinite(center - extent) || !std::isfinite(center + extent))
      return false;
  }
  return true;
}

bool overlaps(const ResourceBounds &a, const ResourceBounds &b) {
  for (size_t axis : {size_t(0), size_t(2)})
    if (std::fabs(a.values[axis] - b.values[axis]) >
        a.values[axis + 3] + b.values[axis + 3])
      return false;
  return true;
}
} // namespace

SectorCollisionManager::Effect::Effect(EffectKind kind, LinkOwner *target)
    : kind(kind) {
  setLinkOwner(this->target, target);
}
SectorCollisionManager::Effect::~Effect() { setLinkOwner(target, nullptr); }
SectorCollisionManager::Record::~Record() { setLinkOwner(owner, nullptr); }

SectorCollisionManager::SectorCollisionManager(ResourceNode &city) {
  setLinkOwner(city_, &city);
  // 00463220 calls 004b3ee0(city, null), whose unloaded-owner path does not
  // require the city's inherited sector to equal the manager's city.
  auto record = std::make_unique<Record>();
  setLinkOwner(record->owner, &city);
  record->citySeed = true;
  records_.push_back(std::move(record));
}

SectorCollisionManager::~SectorCollisionManager() {
  records_.clear();
  setLinkOwner(city_, nullptr);
}

bool SectorCollisionManager::queue(LinkOwner &owner, ResourceWorld &resources,
                                   std::string &error) {
  error.clear();
  if (!city_.owner) {
    error = "collision manager city no longer exists";
    return false;
  }
  ResolvedNode node = resources.resolve(&owner);
  if (!node) {
    error = "collision manager owner is not in the resource world";
    return false;
  }
  if (node.type != 1 || stateOf(node).sector != city_.owner)
    return true;
  for (auto &record : records_) {
    if (record->owner.owner && record->owner.cachedValue == owner.value) {
      record->active = true;
      record->lastQueued = frame_;
      return true;
    }
  }
  auto record = std::make_unique<Record>();
  setLinkOwner(record->owner, &owner);
  record->active = true;
  record->lastQueued = frame_;
  records_.push_back(std::move(record));
  return true;
}

bool SectorCollisionManager::discover(LinkOwner &owner,
                                      ResourceWorld &resources,
                                      std::string &error) {
  error.clear();
  if (!city_.owner) {
    error = "collision manager city no longer exists";
    return false;
  }
  ResolvedNode node = resources.resolve(&owner);
  if (!node) {
    error = "collision manager owner is not in the resource world";
    return false;
  }
  NodeState state = stateOf(node);
  if (node.type != 1 || ((state.state & 1) && state.sector != city_.owner))
    return true;
  for (const auto &record : records_)
    if (record->owner.owner && record->owner.cachedValue == owner.value)
      return true;
  auto record = std::make_unique<Record>();
  setLinkOwner(record->owner, &owner);
  records_.push_back(std::move(record));
  return true;
}

bool SectorCollisionManager::process(ResourceWorld &resources,
                                     std::string &error) {
  error.clear();
  if (!city_.owner) {
    error = "collision manager city no longer exists";
    return false;
  }
  // Validate before changing the frame, so an unrepresented bound is not
  // silently interpreted as an empty broad phase.
  for (const auto &record : records_) {
    if (!record->owner.owner)
      continue;
    ResolvedNode node = resources.resolve(record->owner.owner);
    if (!node || node.type != 1) {
      error = "collision manager record owner is unavailable";
      return false;
    }
    NodeState state = stateOf(node);
    if (!record->citySeed && state.sector != city_.owner &&
        (record->active || (state.state & 1)))
      continue;
    if (!validBounds(state.bounds)) {
      error = "collision manager record bounds unsupported";
      return false;
    }
  }
  effects_.clear();
  // 004b4250 scans beyond each moving endpoint, requesting nearby unloaded
  // resources. Neighbors crossed by the endpoints are reconciled below using
  // the new X/Z bounds, which also removes edges that no longer overlap.
  for (const auto &source : records_) {
    if (!source->active || !source->owner.owner || !source->hasBounds)
      continue;
    NodeState a = stateOf(resources.resolve(source->owner.owner));
    if (!validBounds(a.bounds))
      continue;
    for (size_t axis : {size_t(0), size_t(2)}) {
      for (int side : {-1, 1}) {
        float oldEnd = source->bounds.values[axis] +
                       side * source->bounds.values[axis + 3];
        float newEnd =
            a.bounds->values[axis] + side * a.bounds->values[axis + 3];
        if (oldEnd == newEnd)
          continue;
        float limit = newEnd + (newEnd - oldEnd) * 5.0f +
                      (newEnd > oldEnd ? 5.0f : -5.0f);
        for (const auto &target : records_) {
          if (source == target || !target->owner.owner)
            continue;
          NodeState b = stateOf(resources.resolve(target->owner.owner));
          if ((b.state & 1) || !validBounds(b.bounds))
            continue;
          const ResourceBounds &targetBounds =
              target->hasBounds ? target->bounds : *b.bounds;
          for (int targetSide : {-1, 1}) {
            float endpoint = targetBounds.values[axis] +
                             targetSide * targetBounds.values[axis + 3];
            if ((newEnd > oldEnd && endpoint >= newEnd && endpoint < limit) ||
                (newEnd < oldEnd && endpoint <= newEnd && endpoint > limit))
              effects_.push_back(std::make_unique<Effect>(EffectKind::Load,
                                                          target->owner.owner));
          }
        }
      }
    }
  }
  ++frame_;
  records_.erase(
      std::remove_if(
          records_.begin(), records_.end(),
          [&](const std::unique_ptr<Record> &record) {
            if (!record->owner.owner)
              return true;
            NodeState state = stateOf(resources.resolve(record->owner.owner));
            if (!record->citySeed && state.sector != city_.owner &&
                (record->active || (state.state & 1)))
              return true;
            if (record->active && frame_ - 1 > record->lastQueued + 10)
              record->active = false;
            // Static records without incoming links are retained for traversal;
            // the city seed is created as a static record by 00463220.
            return false;
          }),
      records_.end());
  for (auto &record : records_) {
    NodeState state = stateOf(resources.resolve(record->owner.owner));
    record->bounds = *state.bounds;
    record->hasBounds = true;
  }
  for (auto &source : records_) {
    if (!source->active) {
      source->neighbors.clear();
      continue;
    }
    NodeState a = stateOf(resources.resolve(source->owner.owner));
    std::vector<OwnerLink *> next;
    for (auto &target : records_) {
      if (source == target)
        continue;
      NodeState b = stateOf(resources.resolve(target->owner.owner));
      if (a.sector != b.sector || !overlaps(source->bounds, target->bounds))
        continue;
      next.push_back(&target->owner);
      if (std::find(source->neighbors.begin(), source->neighbors.end(),
                    &target->owner) == source->neighbors.end()) {
        effects_.push_back(std::make_unique<Effect>(EffectKind::NeighborAdded,
                                                    target->owner.owner));
        if (!(b.state & 0x10000001u) && !(b.flags & 0x02000000u))
          effects_.push_back(
              std::make_unique<Effect>(EffectKind::Load, target->owner.owner));
      }
    }
    source->neighbors.swap(next);
  }
  return true;
}
