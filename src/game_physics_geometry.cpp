#include "game_physics_geometry.hpp"
#include "physics_polyhedron.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace {
float floatWord(uint32_t word) {
  float value;
  std::memcpy(&value, &word, sizeof value);
  return value;
}
} // namespace
bool decodeGamePhysicsGeometry(const GamePhysicsChunk &chunk,
                               GamePhysicsGeometry &out, std::string &error) {
  GamePhysicsGeometry result;
  result.vertices.reserve(chunk.triples.size());
  for (const auto &words : chunk.triples) {
    std::array<float, 3> point{};
    for (size_t i = 0; i < 3; ++i) {
      point[i] = floatWord(words[i]);
      if (!std::isfinite(point[i])) {
        error = "PHYS: non-finite vertex";
        return false;
      }
    }
    result.vertices.push_back(point);
  }
  result.triangles.reserve(chunk.records.size());
  for (const auto &words : chunk.records) {
    GamePhysicsTriangle triangle;
    triangle.materialWord = words[0];
    for (size_t i = 0; i < 3; ++i) {
      triangle.vertices[i] = words[i + 1];
      triangle.normal[i] = floatWord(words[i + 4]);
      if (triangle.vertices[i] >= result.vertices.size()) {
        error = "PHYS: vertex index out of range";
        return false;
      }
      if (!std::isfinite(triangle.normal[i])) {
        error = "PHYS: non-finite normal";
        return false;
      }
    }
    result.triangles.push_back(triangle);
  }
  out = std::move(result);
  error.clear();
  return true;
}

