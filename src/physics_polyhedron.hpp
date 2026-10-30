#pragma once

#include "physics_body.hpp"

#include <string>

namespace physics {

struct CoreEdge {
  std::array<std::size_t, 2> vertices;
  Vec3 direction;
  std::vector<std::size_t> faces;
};
struct CoreFace {
  std::vector<std::size_t> vertices;
  std::vector<std::size_t> edges;
  Vec3 normal;
};
struct CoreVertex {
  Vec3 position;
  std::vector<std::size_t> edges;
};
enum class CoreFeatureKind { Vertex, Edge, Face };
struct CoreFeatureRef {
  CoreFeatureKind kind;
  std::size_t index;
};
struct CoreSeparation {
  CoreFeatureRef neighbor;
  std::size_t anchorVertex;
  Vec3 normal;
};
struct CoreFeature {
  CoreFeatureRef ref;
  std::vector<CoreSeparation> adjacent;
};
struct CollisionCore {
  std::vector<CoreVertex> vertices;
  std::vector<CoreEdge> edges;
  std::vector<CoreFace> faces;
  // Stock stores all three feature kinds in a single creation-ordered array.
  std::vector<CoreFeature> features;
};

// 004ac150/004ab8d0: faces contain three to seven vertex indices.
// Failure leaves output unchanged.
bool meshCore(const std::vector<Vec3>& vertices,
              const std::vector<std::vector<std::size_t>>& faces,
              CollisionCore& output, std::string& error);

// 004ac2b0 produces vertices only. 004ac260 adds box topology.
CollisionCore boundsVertices(const Vec3& low, const Vec3& high);
CollisionCore boundsCore(const Vec3& low, const Vec3& high);
CollisionCore boxCore(const Vec3& halfExtents); // 004ac1c0
CollisionCore pointCore(); // 004ac370
CollisionCore segmentCore(const Vec3& first, const Vec3& second); // 004ac440

} // namespace physics
