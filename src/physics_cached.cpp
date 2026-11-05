#include "physics_cached.hpp"
#include <cmath>

namespace physics {
namespace {
std::optional<CorePair> cachedLocalPointFace(const Vec3& localPoint, const State& pointPose,
    const CollisionCore& core, const State& polyPose,
    std::size_t featureIndex, bool pointFirst) {
  if (featureIndex >= core.features.size()) return std::nullopt;
  const auto& feature = core.features[featureIndex];
  if (feature.ref.kind != CoreFeatureKind::Face || feature.ref.index >= core.faces.size())
    return std::nullopt;
  const auto& face = core.faces[feature.ref.index];
  if (face.vertices.empty() || face.vertices[0] >= core.vertices.size()) return std::nullopt;
  // 004af4f0 rotates with 00436880 before adding/subtracting origins;
  // 004b1460 uses a different float grouping for this operation.
  const auto rotated = matrixTransformRows(pointPose.rot, localPoint);
  Vec3 world = {(rotated[0] + pointPose.pos[0]) - polyPose.pos[0],
                (rotated[1] + pointPose.pos[1]) - polyPose.pos[1],
                (rotated[2] + pointPose.pos[2]) - polyPose.pos[2]};
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
  result.pointA = localPoint;
  for (int i = 0; i < 3; ++i) result.pointB[i] = -d*face.normal[i] + p[i];
  result.normal = matrixTransformRows(polyPose.rot, face.normal);
  result.distance = d;
  if (!pointFirst) {
    std::swap(result.pointA, result.pointB);
    for (auto& n : result.normal) n = -n;
  }
  return result;
}
} // namespace

std::optional<CorePair> cachedPointFace(const State& pointPose,
    const CollisionCore& core, const State& polyPose,
    std::size_t featureIndex, bool pointFirst) {
  return cachedLocalPointFace({}, pointPose, core, polyPose, featureIndex, pointFirst);
}

std::optional<CorePair> cachedVertexFace(const CollisionCore& vertexCore,
    const State& vertexPose, std::size_t vertexFeature,
    const CollisionCore& faceCore, const State& facePose,
    std::size_t faceFeature, bool vertexFirst) {
  if (vertexFeature >= vertexCore.features.size()) return std::nullopt;
  const auto ref = vertexCore.features[vertexFeature].ref;
  if (ref.kind != CoreFeatureKind::Vertex || ref.index >= vertexCore.vertices.size())
    return std::nullopt;
  return cachedLocalPointFace(vertexCore.vertices[ref.index].position,
                              vertexPose, faceCore, facePose, faceFeature, vertexFirst);
}
} // namespace physics

