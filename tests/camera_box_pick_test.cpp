#include "camera_box_pick.hpp"
#include <cassert>
#include <cmath>
int main() {
  const std::array<float, 16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  const ViewportInstance viewport{0, 0, 0, 1, 1};
  PhysicsBox distant; distant.halfExtents = {0.01f, 0.01f, 0.01f};
  distant.position = {{{0,0,0.08f},{0,0,0.08f}}};
  PhysicsBox near = distant; near.position = {{{0,0,0.04f},{0,0,0.04f}}};
  auto hit = pickCameraBox({0, 0}, viewport, identity, identity, {distant, near});
  assert(hit && hit->index == 1 && std::abs(hit->fraction - 0.3f) < 0.0001f);
  assert(!pickCameraBox({0.5f, 0}, viewport, identity, identity, {near}));
  assert(!pickCameraBox({2, 0}, viewport, identity, identity, {near}));
  const ViewportInstance invalid{0, 0, 0, 0, 1};
  assert(!pickCameraBox({0, 0}, invalid, identity, identity, {near}));
}
