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

  // A fixed edge pair rejects an endpoint crossing instead of following
  // the adjacent vertex as the new-candidate closest-feature walk does.
  const auto horizontal = segmentCore({-1, 0, 0}, {1, 0, 0});
  const auto vertical = segmentCore({0, -1, 0}, {0, 1, 0});
  State horizontalPose, verticalPose;
  verticalPose.pos = {0, 0, 0.25f};
  auto edge = cachedEdgeEdge(horizontal, horizontalPose, 2, vertical, verticalPose, 2);
  assert(edge && edge->distance == 0.25f);
  assert(edge->pointA == Vec3({0, 0, 0}) && edge->pointB == Vec3({0, 0, 0}));
  assert(edge->normal == Vec3({0, 0, -1}));
  verticalPose.pos[0] = 2;
  auto expired = cachedEdgeEdge(horizontal, horizontalPose, 2, vertical, verticalPose, 2);
  assert(expired && expired->distance == 0);
  std::optional<std::size_t> horizontalFeature = 2, verticalFeature = 2;
  auto migrated = closestSegmentCores(horizontal, horizontalPose, vertical, verticalPose,
                                      horizontalFeature, verticalFeature);
  assert(migrated && migrated->distance > 0 && horizontalFeature != 2);
  assert(!cachedEdgeEdge(horizontal, horizontalPose, 2, horizontal, verticalPose, 2));
  assert(!cachedEdgeEdge(horizontal, horizontalPose, 0, vertical, verticalPose, 2));

  // 004af630 return2 keeps penetrating edges and 004af970 negates the
  // distance and cross-edge normal; it never replaces either feature.
  const auto box = boxCore({1, 1, 1});
  State firstBox, secondBox;
  const float turn = std::sqrt(0.5f);
  secondBox.rot = {turn, 0, turn, 0, 0, 1, 0, 0, -turn, 0, turn, 0};
  secondBox.pos[1] = -0.5f;
  auto penetrating = cachedEdgeEdge(box, firstBox, 3, box, secondBox, 5);
  assert(penetrating && penetrating->distance < 0);
  assert(penetrating->normal[0] > 0 && penetrating->normal[2] > 0);

  // Moving along the same infinite edge leaves one region penetrating but
  // takes its finite endpoints past the other edge. Stock's uncaptured
  // initial side decides return1 versus return2; both query orders refuse.
  secondBox.pos[0] = 3 * turn;
  secondBox.pos[2] = -3 * turn;
  assert(!cachedEdgeEdge(box, firstBox, 3, box, secondBox, 5));
  assert(!cachedEdgeEdge(box, secondBox, 5, box, firstBox, 3));
}
