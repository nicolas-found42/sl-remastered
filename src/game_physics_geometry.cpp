#include "game_physics_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace {
float floatWord(uint32_t word) {
  float value;
  std::memcpy(&value,&word,sizeof value);
  return value;
}
}
bool decodeGamePhysicsGeometry(const GamePhysicsChunk &chunk,
                               GamePhysicsGeometry &out, std::string &error) {
  GamePhysicsGeometry result;
  result.vertices.reserve(chunk.triples.size());
  for (const auto &words : chunk.triples) {
    std::array<float,3> point{};
    for (size_t i=0;i<3;++i) {
      point[i]=floatWord(words[i]);
      if (!std::isfinite(point[i])) { error="PHYS: non-finite vertex"; return false; }
    }
    result.vertices.push_back(point);
  }
  result.triangles.reserve(chunk.records.size());
  for (const auto &words : chunk.records) {
    GamePhysicsTriangle triangle;
    triangle.materialWord=words[0];
    for (size_t i=0;i<3;++i) {
      triangle.vertices[i]=words[i+1];
      triangle.normal[i]=floatWord(words[i+4]);
      if (triangle.vertices[i]>=result.vertices.size()) { error="PHYS: vertex index out of range"; return false; }
      if (!std::isfinite(triangle.normal[i])) { error="PHYS: non-finite normal"; return false; }
    }
    result.triangles.push_back(triangle);
  }
  out=std::move(result);
  error.clear();
  return true;
}

GamePhysicsHit queryGamePhysicsFace(const GamePhysicsGeometry &geometry, size_t face,
                                    const std::array<float,3> &start,
                                    const std::array<float,3> &end) {
  GamePhysicsHit result;
  if (face >= geometry.triangles.size()) return result;
  const auto &triangle=geometry.triangles[face];
  for (size_t i=0;i<3;++i)
    if (triangle.vertices[i]>=geometry.vertices.size() || !std::isfinite(start[i]) ||
        !std::isfinite(end[i]) || !std::isfinite(triangle.normal[i])) return result;
  const auto &a=geometry.vertices[triangle.vertices[0]], &b=geometry.vertices[triangle.vertices[1]],
             &c=geometry.vertices[triangle.vertices[2]];
  float d0=0, d1=0;
  for (size_t i=0;i<3;++i) {
    if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || !std::isfinite(c[i])) return result;
    d0+=(start[i]-a[i])*triangle.normal[i];
    d1+=(end[i]-a[i])*triangle.normal[i];
  }
  const float normalSquared=triangle.normal[0]*triangle.normal[0] +
      triangle.normal[1]*triangle.normal[1] + triangle.normal[2]*triangle.normal[2];
  if (!std::isfinite(d0) || !std::isfinite(d1) || !std::isfinite(normalSquared) ||
      normalSquared==0) return result;
  if (!(d1>0 && d0<=0)) { result.status=GamePhysicsHitStatus::Miss; return result; }
  if (std::fabs(d0)<=0.02f && std::fabs(d1)<=0.02f) return result;
  const float t=std::fabs(d0/(d0-d1));
  if (!(t>0) || !std::isfinite(t)) return result;
  std::array<float,3> relative{};
  for (size_t i=0;i<3;++i) relative[i]=(end[i]-start[i])*t+start[i]-a[i];
  const auto &n=triangle.normal;
  size_t axis0, axis1;
  if (std::fabs(n[1])<std::fabs(n[0])) {
    if (std::fabs(n[2])<=std::fabs(n[0])) { axis0=1; axis1=2; }
    else { axis0=0; axis1=1; }
  } else if (std::fabs(n[2])<=std::fabs(n[1])) { axis0=0; axis1=2; }
  else { axis0=0; axis1=1; }
  const float bx=b[axis0]-a[axis0], by=b[axis1]-a[axis1];
  const float cx=c[axis0]-a[axis0], cy=c[axis1]-a[axis1];
  const float determinant=cy*bx-cx*by;
  if (!std::isfinite(determinant) || determinant==0) return result;
  const float u=(cy*relative[axis0]-cx*relative[axis1])/determinant;
  const float v=(bx*relative[axis1]-by*relative[axis0])/determinant;
  if (!std::isfinite(u) || !std::isfinite(v)) return result;
  if (u<0 || u>1 || v<0 || u+v>1) { result.status=GamePhysicsHitStatus::Miss; return result; }
  result.status=GamePhysicsHitStatus::Hit;
  result.fraction=t;
  for (size_t i=0;i<3;++i) result.point[i]=relative[i]+a[i];
  result.normal=n;
  result.materialWord=triangle.materialWord;
  return result;
}

