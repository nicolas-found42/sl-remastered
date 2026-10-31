#pragma once

#include "owner_links.hpp"
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>

// Stable stock-style links keep queued timers from retaining destroyed nodes.
class TimerState {
public:
  struct Timer {
    OwnerLink target;
    float deadline = 0;
    int32_t id = 0;
    // Stock timer request word (FUN_0048b750 argument 3). The object random
    // sound path (id 0x80000080) carries (entry index << 16) | sub-event;
    // script timers leave both zero and keep the stock script upcall path.
    uint32_t event = 0;
    int32_t param = 0;
    ~Timer() { setLinkOwner(target, nullptr); }
  };
  using PhysicsStep = std::function<bool(float, std::string &)>;
  using Drain = std::function<bool(std::string &)>;
  using Delivery = std::function<bool(LinkOwner &, int32_t, uint32_t, int32_t,
                                      std::string &)>;
  float now() const { return now_; }
  float timeScale() const { return timeScale_; }
  float timeOrigin() const { return timeOrigin_; }
  bool setTimeWarp(float scale, double performanceMilliseconds,
                   std::string &error);
  size_t size() const { return timers_.size(); }
  bool add(LinkOwner *target, float delay, int32_t id, std::string &error,
           uint32_t event = 0, int32_t param = 0);
  void removeAll(LinkOwner *target);
  bool advance(float now, const Delivery &delivery, std::string &error);
  bool advanceFrame(float elapsed, float timeScale, float maxDelta,
                    float maxStep, const PhysicsStep &physics,
                    const Delivery &delivery, const Drain &drain,
                    std::string &error);

private:
  float now_ = 0;
  float timeScale_ = 1.0f;
  float timeOrigin_ = 0;
  float partial_ = 0;
  bool advancing_ = false;
  std::list<std::unique_ptr<Timer>> timers_;
};