namespace physics {
std::optional<CorePair> cachedEdgeEdge(const CollisionCore& first,
                                       const State& firstPose, std::size_t firstFeature,
                                       const CollisionCore& second, const State& secondPose,
                                       std::size_t secondFeature) {
  const CollisionCore* cores[] = {&first, &second};
  const State* poses[] = {&firstPose, &secondPose};
  const std::size_t indices[] = {firstFeature, secondFeature};
  const CoreEdge* edges[2]{};
  for (unsigned side = 0; side < 2; ++side) {
    const auto& core = *cores[side];
    if (indices[side] >= core.features.size()) return std::nullopt;
    const auto& feature = core.features[indices[side]];
    if (feature.ref.kind != CoreFeatureKind::Edge ||
        feature.ref.index >= core.edges.size() || feature.adjacent.size() < 2)
      return std::nullopt;
    edges[side] = &core.edges[feature.ref.index];
    for (auto vertex : edges[side]->vertices)
      if (vertex >= core.vertices.size()) return std::nullopt;
  }
  unsigned regions[2]{};
  // 004af630's two endpoint planes reject; all remaining neighboring
  // face planes determine sign. Stock begins at persistent DAT_0064464c.
  // Evaluate both sides and refuse differing early-return classifications
  // until that orientation is captured; do not invent its current value.
  for (unsigned side = 0; side < 2; ++side) {
    const unsigned other = side ^ 1;
    const auto& core = *cores[side];
    const auto& feature = core.features[indices[side]];
    const auto& pose = *poses[side];
    const auto& otherPose = *poses[other];
    const auto& anchor = core.vertices[edges[side]->vertices[0]].position;
    const auto& otherAnchor = cores[other]->vertices[edges[other]->vertices[0]].position;
    const auto rotated = matrixTransformRows(otherPose.rot, otherAnchor);
    const Vec3 offset{(rotated[0] + otherPose.pos[0]) - pose.pos[0],
                      (rotated[1] + otherPose.pos[1]) - pose.pos[1],
                      (rotated[2] + otherPose.pos[2]) - pose.pos[2]};
    const Vec3 origin{pose.rot[0] * offset[0] + pose.rot[4] * offset[1] + pose.rot[8] * offset[2],
                      pose.rot[5] * offset[1] + pose.rot[1] * offset[0] + pose.rot[9] * offset[2],
                      pose.rot[6] * offset[1] + pose.rot[2] * offset[0] + pose.rot[10] * offset[2]};
    const auto& od = edges[other]->direction;
    const Vec3 worldDirection{od[0] * otherPose.rot[0] + otherPose.rot[1] * od[1] + otherPose.rot[2] * od[2],
                              otherPose.rot[5] * od[1] + otherPose.rot[4] * od[0] + otherPose.rot[6] * od[2],
                              otherPose.rot[9] * od[1] + otherPose.rot[8] * od[0] + otherPose.rot[10] * od[2]};
    const Vec3 direction{worldDirection[0] * pose.rot[0] + worldDirection[1] * pose.rot[4] + worldDirection[2] * pose.rot[8],
                         worldDirection[0] * pose.rot[1] + worldDirection[1] * pose.rot[5] + worldDirection[2] * pose.rot[9],
                         worldDirection[0] * pose.rot[2] + worldDirection[1] * pose.rot[6] + worldDirection[2] * pose.rot[10]};
    const auto& edgeDirection = edges[side]->direction;
    const float cx = edgeDirection[2] * direction[1] - edgeDirection[1] * direction[2];
    const float cy = edgeDirection[0] * direction[2] - edgeDirection[2] * direction[0];
    const float cz = edgeDirection[1] * direction[0] - edgeDirection[0] * direction[1];
    // The retained decompile leaves the projected point uninitialized here.
    // Do not manufacture parallel-edge geometry without a Stock witness.
    if (cx * cx + cz * cz + cy * cy == 0) return std::nullopt;
    const float nx = edgeDirection[2] * cy - edgeDirection[1] * cz;
    const float ny = edgeDirection[0] * cz - edgeDirection[2] * cx;
    const float nz = edgeDirection[1] * cx - edgeDirection[0] * cy;
    const float denominator = nx * direction[0] + ny * direction[1] + nz * direction[2];
    if (denominator == 0) return std::nullopt;
    const float t = (nx * (anchor[0] - origin[0]) +
                     (anchor[1] - origin[1]) * ny + (anchor[2] - origin[2]) * nz) /
                    denominator;
    const Vec3 projected{t * direction[0] + origin[0],
                         t * direction[1] + origin[1],
                         t * direction[2] + origin[2]};
    if (!std::isfinite(t)) return std::nullopt;
    for (std::size_t i = 0; i < 2; ++i) {
      const auto& plane = feature.adjacent[i];
      if (plane.anchorVertex >= core.vertices.size()) return std::nullopt;
      const auto& vertex = core.vertices[plane.anchorVertex].position;
      if ((projected[0] - vertex[0]) * plane.normal[0] +
              (projected[1] - vertex[1]) * plane.normal[1] +
              (projected[2] - vertex[2]) * plane.normal[2] <
          0) {
        regions[side] = 1;
        break;
      }
    }
    if (regions[side] == 1) continue;
    std::size_t behind = 0;
    for (std::size_t i = 2; i < feature.adjacent.size(); ++i) {
      const auto neighbor = feature.adjacent[i].neighbor;
      if (neighbor.kind != CoreFeatureKind::Face || neighbor.index >= core.faces.size())
        return std::nullopt;
      const auto& face = core.faces[neighbor.index];
      if (face.vertices.empty() || face.vertices[0] >= core.vertices.size()) return std::nullopt;
      const auto& vertex = core.vertices[face.vertices[0]].position;
      if ((projected[0] - vertex[0]) * face.normal[0] +
              (projected[1] - vertex[1]) * face.normal[1] +
              (projected[2] - vertex[2]) * face.normal[2] <
          0)
        ++behind;
    }
    if (behind > 0 && behind == feature.adjacent.size() - 2)
      regions[side] = 2;
  }
  if ((regions[0] == 1 && regions[1] == 2) ||
      (regions[0] == 2 && regions[1] == 1))
    return std::nullopt;
  if (regions[0] == 1 || regions[1] == 1) return CorePair{};
  const bool penetrating = regions[0] == 2 || regions[1] == 2;
  // 004ad220 -> 004ac760: stored edge directions, endpoint clamping and
  // individual float stores precede the signed square root in 004af970.
  const auto worldEndpoint = [&](unsigned side, unsigned endpoint) {
    const auto& vertex = cores[side]->vertices[edges[side]->vertices[endpoint]].position;
    auto point = matrixTransformRows(poses[side]->rot, vertex);
    for (unsigned axis = 0; axis < 3; ++axis) point[axis] += poses[side]->pos[axis];
    return point;
  };
  const Vec3 a = worldEndpoint(0, 0), ae = worldEndpoint(0, 1);
  const Vec3 b = worldEndpoint(1, 0), be = worldEndpoint(1, 1);
  const auto& d0 = edges[0]->direction;
  const auto& d1 = edges[1]->direction;
  const Vec3 da{d0[0] * firstPose.rot[0] + firstPose.rot[2] * d0[2] + firstPose.rot[1] * d0[1],
                firstPose.rot[6] * d0[2] + firstPose.rot[5] * d0[1] + firstPose.rot[4] * d0[0],
                firstPose.rot[10] * d0[2] + firstPose.rot[9] * d0[1] + firstPose.rot[8] * d0[0]};
  const Vec3 db{d1[0] * secondPose.rot[0] + secondPose.rot[2] * d1[2] + secondPose.rot[1] * d1[1],
                secondPose.rot[6] * d1[2] + secondPose.rot[4] * d1[0] + secondPose.rot[5] * d1[1],
                secondPose.rot[10] * d1[2] + secondPose.rot[8] * d1[0] + secondPose.rot[9] * d1[1]};
  const Vec3 difference{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  Vec3 normal{da[1] * db[2] - db[1] * da[2],
              db[0] * da[2] - da[0] * db[2],
              db[1] * da[0] - db[0] * da[1]};
  const Vec3 nb{normal[1] * da[2] - normal[2] * da[1],
                normal[2] * da[0] - normal[0] * da[2],
                normal[0] * da[1] - normal[1] * da[0]};
  const float tb = -((nb[0] * difference[0] + difference[1] * nb[1] + difference[2] * nb[2]) /
                     (nb[0] * db[0] + nb[1] * db[1] + nb[2] * db[2]));
  const Vec3 na{normal[1] * db[2] - normal[2] * db[1],
                normal[2] * db[0] - normal[0] * db[2],
                normal[0] * db[1] - normal[1] * db[0]};
  const float ta = (na[0] * difference[0] + difference[1] * na[1] + difference[2] * na[2]) /
                   (na[2] * da[2] + na[1] * da[1] + na[0] * da[0]);
  if (!std::isfinite(ta) || !std::isfinite(tb)) return std::nullopt;
  const float az = ta * da[2];
  const float measureA = ta * da[1] * da[1] + az * da[2] + ta * da[0] * da[0];
  const float measureB = tb * db[0] * db[0] + tb * db[2] * db[2] + tb * db[1] * db[1];
  Vec3 offsetA{}, offsetB{};
  if (measureA >= 0) {
    offsetA = {ta * da[0], ta * da[1], az};
    if ((ae[2] - a[2]) * da[2] + (ae[1] - a[1]) * da[1] + (ae[0] - a[0]) * da[0] < measureA)
      for (unsigned axis = 0; axis < 3; ++axis) offsetA[axis] = ae[axis] - a[axis];
  }
  if (measureB >= 0) {
    offsetB = {tb * db[0], tb * db[1], tb * db[2]};
    if ((be[0] - b[0]) * db[0] + (be[1] - b[1]) * db[1] + (be[2] - b[2]) * db[2] < measureB)
      for (unsigned axis = 0; axis < 3; ++axis) offsetB[axis] = be[axis] - b[axis];
  }
  if (normal[0] * difference[0] + difference[1] * normal[1] + difference[2] * normal[2] > 0)
    for (auto& coordinate : normal) coordinate = -coordinate;
  Vec3 worldA{}, worldB{};
  for (unsigned axis = 0; axis < 3; ++axis) {
    worldA[axis] = offsetA[axis] + a[axis];
    worldB[axis] = offsetB[axis] + b[axis];
  }
  const Vec3 localA{worldA[0] - firstPose.pos[0], worldA[1] - firstPose.pos[1], worldA[2] - firstPose.pos[2]};
  const Vec3 localB{worldB[0] - secondPose.pos[0], worldB[1] - secondPose.pos[1], worldB[2] - secondPose.pos[2]};
  CorePair result;
  result.pointA = {firstPose.rot[0] * localA[0] + firstPose.rot[4] * localA[1] + firstPose.rot[8] * localA[2],
                   firstPose.rot[5] * localA[1] + firstPose.rot[1] * localA[0] + firstPose.rot[9] * localA[2],
                   firstPose.rot[6] * localA[1] + firstPose.rot[2] * localA[0] + firstPose.rot[10] * localA[2]};
  result.pointB = {secondPose.rot[0] * localB[0] + secondPose.rot[8] * localB[2] + secondPose.rot[4] * localB[1],
                   secondPose.rot[1] * localB[0] + secondPose.rot[9] * localB[2] + secondPose.rot[5] * localB[1],
                   secondPose.rot[2] * localB[0] + secondPose.rot[10] * localB[2] + secondPose.rot[6] * localB[1]};
  const float dx = worldA[0] - worldB[0], dy = worldA[1] - worldB[1], dz = worldA[2] - worldB[2];
  result.distance = std::sqrt(dz * dz + dy * dy + dx * dx);
  const float length = std::sqrt(normal[2] * normal[2] + normal[1] * normal[1] + normal[0] * normal[0]);
  if (length != 0) {
    const float inverse = 1 / length;
    for (unsigned axis = 0; axis < 3; ++axis) result.normal[axis] = normal[axis] * inverse;
  }
  if (penetrating) {
    result.distance = -result.distance;
    for (auto& coordinate : result.normal) coordinate = -coordinate;
  }
  return result;
}
} // namespace physics

namespace physics {
namespace {
std::optional<CorePair> cachedLocalPointEdge(const Vec3& vertex, const State& vertexPose,
                                             const CollisionCore& edgeCore, const State& edgePose, std::size_t edgeFeature,
                                             bool vertexFirst) {
  if (edgeFeature >= edgeCore.features.size()) return std::nullopt;
  const auto& feature = edgeCore.features[edgeFeature];
  if (feature.ref.kind != CoreFeatureKind::Edge || feature.ref.index >= edgeCore.edges.size() ||
      feature.adjacent.size() < 2)
    return std::nullopt;
  const auto& edge = edgeCore.edges[feature.ref.index];
  for (const auto endpoint : edge.vertices)
    if (endpoint >= edgeCore.vertices.size()) return std::nullopt;
  const auto inverse = [&](const Vec3& world) {
    return Vec3{edgePose.rot[0] * world[0] + edgePose.rot[4] * world[1] + edgePose.rot[8] * world[2],
                edgePose.rot[5] * world[1] + edgePose.rot[1] * world[0] + edgePose.rot[9] * world[2],
                edgePose.rot[6] * world[1] + edgePose.rot[2] * world[0] + edgePose.rot[10] * world[2]};
  };
  // 004af3a0 inlines x/z/y rotation sums before 0045dbc0.
  Vec3 relative;
  for (unsigned axis = 0; axis < 3; ++axis)
    relative[axis] = ((vertexPose.rot[axis * 4] * vertex[0] +
                       vertexPose.rot[axis * 4 + 2] * vertex[2] +
                       vertexPose.rot[axis * 4 + 1] * vertex[1]) +
                      vertexPose.pos[axis]) -
                     edgePose.pos[axis];
  const auto checked = inverse(relative);
  for (const auto& plane : feature.adjacent) {
    if (plane.anchorVertex >= edgeCore.vertices.size()) return std::nullopt;
    const auto& anchor = edgeCore.vertices[plane.anchorVertex].position;
    const float distance = (checked[1] - anchor[1]) * plane.normal[1] +
                           (checked[0] - anchor[0]) * plane.normal[0] +
                           (checked[2] - anchor[2]) * plane.normal[2];
    if (!std::isfinite(distance)) return std::nullopt;
    if (distance < 0) return CorePair{};
  }
  // 004ace20 instead calls 00436880 for the distance query's vertex pose.
  const auto vertexRotated = matrixTransformRows(vertexPose.rot, vertex);
  for (unsigned axis = 0; axis < 3; ++axis)
    relative[axis] = (vertexRotated[axis] + vertexPose.pos[axis]) - edgePose.pos[axis];
  const auto local = inverse(relative);
  const auto& start = edgeCore.vertices[edge.vertices[0]].position;
  const auto& end = edgeCore.vertices[edge.vertices[1]].position;
  const auto& direction = edge.direction;
  const float along = (local[0] - start[0]) * direction[0] +
                      (local[1] - start[1]) * direction[1] +
                      (local[2] - start[2]) * direction[2];
  Vec3 projection;
  if (along >= 0) {
    const float length = (end[0] - start[0]) * direction[0] +
                         (end[1] - start[1]) * direction[1] +
                         (end[2] - start[2]) * direction[2];
    if (along <= length)
      for (unsigned axis = 0; axis < 3; ++axis)
        projection[axis] = along * direction[axis] + start[axis];
    else
      projection = end;
  } else {
    projection = start;
  }
  const auto edgeRotated = matrixTransformRows(edgePose.rot, projection);
  CorePair result;
  result.pointA = vertex;
  result.pointB = projection;
  for (unsigned axis = 0; axis < 3; ++axis)
    result.normal[axis] = (vertexRotated[axis] + vertexPose.pos[axis]) -
                          (edgeRotated[axis] + edgePose.pos[axis]);
  result.distance = std::sqrt(result.normal[0] * result.normal[0] +
                              result.normal[2] * result.normal[2] +
                              result.normal[1] * result.normal[1]);
  if (!std::isfinite(result.distance)) return std::nullopt;
  if (result.distance > 0) {
    const float scale = 1 / result.distance;
    for (auto& value : result.normal) value *= scale;
  }
  if (!vertexFirst) {
    std::swap(result.pointA, result.pointB);
    for (auto& value : result.normal) value = -value;
  }
  return result;
}
} // namespace
std::optional<CorePair> cachedPointEdge(const State& pointPose,
                                        const CollisionCore& edgeCore, const State& edgePose, std::size_t edgeFeature,
                                        bool pointFirst) {
  return cachedLocalPointEdge({}, pointPose, edgeCore, edgePose, edgeFeature, pointFirst);
}
std::optional<CorePair> cachedVertexEdge(const CollisionCore& vertexCore,
                                         const State& vertexPose, std::size_t vertexFeature,
                                         const CollisionCore& edgeCore, const State& edgePose, std::size_t edgeFeature,
                                         bool vertexFirst) {
  if (vertexFeature >= vertexCore.features.size()) return std::nullopt;
  const auto ref = vertexCore.features[vertexFeature].ref;
  if (ref.kind != CoreFeatureKind::Vertex || ref.index >= vertexCore.vertices.size())
    return std::nullopt;
  return cachedLocalPointEdge(vertexCore.vertices[ref.index].position, vertexPose,
                              edgeCore, edgePose, edgeFeature, vertexFirst);
}
} // namespace physics
