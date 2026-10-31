#include "physics_polyhedron.hpp"

#include <algorithm>
#include <cmath>

namespace physics {
namespace {
Vec3 subtract(const Vec3 &a, const Vec3 &b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Vec3 normalize(Vec3 a) {
  const float length = std::sqrt(a[0] * a[0] + a[2] * a[2] + a[1] * a[1]);
  if (length != 0.0f)
    for (float &x : a)
      x *= 1.0f / length;
  return a;
}
Vec3 normalizeMeshEdge(Vec3 a) {
  const float length = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  if (length != 0.0f)
    for (float &x : a)
      x *= 1.0f / length;
  return a;
}
Vec3 normalizeFace(Vec3 a) {
  const float length = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  if (length != 0.0f)
    for (float &x : a)
      x *= 1.0f / length;
  return a;
}
Vec3 cross(const Vec3 &a, const Vec3 &b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
constexpr std::array<std::array<std::size_t, 4>, 6> boxFaces{{{{0, 2, 3, 1}},
                                                              {{4, 5, 7, 6}},
                                                              {{0, 1, 5, 4}},
                                                              {{7, 3, 2, 6}},
                                                              {{1, 3, 7, 5}},
                                                              {{0, 4, 6, 2}}}};

} // namespace

static bool buildCore(const std::vector<Vec3> &vertices,
                      const std::vector<std::vector<std::size_t>> &faces,
                      std::size_t capacity, CollisionCore &output,
                      std::string &error,
                      const std::vector<Vec3> *faceNormals = nullptr) {
  error.clear();
  for (const auto &face : faces) {
    if (face.size() < 3 || face.size() > 7) {
      error = "collision face must have three to seven vertices";
      return false;
    }
    for (auto index : face)
      if (index >= vertices.size()) {
        error = "collision face vertex index out of range";
        return false;
      }
  }
  CollisionCore core;
  for (const auto &vertex : vertices)
    core.vertices.push_back({vertex, {}});
  // 004ac150 reserves this many feature records, with a special case for
  // a two-triangle quadrilateral. 004ab8d0 returns failure when it fills.
  const auto missing = static_cast<std::size_t>(-1);
  std::vector<std::size_t> vertexFeatures(vertices.size(), missing),
      edgeFeatures;
  auto feature = [&](CoreFeatureKind kind, std::size_t index) {
    if (core.features.size() == capacity)
      return missing;
    core.features.push_back({{kind, index}, {}});
    return core.features.size() - 1;
  };
  auto link = [&](std::size_t from, CoreFeatureRef to, std::size_t anchor,
                  Vec3 normal) {
    core.features[from].adjacent.push_back({to, anchor, normal});
  };
  auto negate = [](Vec3 v) {
    for (float &n : v)
      n = -n;
    return v;
  };
  // 004ab8d0 stops adding records the moment the feature count reaches the
  // 004ac150 reservation and returns failure; 004a5e80 ignores that failure
  // and the shape keeps the well-formed-record prefix (the "built" flag is
  // already set). Mirror that: stop at the next capacity check instead of
  // failing, and keep the partial core.
  bool full = false;
  for (const auto &indices : faces) {
    const auto faceIndex = core.faces.size();
    const auto faceFeature = feature(CoreFeatureKind::Face, faceIndex);
    if (faceFeature == missing) {
      full = true;
      break;
    }
    const Vec3 a = subtract(vertices[indices[1]], vertices[indices[0]]);
    const Vec3 b = subtract(vertices[indices[2]], vertices[indices[0]]);
    const Vec3 normal =
        faceNormals ? (*faceNormals)[faceIndex] : normalizeFace(cross(a, b));
    core.faces.push_back({indices, {}, normal});
    // The stock point used to orient each edge plane is inside the first
    // triangle.
    Vec3 inside;
    for (std::size_t j = 0; j < 3; ++j)
      inside[j] = (b[j] * 0.5f - a[j] * 0.5f) * 0.5f + a[j] * 0.5f +
                  vertices[indices[0]][j];
    for (std::size_t i = 0; i < indices.size(); ++i) {
      const auto from = indices[i], to = indices[(i + 1) % indices.size()];
      for (auto v : {from, to})
        if (vertexFeatures[v] == missing) {
          vertexFeatures[v] = feature(CoreFeatureKind::Vertex, v);
          if (vertexFeatures[v] == missing) {
            full = true;
            break;
          }
        }
      if (full)
        break;
      auto edge = std::find_if(
          core.edges.begin(), core.edges.end(), [from, to](const CoreEdge &e) {
            return (e.vertices[0] == from && e.vertices[1] == to) ||
                   (e.vertices[0] == to && e.vertices[1] == from);
          });
      const auto ei = static_cast<std::size_t>(edge - core.edges.begin());
      if (edge == core.edges.end()) {
        const auto ef = feature(CoreFeatureKind::Edge, ei);
        if (ef == missing) {
          full = true;
          break;
        }
        edgeFeatures.push_back(ef);
        const auto direction =
            normalizeMeshEdge(subtract(vertices[to], vertices[from]));
        core.edges.push_back({{from, to}, direction, {}});
        core.vertices[from].edges.push_back(ei);
        core.vertices[to].edges.push_back(ei);
        link(vertexFeatures[from], {CoreFeatureKind::Edge, ei}, from,
             negate(direction));
        link(ef, {CoreFeatureKind::Vertex, from}, from, direction);
        link(vertexFeatures[to], {CoreFeatureKind::Edge, ei}, to, direction);
        link(ef, {CoreFeatureKind::Vertex, to}, to, negate(direction));
      }
      auto &e = core.edges[ei];
      e.faces.push_back(faceIndex);
      core.faces[faceIndex].edges.push_back(ei);
      Vec3 side = cross(normal, e.direction);
      const auto offset = subtract(inside, vertices[e.vertices[0]]);
      if (offset[0] * side[0] + offset[1] * side[1] + offset[2] * side[2] <
          0.0f)
        side = negate(side);
      link(edgeFeatures[ei], {CoreFeatureKind::Face, faceIndex}, e.vertices[0],
           negate(side));
      link(faceFeature, {CoreFeatureKind::Edge, ei}, e.vertices[0], side);
    }
    if (full)
      break;
  }
  (void)full;
  output = std::move(core);
  return true;
}

bool meshCore(const std::vector<Vec3> &vertices,
              const std::vector<std::vector<std::size_t>> &faces,
              CollisionCore &output, std::string &error) {
  const std::size_t capacity =
      vertices.size() + faces.size() +
      ((vertices.size() == 4 && faces.size() == 2) ? 5 : faces.size() * 3 / 2);
  return buildCore(vertices, faces, capacity, output, error);
}
bool triangleCore(const std::array<Vec3, 3> &vertices, const Vec3 &normal,
                  CollisionCore &output, std::string &error) {
  for (const auto &vertex : vertices)
    for (float value : vertex)
      if (!std::isfinite(value)) {
        error = "non-finite triangle vertex";
        return false;
      }
  for (float value : normal)
    if (!std::isfinite(value)) {
      error = "non-finite triangle normal";
      return false;
    }
  const Vec3 geometric = cross(subtract(vertices[1], vertices[0]),
                               subtract(vertices[2], vertices[0]));
  const float area2 = geometric[0] * geometric[0] +
                      geometric[1] * geometric[1] + geometric[2] * geometric[2];
  const float normal2 =
      normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
  const Vec3 parallel = cross(geometric, normal);
  const float parallel2 = parallel[0] * parallel[0] +
                          parallel[1] * parallel[1] + parallel[2] * parallel[2];
  if (!std::isfinite(area2) || !std::isfinite(normal2) ||
      !std::isfinite(parallel2) || area2 <= 0 ||
      std::fabs(normal2 - 1.0f) > 0.0001f || parallel2 > area2 * 0.0001f) {
    error = "invalid triangle geometry";
    return false;
  }
  const std::vector<Vec3> points(vertices.begin(), vertices.end());
  const std::vector<std::vector<std::size_t>> faces{{0, 1, 2}, {0, 1, 2}};
  const std::vector<Vec3> normals{normal, {-normal[0], -normal[1], -normal[2]}};
  return buildCore(points, faces, 8, output, error, &normals);
}

CollisionCore boundsVertices(const Vec3 &low, const Vec3 &high) {
  CollisionCore core;
  for (unsigned i = 0; i < 8; ++i) {
    core.vertices.push_back(
        {{(i & 1) ? high[0] : low[0], (i & 2) ? high[1] : low[1],
          (i & 4) ? high[2] : low[2]},
         {}});
  }
  return core;
}
CollisionCore boundsCore(const Vec3 &low, const Vec3 &high) {
  CollisionCore core = boundsVertices(low, high);
  std::vector<Vec3> vertices;
  for (const auto &vertex : core.vertices)
    vertices.push_back(vertex.position);
  std::vector<std::vector<std::size_t>> faces;
  for (const auto &face : boxFaces)
    faces.emplace_back(face.begin(), face.end());
  std::string error;
  buildCore(vertices, faces, 26, core, error);
  return core;
}
CollisionCore boxCore(const Vec3 &halfExtents) {
  return boundsCore({-halfExtents[0], -halfExtents[1], -halfExtents[2]},
                    halfExtents);
}
CollisionCore pointCore() {
  return {{{{0, 0, 0}, {}}}, {}, {}, {{{CoreFeatureKind::Vertex, 0}, {}}}};
}
CollisionCore segmentCore(const Vec3 &first, const Vec3 &second) {
  CollisionCore core;
  core.vertices = {{first, {0}}, {second, {0}}};
  core.edges.push_back({{0, 1}, normalize(subtract(second, first)), {}});
  const auto direction = core.edges[0].direction;
  Vec3 negative = direction;
  for (auto &v : negative)
    v = -v;
  core.features = {{{CoreFeatureKind::Vertex, 0},
                    {{{CoreFeatureKind::Edge, 0}, 0, negative}}},
                   {{CoreFeatureKind::Vertex, 1},
                    {{{CoreFeatureKind::Edge, 0}, 1, direction}}},
                   {{CoreFeatureKind::Edge, 0},
                    {{{CoreFeatureKind::Vertex, 0}, 0, direction},
                     {{CoreFeatureKind::Vertex, 1}, 1, negative}}}};
  return core;
}

bool buildShapeCore(const BodyProto &proto,
                    std::shared_ptr<const CollisionCore> &core,
                    std::string &error) {
  const Shape &shape = proto.shape;
  std::shared_ptr<const CollisionCore> built;
  switch (shape.type) {
  case ShapeType::Sphere:
    built = std::make_shared<CollisionCore>(pointCore());
    break;
  case ShapeType::CylinderX:
    built = std::make_shared<CollisionCore>(
        segmentCore({-shape.dim[0], 0, 0}, {shape.dim[0], 0, 0}));
    break;
  case ShapeType::CylinderY:
    built = std::make_shared<CollisionCore>(
        segmentCore({0, -shape.dim[0], 0}, {0, shape.dim[0], 0}));
    break;
  case ShapeType::CylinderZ:
    built = std::make_shared<CollisionCore>(
        segmentCore({0, 0, -shape.dim[0]}, {0, 0, shape.dim[0]}));
    break;
  case ShapeType::Box:
    built = std::make_shared<CollisionCore>(
        boxCore({shape.dim[0], shape.dim[1], shape.dim[2]}));
    break;
  case ShapeType::Mesh: {
    if (shape.vertices.empty() || shape.triangles.empty()) {
      error = "mesh shape has no vertices or faces for a collision core";
      return false;
    }
    std::vector<std::vector<std::size_t>> faces;
    faces.reserve(shape.triangles.size());
    for (const auto &triangle : shape.triangles)
      faces.push_back({triangle.indices[0], triangle.indices[1],
                       triangle.indices[2]});
    auto mesh = std::make_shared<CollisionCore>();
    if (!meshCore(shape.vertices, faces, *mesh, error))
      return false;
    built = std::move(mesh);
    break;
  }
  default:
    break;
  }
  core = std::move(built);
  error.clear();
  return true;
}

} // namespace physics
