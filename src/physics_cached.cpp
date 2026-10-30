#include "physics_cached.hpp"

namespace physics {
std::optional<CorePair> cachedPointFace(const State& pointPose,
    const CollisionCore& core, const State& polyPose,
    std::size_t featureIndex, bool pointFirst) {
  if (featureIndex >= core.features.size()) return std::nullopt;
  const auto& feature = core.features[featureIndex];
  if (feature.ref.kind != CoreFeatureKind::Face || feature.ref.index >= core.faces.size())
    return std::nullopt;
  const auto& face = core.faces[feature.ref.index];
  if (face.vertices.empty() || face.vertices[0] >= core.vertices.size()) return std::nullopt;
  Vec3 world = {pointPose.pos[0] - polyPose.pos[0], pointPose.pos[1] - polyPose.pos[1],
                pointPose.pos[2] - polyPose.pos[2]};
  Vec3 p = {polyPose.rot[0]*world[0] + polyPose.rot[4]*world[1] + polyPose.rot[8]*world[2],
            polyPose.rot[1]*world[0] + polyPose.rot[5]*world[1] + polyPose.rot[9]*world[2],
            polyPose.rot[2]*world[0] + polyPose.rot[6]*world[1] + polyPose.rot[10]*world[2]};
  for (const auto& side : feature.adjacent) {
    if (side.anchorVertex >= core.vertices.size()) return std::nullopt;
    const auto& a = core.vertices[side.anchorVertex].position;
    if ((p[1]-a[1])*side.normal[1] + (p[0]-a[0])*side.normal[0] +
        (p[2]-a[2])*side.normal[2] < 0) return std::nullopt;
  }
  const auto& a = core.vertices[face.vertices[0]].position;
  const float d = (p[0]-a[0])*face.normal[0] + (p[1]-a[1])*face.normal[1] +
                  (p[2]-a[2])*face.normal[2];
  CorePair result;
  result.pointA = {};
  for (int i = 0; i < 3; ++i) result.pointB[i] = -d*face.normal[i] + p[i];
  result.normal = matrixTransformRows(polyPose.rot, face.normal);
  result.distance = d;
  if (!pointFirst) {
    std::swap(result.pointA, result.pointB);
    for (auto& n : result.normal) n = -n;
  }
  return result;
}
}
