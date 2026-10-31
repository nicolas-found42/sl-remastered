#pragma once

#include "owner_links.hpp"
#include "world_model.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ResourceNode;
class ResourceWorld;

// Per-city broad phase. Record and neighbor addresses are stable until the next
// process() call; a detached OwnerLink denotes a destroyed resource.
class SectorCollisionManager {
public:
  struct Record {
    ~Record();
    OwnerLink owner;
    bool active = false;
    bool citySeed = false;
    uint64_t lastQueued = 0;
    ResourceBounds bounds;
    bool hasBounds = false;
    // Directed edges from an active record to overlapping records. Check each
    // link's owner before use: destruction of a resource detaches its link.
    std::vector<OwnerLink *> neighbors;
  };

  enum class EffectKind { NeighborAdded, Load };
  struct Effect {
    explicit Effect(EffectKind kind, LinkOwner *target);
    ~Effect();
    EffectKind kind;
    OwnerLink target;
    float demand = 10.0f;
  };
  explicit SectorCollisionManager(ResourceNode &city);
  ~SectorCollisionManager();
  SectorCollisionManager(const SectorCollisionManager &) = delete;
  SectorCollisionManager &operator=(const SectorCollisionManager &) = delete;

  // 004b3ee0: unloaded owners can be discovered across sector links.
  bool discover(LinkOwner &owner, ResourceWorld &resources, std::string &error);
  // 004b3c40: non-type-1 or different-sector owners are ignored.
  bool queue(LinkOwner &owner, ResourceWorld &resources, std::string &error);
  // Updates the 004b4250 record lifetime and X/Z broad phase. Narrow-phase
  // dispatch remains the caller's responsibility.
  bool process(ResourceWorld &resources, std::string &error);
  const std::vector<std::unique_ptr<Record>> &records() const {
    return records_;
  }
  const std::vector<std::unique_ptr<Effect>> &effects() const {
    return effects_;
  }
  LinkOwner *city() const { return city_.owner; }

private:
  OwnerLink city_;
  uint64_t frame_ = 0;
  std::vector<std::unique_ptr<Record>> records_;
  std::vector<std::unique_ptr<Effect>> effects_;
};
