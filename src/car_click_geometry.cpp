#include "car_click_geometry.hpp"
#include "physics_polyhedron.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace {
bool readMesh(const std::string& path, const CarCollisionMeshLoader& loader,
              std::vector<physics::Vec3>& vertices, std::vector<physics::MeshTriangle>& triangles,
              std::string& error) {
  ScxModel model;
  if (!loader || !loader(path, model, error)) {
    if (error.empty()) error = "collision mesh load failed";
    return false;
  }
  return decodeCarCollisionMesh(model, vertices, triangles, error);
}

void bounds(physics::Shape& shape) {
  auto lo = shape.vertices.front(), hi = lo;
  for (size_t i = 0; i + 1 < shape.vertices.size(); ++i)
    for (size_t axis = 0; axis < 3; ++axis) {
      lo[axis] = std::min(lo[axis], shape.vertices[i][axis]);
      hi[axis] = std::max(hi[axis], shape.vertices[i][axis]);
    }
  for (size_t axis = 0; axis < 3; ++axis) {
    shape.extent[axis] = (hi[axis] - lo[axis]) * 0.5f;
    shape.centerOffset[axis] = lo[axis] + shape.extent[axis];
  }
}
}

bool loadCarClickMesh(const std::string& path, const CarCollisionMeshLoader& loader,
                      CarGeometryPrototype& result, std::string& error) {
  error.clear(); CarGeometryPrototype out; std::vector<physics::Vec3> vertices;
  if (!readMesh(path, loader, vertices, out.body->shape.triangles, error)) return false;
  auto proto = physics::createBodyProto(0, 7, 0, 0, 0, &vertices, error);
  if (!proto) return false;
  proto->shape.triangles = std::move(out.body->shape.triangles);
  bounds(proto->shape);
  proto->flags = (proto->flags | 0x20u) & ~physics::kProtoCreated;
  *out.body = std::move(*proto); result = std::move(out); return true;
}

CarGeometryPrototype cloneGeometryResource(const CarGeometryPrototype& resource, float requestedMass) {
  CarGeometryPrototype out = resource;
  out.body = std::make_shared<physics::BodyProto>(*resource.body);
  auto& body = *out.body;
  body.flags = physics::kProtoCreated;
  body.invInertia = physics::matrixZero();
  body.inertia = physics::matrixZero();
  if (requestedMass == 0) {
    body.flags |= physics::kProtoStatic;
    body.invMass = body.linearDrag = body.angularDrag = 0;
  } else {
    body.invMass = 1.0f / requestedMass;
    body.linearDrag = requestedMass * physics::k::linearDragPerMass;
    body.angularDrag = requestedMass * physics::k::angularDragPerMass;
    const float inverse = physics::k::sphereInertiaFactor / requestedMass;
    body.invInertia[0] = body.invInertia[5] = body.invInertia[10] = inverse;
  }
  body.volume = body.shape.volume;
  body.massPropertiesSupported = true;
  return out;
}

bool loadCarGeometryResource(std::string_view payload, const CarCollisionMeshLoader& loader,
                             CarGeometryPrototype& result, std::string& error) {
  error.clear(); std::string path; int type = 7; float friction = 0.6f, bounce = 0.2f, skin = 0, mass = 0;
  std::istringstream lines{std::string(payload)}; std::string line;
  while (std::getline(lines, line)) {
    std::istringstream words(line); std::string key; words >> key;
    if (key == "shape") words >> path;
    else if (key == "type") words >> type;
    else if (key == "friction") words >> friction;
    else if (key == "coll_coeff") words >> bounce;
    else if (key == "skin") words >> skin;
    else if (key == "mass") words >> mass;
  }
  if (path.empty()) { error = "physics resource has no shape"; return false; }
  if (type != 6 && type != 7) { error = "physics resource shape type is unsupported"; return false; }
  CarGeometryPrototype out; std::vector<physics::Vec3> vertices;
  if (!readMesh(path, loader, vertices, out.body->shape.triangles, error)) return false;
  physics::BodyProto proto;
  proto.shape.type = static_cast<physics::ShapeType>(type);
  proto.shape.vertices = std::move(vertices);
  proto.shape.triangles = std::move(out.body->shape.triangles);
  proto.shape.friction = friction; proto.shape.bounce = bounce; proto.shape.radius = skin;
  bounds(proto.shape);
  float maxSquared = 0;
  for (const auto& v : proto.shape.vertices) maxSquared = std::max(maxSquared, v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
  proto.shape.boundRadius = std::sqrt(maxSquared) + physics::k::skin;
  if (type == 6) {
    std::vector<std::vector<std::size_t>> faces;
    for (const auto& triangle : proto.shape.triangles)
      faces.push_back({triangle.indices[0], triangle.indices[1], triangle.indices[2]});
    auto core = std::make_shared<physics::CollisionCore>();
    if (!physics::meshCore(proto.shape.vertices, faces, *core, error)) return false;
    proto.shape.core = std::move(core);
  }
  *out.body = std::move(proto);
  result = cloneGeometryResource(out, mass);
  return true;
}
