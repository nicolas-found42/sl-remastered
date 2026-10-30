#include "car_geometry.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  CarConfig config;
  CarBodyRecord render;
  render.kind = CarBodyRecord::Render;
  config.bodies.push_back(render);
  CarBodyRecord box;
  box.shape.kind = CarShapeKind::Box;
  box.shape.mass = 3;
  box.shape.dims = {1, 2, 3};
  box.geometryNoCollision = true;
  config.bodies.push_back(box);
  CarBodyRecord mesh;
  mesh.shape.kind = CarShapeKind::MeshFile;
  mesh.shape.meshPath = "test.scx";
  config.bodies.push_back(mesh);
  auto loader = [](const std::string& path, ScxModel& model, std::string&) {
    assert(path == "test.scx");
    model.version = 4;
    ScxMesh part;
    part.vertexCount = 3;
    part.positions = {0, 0, 0, 100, 0, 0, 0, 100, 0};
    part.indices = {0, 1, 2};
    model.meshes.push_back(part);
    return true;
  };
  std::vector<CarGeometryPrototype> out;
  std::string error;
  assert(buildCarGeometry(config, loader, out, error));
  assert(out.size() == 2 && out[0].bodyIndex == 1 && out[1].bodyIndex == 2);
  assert(out[0].body->shape.type == physics::ShapeType::Box);
  assert((out[0].body->flags & physics::kProtoCreated) == 0);
  assert(out[0].body->flags & physics::kProtoBit8);
  assert(out[1].body->shape.vertices[1][0] == 1);
  assert(out[1].body->shape.triangles.size() == 1 && out[1].body->shape.triangles[0].normal[2] == 1);
  config.bodies[2].shape.mass = 1;
  assert(!buildCarGeometry(config, loader, out, error));
  assert(out.size() == 1 && error.find("volume") != std::string::npos);
  config.bodies[2].shape.kind = CarShapeKind::MeshResource;
  assert(!buildCarGeometry(config, loader, out, error));
  assert(out.size() == 1 && error.find("unavailable") != std::string::npos);
  config.bodies[2].shape.meshResource = 0x1234;
  auto resourceLoader = [](uint32_t id, float mass, CarGeometryPrototype& result, std::string& detail) {
    assert(id == 0x1234 && mass == 1);
    auto body = physics::createBodyProto(mass, 5, 1, 2, 3, nullptr, detail);
    if (!body) return false;
    *result.body = std::move(*body);
    return true;
  };
  assert(buildCarGeometry(config, loader, out, error, resourceLoader));
  assert(out.size() == 2 && out[1].bodyIndex == 2 && out[1].body->invMass == 1);
  std::cout << "car geometry tests passed\n";
}
