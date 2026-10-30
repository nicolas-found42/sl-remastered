#include "car_wheel_geometry.hpp"

#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cctype>

int main() {
  CarConfig config;
  CarWheel wheel;
  wheel.shape.kind = CarShapeKind::Sphere;
  wheel.shape.mass = 2;
  wheel.shape.dims = {0.3f, 0, 0};
  wheel.position = {1, 2, 3};
  wheel.wheelBones = true;
  wheel.hasWheel = true;
  wheel.wheelC = 2;
  wheel.wheelD = 3;
  wheel.armAnchor = {4, 5, 6};
  config.wheels.push_back(wheel);
  std::vector<CarWheelPrototype> out;
  std::string error;
  assert(buildCarWheelGeometry(config, {4, 5}, out, error));
  assert(out.size() == 1 && out[0].wheelIndex == 0);
  assert(out[0].wheelBody.shape.type == physics::ShapeType::Sphere);
  assert(out[0].wheelBody.shape.dim[0] == 0.3f);
  assert(out[0].boneBody && out[0].boneBody->shape.dim[0] == 0.1f);
  assert(out[0].wheelParameters[2] == 8 && out[0].wheelParameters[3] == 15);
  assert(out[0].position == wheel.position && out[0].armAnchor == wheel.armAnchor);
  assert(out[0].instantCenterA[2] == 10000 && out[0].armValue == -1);
  wheel.shape.kind = CarShapeKind::Box;
  wheel.shape.dims = {1, 2, 3};
  wheel.wheelBones = false;
  config.wheels.push_back(wheel);
  assert(buildCarWheelGeometry(config, {1, 1}, out, error));
  assert(out.size() == 2 && out[1].wheelBody.shape.type == physics::ShapeType::Box);
  assert(!out[1].boneBody);
  config.wheels[1].shape.kind = CarShapeKind::MeshFile;
  assert(!buildCarWheelGeometry(config, {1, 1}, out, error));
  assert(out.size() == 1 && error.find("wheel 1") != std::string::npos);
  CarConfig parsed;
  assert(parseCarConfig("type 2 sphere 0.3\nwheel 1 2 3 0 0 0 2 0\n", parsed, error));
  assert(buildCarWheelGeometry(parsed, {1, 1}, out, error));
  assert(out.size() == 1 && out[0].hasWheel);
  assert(out[0].wheelParameters[2] == 1 && out[0].wheelParameters[3] == 0);
  assert(out[0].bonePosition[1] == 0.4f && out[0].hubValue == -1);
  namespace fs = std::filesystem;
  const fs::path root = ".scratch/2005-extract/Program Files/Street Legal Racing - Redline/cars";
  if (fs::exists(root)) {
    size_t files = 0, wheels = 0;
    for (const auto& file : fs::recursive_directory_iterator(root)) {
      auto name = file.path().filename().string();
      for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (!file.is_regular_file() || name.rfind("_main", 0) != 0 || file.path().extension() != ".cfg") continue;
      std::ifstream input(file.path(), std::ios::binary);
      std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
      CarConfig car;
      assert(parseCarConfig(text, car, error));
      assert(buildCarWheelGeometry(car, {1, 1}, out, error));
      assert(out.size() == car.wheels.size());
      wheels += out.size();
      ++files;
    }
    assert(files == 39 && wheels == 158);
  }
  std::cout << "car wheel geometry tests passed\n";
}