GamePhysicsHit queryGamePhysicsFace(const GamePhysicsGeometry &geometry,
                                    size_t face,
                                    const std::array<float, 3> &start,
                                    const std::array<float, 3> &end) {
  GamePhysicsHit result;
  if (face >= geometry.triangles.size())
    return result;
  const auto &triangle = geometry.triangles[face];
  for (size_t i = 0; i < 3; ++i)
    if (triangle.vertices[i] >= geometry.vertices.size() ||
        !std::isfinite(start[i]) || !std::isfinite(end[i]) ||
        !std::isfinite(triangle.normal[i]))
      return result;
  const auto &a = geometry.vertices[triangle.vertices[0]],
             &b = geometry.vertices[triangle.vertices[1]],
             &c = geometry.vertices[triangle.vertices[2]];
  float d0 = 0, d1 = 0;
  for (size_t i = 0; i < 3; ++i) {
    if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || !std::isfinite(c[i]))
      return result;
    d0 += (start[i] - a[i]) * triangle.normal[i];
    d1 += (end[i] - a[i]) * triangle.normal[i];
  }
  const float normalSquared = triangle.normal[0] * triangle.normal[0] +
                              triangle.normal[1] * triangle.normal[1] +
                              triangle.normal[2] * triangle.normal[2];
  if (!std::isfinite(d0) || !std::isfinite(d1) ||
      !std::isfinite(normalSquared) || normalSquared == 0)
    return result;
  if (!(d1 > 0 && d0 <= 0)) {
    result.status = GamePhysicsHitStatus::Miss;
    return result;
  }
  if (std::fabs(d0) <= 0.02f && std::fabs(d1) <= 0.02f) {
    // 0049fa60 coplanar-graze branch (both ends within 0.02 of the plane):
    // clip the segment against the triangle in the two axes kept by 004a5c60
    // (the two smallest normal components). A hit reports the entry fraction.
    const auto &n = triangle.normal;
    size_t k0, k1;
    if (std::fabs(n[1]) < std::fabs(n[0])) {
      if (std::fabs(n[2]) <= std::fabs(n[0])) {
        k0 = 1;
        k1 = 2;
      } else {
        k0 = 0;
        k1 = 1;
      }
    } else if (std::fabs(n[2]) <= std::fabs(n[1])) {
      k0 = 0;
      k1 = 2;
    } else {
      k0 = 0;
      k1 = 1;
    }
    const float area = (b[k0] - a[k0]) * (c[k1] - a[k1]) -
                       (c[k0] - a[k0]) * (b[k1] - a[k1]);
    // _DAT_005f0c24 (the collinearity epsilon) is not read; a near-zero
    // projected area is treated as a miss like any degenerate clip.
    if (!std::isfinite(area) || std::fabs(area) < 1e-6f) {
      result.status = GamePhysicsHitStatus::Miss;
      return result;
    }
    // 0049fa60 literal: the edge value pairs each delta component with the
    // OTHER axis of the edge (a 2D cross), scaled by the signed area; the
    // f0 >= 0 side updates local_44 (max) and the f0 < 0 side updates
    // local_40 (min, missing when f1 < 0). The loop breaks when local_40 <=
    // local_44; surviving all three edges reports local_44. The interior
    // side therefore depends on the winding sign, which the literal port
    // preserves.
    const std::array<const std::array<float, 3> *, 3> verts{{&a, &b, &c}};
    float enter = 0, exit = 1;
    for (size_t i = 0; i < 3; ++i) {
      const auto &vi = *verts[i];
      const auto &vn = *verts[(i + 1) % 3];
      const float ex = (vn[k0] - vi[k0]) * area;
      const float ez = (vn[k1] - vi[k1]) * area;
      const float f0 =
          (start[k1] - vi[k1]) * ex - (start[k0] - vi[k0]) * ez;
      const float f1 = (end[k1] - vi[k1]) * ex - (end[k0] - vi[k0]) * ez;
      if (f0 >= 0) {
        const float t = f0 / (f0 - f1);
        if (enter < t)
          enter = t;
      } else {
        if (f1 < 0) {
          result.status = GamePhysicsHitStatus::Miss;
          return result;
        }
        const float t = f0 / (f0 - f1);
        if (t < exit)
          exit = t;
      }
      if (exit <= enter) {
        result.status = GamePhysicsHitStatus::Miss;
        return result;
      }
    }
    result.status = GamePhysicsHitStatus::Hit;
    result.fraction = enter;
    for (size_t i = 0; i < 3; ++i)
      result.point[i] = (start[i] - end[i]) * enter + start[i];
    result.normal = n;
    result.materialWord = triangle.materialWord;
    return result;
  }
  const float t = std::fabs(d0 / (d0 - d1));
  if (!(t > 0) || !std::isfinite(t))
    return result;
  std::array<float, 3> relative{};
  for (size_t i = 0; i < 3; ++i)
    relative[i] = (end[i] - start[i]) * t + start[i] - a[i];
  const auto &n = triangle.normal;
  size_t axis0, axis1;
  if (std::fabs(n[1]) < std::fabs(n[0])) {
    if (std::fabs(n[2]) <= std::fabs(n[0])) {
      axis0 = 1;
      axis1 = 2;
    } else {
      axis0 = 0;
      axis1 = 1;
    }
  } else if (std::fabs(n[2]) <= std::fabs(n[1])) {
    axis0 = 0;
    axis1 = 2;
  } else {
    axis0 = 0;
    axis1 = 1;
  }
  const float bx = b[axis0] - a[axis0], by = b[axis1] - a[axis1];
  const float cx = c[axis0] - a[axis0], cy = c[axis1] - a[axis1];
  const float determinant = cy * bx - cx * by;
  if (!std::isfinite(determinant) || determinant == 0)
    return result;
  const float u = (cy * relative[axis0] - cx * relative[axis1]) / determinant;
  const float v = (bx * relative[axis1] - by * relative[axis0]) / determinant;
  if (!std::isfinite(u) || !std::isfinite(v))
    return result;
  if (u < 0 || u > 1 || v < 0 || u + v > 1) {
    result.status = GamePhysicsHitStatus::Miss;
    return result;
  }
  result.status = GamePhysicsHitStatus::Hit;
  result.fraction = t;
  for (size_t i = 0; i < 3; ++i)
    result.point[i] = relative[i] + a[i];
  result.normal = n;
  result.materialWord = triangle.materialWord;
  return result;
}

