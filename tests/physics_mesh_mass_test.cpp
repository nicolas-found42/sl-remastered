#include "physics_mesh_mass.hpp"
#include <cassert>
#include <cmath>
#include <string>
#include <cstdint>
#include <cstring>

static uint32_t bits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, sizeof result);
  return result;
}

int main() {
  physics::BodyProto p;
  p.invMass=0.5f;
  p.shape.vertices={{{0,0,0}},{{1,0,0}},{{0,1,0}},{{0,0,1}}};
  const float s=1.0f/std::sqrt(3.0f);
  std::vector<physics::MeshTriangle> t={{{{0,2,1}},{{0,0,-1}}},{{{0,1,3}},{{0,-1,0}}},{{{0,3,2}},{{-1,0,0}}},{{{1,2,3}},{{s,s,s}}}};
  physics::Vec3 c{}; std::string error;
  assert(physics::applyMeshMassProperties(p,t,c,error));
  assert(std::abs(p.volume-1.0f/6.0f)<1e-6f);
  for(float x:c) assert(std::abs(x-0.25f)<1e-6f);
  // Unit tetrahedron has diagonal inertia 3/40 and off diagonal 1/80 per unit mass.
  assert(std::abs(p.inertia[0]-0.15f)<1e-5f);
  assert(std::abs(p.inertia[1]-0.025f)<1e-5f);
  physics::BodyProto stock;
  stock.invMass = 1;
  stock.shape.vertices = {{0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}};
  t.back().normal = {1,1,1};
  assert(physics::applyMeshMassProperties(stock, t, c, error));
  const uint32_t stockCenter[]{0x3e800000, 0x3e800000, 0x3e7ffffd};
  const uint32_t stockInverse[]{0x4160000c, 0xc0000015, 0xbfffffb4, 0,
                                0xc0000015, 0x41600010, 0xc0000004, 0,
                                0xbfffffb4, 0xc0000004, 0x415ffff8, 0};
  assert(bits(stock.volume) == 0x3e2aaaab);
  for (size_t i = 0; i < 3; ++i) assert(bits(c[i]) == stockCenter[i]);
  for (size_t i : {0, 1, 2, 4, 5, 6, 8, 9, 10})
    assert(bits(stock.invInertia[i]) == stockInverse[i]);
}
