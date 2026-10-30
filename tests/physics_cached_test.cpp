#include "physics_cached.hpp"
#include "physics_core_pair.hpp"
#include <cassert>
#include <cmath>
using namespace physics;
int main() {
  CollisionCore tetra; std::string error;
  assert(meshCore({{0,0,0},{1,0,0},{0,1,0},{0,0,1}},
                  {{0,2,1},{0,1,3},{0,3,2},{1,2,3}}, tetra, error));
  State point, poly; point.pos={0.2f,0.2f,0.2f};
  std::optional<std::size_t> feature;
  assert(closestPointPolyhedron(point,tetra,poly,feature));
  assert(feature && *feature == 0);
  point.pos[0]=0.325f;
  auto cached=cachedPointFace(point,tetra,poly,*feature);
  auto walked=closestPointPolyhedron(point,tetra,poly,feature);
  assert(cached && walked && cached->distance == -0.2f && walked->distance > cached->distance);
  assert(cached->pointB == Vec3({0.325f,0.2f,0}));
  auto reversed=cachedPointFace(point,tetra,poly,0,false);
  assert(reversed && reversed->pointA==cached->pointB && reversed->normal[2]==1);
  point.pos={2,2,0.2f};
  assert(!cachedPointFace(point,tetra,poly,0));
  assert(!cachedPointFace(point,tetra,poly,tetra.features.size()));
}
