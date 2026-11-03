#pragma once

#include "owner_links.hpp"

#include <cstdint>
#include <string>

struct CarConfig;
struct CarRuntimeInstance;
struct RuntimeSlotState;
struct WorldNode;
class GameWorld;

// Native Part+b8/bc are damage and reciprocal lifetime. Public getWear()
// exposes health; keeping the native values avoids losing small damage when
// health rounds to one. Newer004cd1d0/004ce390/004cd740.
class PartWearState {
public:
  void initialize(float lifetime) {
    damage_ = 0;
    setMaximumWear(lifetime);
  }
  float health() const { return 1.0f - damage_; }
  void setHealth(float value) { damage_ = 1.0f - value; }
  float damage() const { return damage_; }
  void setDamage(float value) { damage_ = value; }
  float rate() const { return rate_; }
  void setMaximumWear(float lifetime) {
    rate_ = 1.0f / lifetime;
    initialized_ = true;
  }
  bool initialized() const { return initialized_; }

private:
  float damage_ = 0;
  float rate_ = 0;
  bool initialized_ = false;
};

// Newer004a8060 retains the last contributing Part for modes9/10/12.
// OwnerLink storage must remain stable while attached.
struct PartRefreshSources {
  OwnerLink clutch;
  OwnerLink transmission;
  OwnerLink cooling;
  ~PartRefreshSources();
};

// Factory initialization supplies the qualified prototype lifetime. This does
// not infer a prototype role or default from an unbound runtime record.
void initializePartRefresh(CarRuntimeInstance& part, float maximumWearLife);

// Apply target inheritance and independent slot halfword overrides to a
// newly mounted tree, before registry callbacks publish the installation.
bool inheritInstalledPartRefresh(GameWorld& world, WorldNode& part,
                                  const WorldNode& target,
                                  const RuntimeSlotState& targetSlot,
                                  std::string& error);

// Called after script updatevariables and before the per-Car config copy.
// Traversal follows the native registry order, not a second tree walk.
bool applyInstalledPartRefresh(GameWorld& world, WorldNode& car,
                                const CarConfig& config, std::string& error);