ParentFaceMask queryParentFace(const GamePhysicsGeometry &geometry,
                               size_t faceIndex, const physics::Body &parent,
                               const ResourceBounds &bounds,
                               bool inheritedRejected) {
  using physics::Vec3;
  if (inheritedRejected)
    return ParentFaceMask::Rejected;
  if (!parent.proto || parent.current > 1 ||
      bool(parent.flags & physics::kBodyChild) != bool(parent.attachment) ||
      (parent.attachment && !parent.attachment->parent) ||
      (parent.flags & (parent.attachment ? 0x20020u : 0x24u)) ||
      (parent.proto->flags & (parent.attachment ? 0x20u : 0x24u)) ||
      !(parent.flags & physics::kBodyHasChildren) || bounds.mode != 1 ||
      faceIndex >= geometry.triangles.size())
    return ParentFaceMask::Unsupported;
  const auto &shape = parent.proto->shape;
  if (shape.type != physics::ShapeType::Box &&
      shape.type != physics::ShapeType::Mesh)
    return ParentFaceMask::Unsupported;
  const auto &current = parent.state[parent.current];
  const auto &predicted = parent.state[parent.current ^ 1];
  if (!std::isfinite(shape.boundRadius) || shape.boundRadius < 0)
    return ParentFaceMask::Unsupported;
  for (size_t i = 0; i < 3; ++i) {
    if (!std::isfinite(current.pos[i]) || !std::isfinite(predicted.pos[i]) ||
        !std::isfinite(bounds.values[i]) ||
        !std::isfinite(bounds.values[i + 3]) || bounds.values[i + 3] < 0)
      return ParentFaceMask::Unsupported;
    if (shape.boundRadius + bounds.values[i + 3] <
        std::fabs(predicted.pos[i] - bounds.values[i]))
      return ParentFaceMask::Rejected;
  }
  for (float v : predicted.rot)
    if (!std::isfinite(v))
      return ParentFaceMask::Unsupported;
  for (size_t i = 0; i < 3; ++i)
    for (size_t j = 0; j < 3; ++j) {
      float dot = 0;
      for (size_t k = 0; k < 3; ++k)
        dot += predicted.rot[4 * k + i] * predicted.rot[4 * k + j];
      if (std::fabs(dot - (i == j ? 1.0f : 0.0f)) > 0.0001f)
        return ParentFaceMask::Unsupported;
    }
  const auto &triangle = geometry.triangles[faceIndex];
  Vec3 vertices[3];
  for (size_t i = 0; i < 3; ++i) {
    if (triangle.vertices[i] >= geometry.vertices.size() ||
        !std::isfinite(triangle.normal[i]))
      return ParentFaceMask::Unsupported;
    vertices[i] = geometry.vertices[triangle.vertices[i]];
    for (float v : vertices[i])
      if (!std::isfinite(v))
        return ParentFaceMask::Unsupported;
  }
  const auto dot = [](const Vec3 &a, const Vec3 &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  const auto sub = [](const Vec3 &a, const Vec3 &b) {
    return Vec3{a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  };
  const auto cross = [](const Vec3 &a, const Vec3 &b) {
    return Vec3{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0]};
  };
  const auto &normal = triangle.normal;
  if (!(dot(normal, normal) > 0))
    return ParentFaceMask::Unsupported;
  const auto first = sub(predicted.pos, vertices[0]);
  const auto last = sub(current.pos, vertices[0]);
  const float p = dot(first, normal), q = dot(last, normal);
  if (p * q > 0 && shape.boundRadius < std::fabs(p) &&
      shape.boundRadius < std::fabs(q))
    return ParentFaceMask::Rejected;
  const float radiusSquared = shape.boundRadius * shape.boundRadius;
  for (size_t i = 0; i < 3; ++i) {
    const auto edge = sub(vertices[(i + 1) % 3], vertices[i]);
    const auto outward = cross(edge, normal);
    const float a = dot(sub(predicted.pos, vertices[i]), outward);
    const float b = dot(sub(current.pos, vertices[i]), outward);
    const float limit = radiusSquared * dot(edge, edge);
    if (a > 0 && b > 0 && limit < a * a && limit < b * b)
      return ParentFaceMask::Rejected;
  }
  Vec3 center{}, half{};
  if (shape.type == physics::ShapeType::Mesh) {
    if (!shape.core || shape.core->vertices.empty())
      return ParentFaceMask::Unsupported;
    Vec3 low = shape.core->vertices.front().position, high = low;
    for (const auto &vertex : shape.core->vertices)
      for (size_t i = 0; i < 3; ++i) {
        if (!std::isfinite(vertex.position[i]))
          return ParentFaceMask::Unsupported;
        low[i] = std::min(low[i], vertex.position[i]);
        high[i] = std::max(high[i], vertex.position[i]);
      }
    for (size_t i = 0; i < 3; ++i) {
      center[i] = (low[i] + high[i]) * 0.5f;
      half[i] = (high[i] - low[i]) * 0.5f;
    }
  } else {
    for (size_t i = 0; i < 3; ++i) {
      if (!std::isfinite(shape.extent[i]) || shape.extent[i] < 0)
        return ParentFaceMask::Unsupported;
      half[i] = shape.extent[i];
    }
  }
  const auto worldCenter =
      physics::transformPoint(predicted.pos, predicted.rot, center);
  Vec3 sweptCenter{}, halfMotion{};
  for (size_t i = 0; i < 3; ++i) {
    halfMotion[i] = (current.pos[i] - predicted.pos[i]) * 0.5f;
    sweptCenter[i] = worldCenter[i] + halfMotion[i];
  }
  const auto toLocal = [&](const Vec3 &v) {
    return Vec3{v[0] * predicted.rot[0] + v[1] * predicted.rot[4] +
                    v[2] * predicted.rot[8],
                v[0] * predicted.rot[1] + v[1] * predicted.rot[5] +
                    v[2] * predicted.rot[9],
                v[0] * predicted.rot[2] + v[1] * predicted.rot[6] +
                    v[2] * predicted.rot[10]};
  };
  const auto motionLocal = toLocal(halfMotion);
  for (size_t i = 0; i < 3; ++i)
    half[i] += std::fabs(motionLocal[i] * 1.5f) + physics::k::skin;
  Vec3 local[3];
  for (size_t i = 0; i < 3; ++i)
    local[i] = toLocal(sub(vertices[i], sweptCenter));
  const auto localNormal = toLocal(normal);
  const float projected = std::fabs(localNormal[0]) * half[0] +
                          std::fabs(localNormal[1]) * half[1] +
                          std::fabs(localNormal[2]) * half[2];
  if (projected < std::fabs(dot(local[0], localNormal)))
    return ParentFaceMask::Rejected;
  for (size_t i = 0; i < 3; ++i) {
    const float low = std::min(local[0][i], std::min(local[1][i], local[2][i]));
    const float high =
        std::max(local[0][i], std::max(local[1][i], local[2][i]));
    if (half[i] < low || high < -half[i])
      return ParentFaceMask::Rejected;
  }
  return ParentFaceMask::Accepted;
}

SectorNeighborTransition updateSectorNeighbor(bool linked, bool sameSector,
                                              const ResourceBounds &source,
                                              const ResourceBounds &target) {
  if (source.mode != 1 || target.mode != 1)
    return SectorNeighborTransition::Unsupported;
  bool overlap = true;
  for (size_t axis : {size_t{0}, size_t{2}}) {
    const float a = source.values[axis], b = target.values[axis];
    const float ra = source.values[axis + 3], rb = target.values[axis + 3];
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(ra) ||
        !std::isfinite(rb) || ra < 0 || rb < 0 || !std::isfinite(ra + rb) ||
        !std::isfinite(a - b))
      return SectorNeighborTransition::Unsupported;
    if (ra + rb < std::fabs(a - b))
      overlap = false;
  }
  if (!overlap)
    return linked ? SectorNeighborTransition::Remove
                  : SectorNeighborTransition::Keep;
  if (!linked && sameSector)
    return SectorNeighborTransition::Add;
  return SectorNeighborTransition::Keep;
}
