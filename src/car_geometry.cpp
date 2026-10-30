#include "car_geometry.hpp"
#include "physics_mesh_mass.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

bool decodeCarCollisionMesh(const ScxModel& model, std::vector<physics::Vec3>& vertices,
                   std::vector<physics::MeshTriangle>& triangles, std::string& error) {
  if (model.version < 1 || model.version > 4) {
    error = "collision mesh SCX version is unsupported by stock loader";
    return false;
  }
  for (const auto& mesh : model.meshes) {
    if (mesh.positions.size() != static_cast<size_t>(mesh.vertexCount) * 3 ||
        mesh.indices.size() % 3 != 0 ||
        mesh.vertexCount > std::numeric_limits<uint32_t>::max() - vertices.size()) {
      error = "invalid collision mesh vertices or triangles";
      return false;
    }
    const auto base = static_cast<uint32_t>(vertices.size());
    for (size_t i = 0; i < mesh.positions.size(); i += 3) {
      vertices.push_back({mesh.positions[i] * 0.01f, mesh.positions[i + 1] * 0.01f,
                          mesh.positions[i + 2] * 0.01f});
    }
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
      if (mesh.indices[i] >= mesh.vertexCount || mesh.indices[i + 1] >= mesh.vertexCount ||
          mesh.indices[i + 2] >= mesh.vertexCount) {
        error = "collision mesh triangle index out of range";
        return false;
      }
      physics::MeshTriangle t{{base + mesh.indices[i], base + mesh.indices[i + 1],
                              base + mesh.indices[i + 2]}, {}};
      const auto& a = vertices[t.indices[0]];
      const auto& b = vertices[t.indices[1]];
      const auto& c = vertices[t.indices[2]];
      t.normal = {(c[2] - b[2]) * (b[1] - a[1]) - (c[1] - b[1]) * (b[2] - a[2]),
                  (b[2] - a[2]) * (c[0] - b[0]) - (c[2] - b[2]) * (b[0] - a[0]),
                  (c[1] - b[1]) * (b[0] - a[0]) - (b[1] - a[1]) * (c[0] - b[0])};
      const float length = std::sqrt(t.normal[1] * t.normal[1] + t.normal[2] * t.normal[2] + t.normal[0] * t.normal[0]);
      if (length != 0) {
        const float inverse = 1.0f / length;
        for (auto& n : t.normal) n *= inverse;
      }
      triangles.push_back(t);
    }
  }
  if (vertices.empty() || triangles.empty()) {
    error = "collision mesh has no vertices or triangles";
    return false;
  }
  return true;
}

bool buildCarGeometry(const CarConfig& config, const CarCollisionMeshLoader& loadMesh,
                      std::vector<CarGeometryPrototype>& result, std::string& error,
                      const CarCollisionResourceLoader& loadResource) {
  result.clear();
  error.clear();
  for (size_t i = 0; i < config.bodies.size(); ++i) {
    const auto& record = config.bodies[i];
    if (record.kind != CarBodyRecord::Body) continue;
    CarGeometryPrototype entry;
    entry.bodyIndex = i;
    entry.transform = record.transform;
    entry.bodyFlags = record.flags;
    const auto& shape = record.shape;
    std::vector<physics::Vec3> vertices;
    std::vector<physics::MeshTriangle> triangles;
    const std::vector<physics::Vec3>* meshVertices = nullptr;
    if (shape.kind == CarShapeKind::MeshResource) {
      CarGeometryPrototype resource;
      if (!loadResource) error = "collision geometry resource loader is unavailable";
      else if (loadResource(shape.meshResource, shape.mass, resource, error)) {
        entry.body = std::move(resource.body);
        entry.body->flags &= ~physics::kProtoCreated;
        if (record.geometryNoCollision) entry.body->flags |= physics::kProtoBit8;
        if (record.geometryBuoy) entry.body->flags |= physics::kProtoWater;
        result.push_back(std::move(entry));
        continue;
      } else if (error.empty()) error = "collision geometry resource load failed";
    } else if (shape.kind == CarShapeKind::MeshFile) {
      ScxModel model;
      if (!loadMesh) error = "collision mesh loader is unavailable";
      else if (loadMesh(shape.meshPath, model, error) && decodeCarCollisionMesh(model, vertices, triangles, error)) {
        meshVertices = &vertices;
      } else if (error.empty()) error = "collision mesh load failed";
    } else if (shape.kind == CarShapeKind::None) {
      error = "invalid car collision shape";
    }
    if (!error.empty()) { error = "body " + std::to_string(i) + ": " + error; return false; }
    auto proto = physics::createBodyProto(shape.mass, static_cast<uint32_t>(shape.kind), shape.dims[0],
                                         shape.dims[1], shape.dims[2], meshVertices, error);
    if (!proto) {
      error = "body " + std::to_string(i) + ": " + error;
      return false;
    }
    proto->shape.triangles = std::move(triangles);
    if (meshVertices) {
      auto lo = vertices.front(), hi = vertices.front();
      // 004b5c30 iterates from the first vertex through the penultimate one.
      for (size_t v = 0; v + 1 < vertices.size(); ++v) {
        for (size_t axis = 0; axis < 3; ++axis) {
          lo[axis] = std::min(lo[axis], vertices[v][axis]);
          hi[axis] = std::max(hi[axis], vertices[v][axis]);
        }
      }
      for (size_t axis = 0; axis < 3; ++axis) {
        proto->shape.extent[axis] = (hi[axis] - lo[axis]) * 0.5f;
        proto->shape.centerOffset[axis] = lo[axis] + proto->shape.extent[axis];
      }
    }
    if (!proto->massPropertiesSupported) {
      physics::Vec3 center;
      if (!physics::applyMeshMassProperties(*proto, proto->shape.triangles, center, error)) {
        error = "body " + std::to_string(i) + ": " + error;
        return false;
      }
      float radiusSquared = 0;
      for (const auto& v : proto->shape.vertices) {
        const float squared = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (squared > radiusSquared) radiusSquared = squared;
      }
      proto->shape.boundRadius = std::sqrt(radiusSquared) + physics::k::skin;
    }
    proto->flags &= ~physics::kProtoCreated;
    if (record.geometryNoCollision) proto->flags |= physics::kProtoBit8;
    if (record.geometryBuoy) proto->flags |= physics::kProtoWater;
    *entry.body = std::move(*proto);
    result.push_back(std::move(entry));
  }
  return true;
}
