#pragma once

#include "car_config.hpp"
#include "physics_body.hpp"
#include "scx.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

struct CarGeometryPrototype {
  size_t bodyIndex = 0;
  CarTransform transform;
  uint32_t bodyFlags = 0;
  std::shared_ptr<physics::BodyProto> body = std::make_shared<physics::BodyProto>();
};

using CarCollisionMeshLoader = std::function<bool(const std::string&, ScxModel&, std::string&)>;
using CarCollisionResourceLoader = std::function<bool(uint32_t, float, CarGeometryPrototype&, std::string&)>;

bool decodeCarCollisionMesh(const ScxModel& model, std::vector<physics::Vec3>& vertices,
                            std::vector<physics::MeshTriangle>& triangles, std::string& error);

// On failure, retains the successfully built prefix and describes the first
// unsupported or invalid body. No live physics bodies are created here.
bool buildCarGeometry(const CarConfig& config, const CarCollisionMeshLoader& loadMesh,
                      std::vector<CarGeometryPrototype>& result, std::string& error,
                      const CarCollisionResourceLoader& loadResource = {});