ParentFaceMask queryStationaryParentFace(const GamePhysicsGeometry &geometry,
    size_t faceIndex, const physics::Body &parent, const ResourceBounds &bounds,
    bool inheritedRejected) {
  if (inheritedRejected) return ParentFaceMask::Rejected;
  if (!parent.proto || parent.current > 1 || parent.attachment ||
      (parent.flags & 0x24u) || (parent.proto->flags & 0x24u) ||
      !(parent.flags & physics::kBodyHasChildren) ||
      parent.proto->shape.type != physics::ShapeType::Box || bounds.mode != 1 ||
      faceIndex >= geometry.triangles.size()) return ParentFaceMask::Unsupported;
  const auto &state = parent.state[parent.current];
  const auto &predicted = parent.state[parent.current ^ 1];
  if (state.pos != predicted.pos || predicted.rot != physics::matrixIdentity())
    return ParentFaceMask::Unsupported;
  const auto &shape = parent.proto->shape;
  if (!std::isfinite(shape.boundRadius) || shape.boundRadius < 0)
    return ParentFaceMask::Unsupported;
  for (size_t i = 0; i < 3; ++i) {
    if (!std::isfinite(state.pos[i]) || !std::isfinite(bounds.values[i]) ||
        !std::isfinite(bounds.values[i+3]) || bounds.values[i+3] < 0 ||
        !std::isfinite(shape.extent[i]) || shape.extent[i] < 0)
      return ParentFaceMask::Unsupported;
    if (shape.boundRadius + bounds.values[i+3] <
        std::fabs(state.pos[i] - bounds.values[i])) return ParentFaceMask::Rejected;
  }
  const auto &triangle = geometry.triangles[faceIndex];
  std::array<physics::Vec3, 3> vertices{};
  for (size_t i = 0; i < 3; ++i) {
    if (triangle.vertices[i] >= geometry.vertices.size() || !std::isfinite(triangle.normal[i]))
      return ParentFaceMask::Unsupported;
    vertices[i] = geometry.vertices[triangle.vertices[i]];
    for (float value : vertices[i]) if (!std::isfinite(value)) return ParentFaceMask::Unsupported;
  }
  const auto dot = [](const physics::Vec3 &a, const physics::Vec3 &b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
  };
  const auto sub = [](const physics::Vec3 &a, const physics::Vec3 &b) {
    return physics::Vec3{a[0]-b[0], a[1]-b[1], a[2]-b[2]};
  };
  const auto cross = [](const physics::Vec3 &a, const physics::Vec3 &b) {
    return physics::Vec3{a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2],
                         a[0]*b[1]-a[1]*b[0]};
  };
  const auto relative = sub(state.pos, vertices[0]);
  const auto &normal = triangle.normal;
  const float normalLength = dot(normal, normal);
  if (!std::isfinite(normalLength) || normalLength == 0) return ParentFaceMask::Unsupported;
  if (std::fabs(dot(relative, normal)) > shape.boundRadius) return ParentFaceMask::Rejected;
  const float radiusSquared = shape.boundRadius * shape.boundRadius;
  for (size_t i = 0; i < 3; ++i) {
    const auto edge = sub(vertices[(i+1)%3], vertices[i]);
    const auto edgeNormal = cross(edge, normal);
    const float distance = dot(sub(state.pos, vertices[i]), edgeNormal);
    if (distance > 0 && radiusSquared * dot(edge, edge) < distance * distance)
      return ParentFaceMask::Rejected;
  }
  physics::Vec3 half{};
  for (size_t i = 0; i < 3; ++i) half[i] = shape.extent[i] + physics::k::skin;
  const float projected = std::fabs(normal[0])*half[0] +
                          std::fabs(normal[1])*half[1] + std::fabs(normal[2])*half[2];
  if (projected < std::fabs(dot(sub(vertices[0], state.pos), normal)))
    return ParentFaceMask::Rejected;
  for (size_t i = 0; i < 3; ++i) {
    const float low = std::min(vertices[0][i], std::min(vertices[1][i], vertices[2][i]));
    const float high = std::max(vertices[0][i], std::max(vertices[1][i], vertices[2][i]));
    if (state.pos[i] + half[i] < low || high < state.pos[i] - half[i])
      return ParentFaceMask::Rejected;
  }
  return ParentFaceMask::Accepted;
}
