#include "physics_box_ray.hpp"
#include <cassert>
#include <cmath>

int main() {
  const std::array<float, 3> box{1, 1, 1};
  auto hit = physicsBoxRayFraction({-2, 0, 0}, {2, 0, 0}, box);
  assert(hit && std::abs(*hit - 0.25f) < 0.00001f);
  assert(!physicsBoxRayFraction({-2, 2, 0}, {2, 2, 0}, box));
  assert(!physicsBoxRayFraction({-2, 0, 0}, {0, 0, 0}, box));
  assert(!physicsBoxRayFraction({2, 0, 0}, {-1, 0, 0}, box));
  assert(physicsBoxRayFraction({-2, 1, 0}, {2, 1, 0}, box));
  assert(!physicsBoxRayFraction({-2, 0, 0}, {2, 0, 0}, {-1, 1, 1}));
}
