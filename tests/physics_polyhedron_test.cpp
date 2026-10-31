#include "physics_polyhedron.hpp"
#include <cassert>
#include <cmath>

int main() {
  using namespace physics;
  auto core = boundsCore({-1, -2, -3}, {4, 5, 6});
  assert(core.vertices.size() == 8 && core.edges.size() == 12 &&
         core.faces.size() == 6);
  assert((core.vertices[0].position == Vec3{-1, -2, -3}));
  assert((core.vertices[7].position == Vec3{4, 5, 6}));
  assert((core.faces[0].vertices == std::vector<std::size_t>{0, 2, 3, 1}));
  assert((core.faces[1].vertices == std::vector<std::size_t>{4, 5, 7, 6}));
  assert((core.faces[0].normal == Vec3{0, 0, -1}));
  for (const auto &e : core.edges)
    assert(e.faces.size() == 2);
  for (const auto &v : core.vertices)
    assert(v.edges.size() == 3);
  auto points = boundsVertices({0, 0, 0}, {1, 1, 1});
  assert(points.vertices.size() == 8 && points.edges.empty() &&
         points.faces.empty());
  auto box = boxCore({1, 2, 3});
  assert((box.vertices[7].position == Vec3{1, 2, 3}));
  assert(pointCore().vertices.size() == 1);
  assert(pointCore().features.size() == 1);
  auto segment = segmentCore({0, 0, 0}, {0, 3, 4});
  assert(segment.vertices.size() == 2 && segment.edges.size() == 1 &&
         segment.faces.empty());
  assert(segment.features.size() == 3 &&
         segment.features[2].adjacent.size() == 2);
  assert(std::abs(segment.edges[0].direction[1] - 0.6f) < 1e-6f);
  assert(std::abs(segment.edges[0].direction[2] - 0.8f) < 1e-6f);
  CollisionCore tetra;
  std::string error;
  const std::vector<Vec3> vertices{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  const std::vector<std::vector<std::size_t>> faces{
      {0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {2, 3, 0}};
  assert(meshCore(vertices, faces, tetra, error));
  assert(tetra.features.size() == 14 && tetra.edges.size() == 6 &&
         tetra.faces.size() == 4);
  assert(tetra.features[0].ref.kind == CoreFeatureKind::Face);
  assert(tetra.features[1].ref.kind == CoreFeatureKind::Vertex &&
         tetra.features[1].ref.index == 0);
  assert(tetra.features[2].ref.kind == CoreFeatureKind::Vertex &&
         tetra.features[2].ref.index == 1);
  assert(tetra.features[3].ref.kind == CoreFeatureKind::Edge);
  assert((tetra.faces[0].normal == Vec3{0, 0, 1}));
  assert((tetra.features[3].adjacent[0].normal == Vec3{1, 0, 0}));
  assert((tetra.features[3].adjacent[1].normal == Vec3{-1, 0, 0}));
  assert((tetra.features[0].adjacent[0].normal == Vec3{0, 1, 0}));
  assert(tetra.edges[0].faces.size() == 2);
  CollisionCore unchanged = pointCore();
  assert(!meshCore(vertices, {{0, 1, 9}}, unchanged, error));
  assert(unchanged.vertices.size() == 1 && !error.empty());
  // 004ab8d0 stops when the feature count reaches the 004ac150 reservation
  // and 004a5e80 ignores the failure: the single triangle below needs seven
  // features (face + 3 vertices + 3 edges) against a reservation of six, so
  // the core keeps the well-formed prefix and links only existing features.
  CollisionCore capped;
  assert(meshCore(vertices, {{0, 1, 2}}, capped, error));
  assert(capped.features.size() == 6 && capped.faces.size() == 1 &&
         capped.edges.size() == 2 && capped.vertices.size() == 4 &&
         error.empty());
  for (const auto &f : capped.features)
    for (const auto &adj : f.adjacent)
      assert(adj.neighbor.index < capped.features.size());
  CollisionCore triangle;
  const std::array<Vec3, 3> tri{{{-2, 0, -2}, {0, 0, 2}, {2, 0, -2}}};
  assert(triangleCore(tri, {0, 1, 0}, triangle, error));
  assert(triangle.vertices.size() == 3 && triangle.edges.size() == 3 &&
         triangle.faces.size() == 2 && triangle.features.size() == 8);
  assert((triangle.faces[0].normal == Vec3{0, 1, 0}));
  assert((triangle.faces[1].normal == Vec3{0, -1, 0}));
  for (const auto &edge : triangle.edges)
    assert(edge.faces.size() == 2);
  assert(!triangleCore(tri, {1, 0, 0}, triangle, error));
  assert(triangle.faces.size() == 2 && !error.empty());
}
