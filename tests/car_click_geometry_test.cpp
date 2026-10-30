#include "car_click_geometry.hpp"
#include "rpk.hpp"
#include "physics_polyhedron.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>

int main() {
  const std::string root = ".scratch/2005-extract/Program Files/Street Legal Racing - Redline/";
  const std::string packPath = root + "cars/racers/Baiern.rpk";
  auto pack = rpkLoadFile(packPath); assert(pack.ok);
  const RpkEntry* entry = nullptr;
  for (const auto& e : pack.pack.entries) if (e.typeId == 0x12c) entry = &e;
  assert(entry && entry->resType == 9);
  std::ifstream file(packPath, std::ios::binary);
  file.seekg(entry->payloadOffset); std::string payload(entry->payloadSize, '\0');
  file.read(payload.data(), static_cast<std::streamsize>(payload.size())); assert(file);
  auto loader = [&](const std::string& source, ScxModel& model, std::string& error) {
    std::string path = source; std::replace(path.begin(), path.end(), '\\', '/');
    auto parsed = scxLoadFile(root + path);
    if (!parsed.ok) { error = parsed.error; return false; }
    model = std::move(parsed.model); return true;
  };
  CarGeometryPrototype result; std::string error;
  assert(loadCarGeometryResource(payload, loader, result, error));
  assert(result.body->shape.type == physics::ShapeType::Hull);
  assert(result.body->flags == (physics::kProtoStatic | physics::kProtoCreated));
  auto dynamic = cloneGeometryResource(result, 5.0f);
  assert(dynamic.body.get() != result.body.get());
  assert(result.body->flags == (physics::kProtoStatic | physics::kProtoCreated));
  assert(dynamic.body->flags == physics::kProtoCreated);
  assert(dynamic.body->invMass == 0.2f && dynamic.body->invInertia[0] == 0.5f);
  assert(std::fabs(dynamic.body->linearDrag + 0.025f) < 1e-6f && std::fabs(dynamic.body->angularDrag + 0.05f) < 1e-6f);
  assert(dynamic.body->shape.vertices == result.body->shape.vertices);
  assert(!result.body->shape.vertices.empty() && !result.body->shape.triangles.empty());
  ScxModel original; assert(loader("cars\\racers\\Baiern_data\\meshes\\chassis.scx", original, error));
  size_t vertices = 0, indices = 0;
  for (const auto& mesh : original.meshes) { vertices += mesh.vertexCount; indices += mesh.indices.size(); }
  assert(result.body->shape.vertices.size() == vertices);
  assert(result.body->shape.triangles.size() * 3 == indices);
  std::cout << "Baiern 0x12c: " << vertices << " vertices, " << result.body->shape.triangles.size() << " triangles\n";
  assert(loadCarClickMesh("cars\\racers\\Baiern_data\\meshes\\chassis.scx", loader, result, error));
  assert(result.body->shape.vertices.size() == vertices && result.body->shape.triangles.size() * 3 == indices);
  assert((result.body->flags & 0x20u) && !(result.body->flags & physics::kProtoCreated));
  auto tetraLoader = [](const std::string&, ScxModel& model, std::string&) {
    model.version = 4;
    ScxMesh mesh; mesh.vertexCount = 4;
    mesh.positions = {0,0,0, 100,0,0, 0,100,0, 0,0,100};
    mesh.indices = {0,2,1, 0,1,3, 0,3,2, 1,2,3};
    model.meshes.push_back(mesh); return true;
  };
  assert(loadCarGeometryResource("type 6\nshape tetra.scx\nmass 10\nfriction 0.8\ncoll_coeff 0.3\nskin 0.1\n", tetraLoader, result, error));
  assert(result.body->shape.type == physics::ShapeType::Mesh && result.body->shape.core);
  assert(result.body->shape.core->faces.size() == 4 && result.body->shape.triangles.size() == 4);
  assert(result.body->invMass == 0.1f && result.body->invInertia[0] == 0.25f);
  assert(result.body->shape.friction == 0.8f && result.body->shape.bounce == 0.3f && result.body->shape.radius == 0.1f);
  assert(result.body->shape.extent[2] == 0);
  auto staticClone = cloneGeometryResource(result, 0);
  assert(staticClone.body->flags == (physics::kProtoCreated | physics::kProtoStatic));
  assert(staticClone.body->invMass == 0 && staticClone.body->shape.core == result.body->shape.core);
  assert(!loadCarGeometryResource("type 5\nshape tetra.scx\n", tetraLoader, result, error));
}
