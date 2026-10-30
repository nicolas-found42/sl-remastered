#include "car_wheel_geometry.hpp"

#include <utility>

bool buildCarWheelGeometry(const CarConfig& config, const std::array<float, 2>& partWheelFactors,
                           std::vector<CarWheelPrototype>& result, std::string& error) {
  result.clear();
  error.clear();
  for (size_t i = 0; i < config.wheels.size(); ++i) {
    const auto& wheel = config.wheels[i];
    if (wheel.shape.kind != CarShapeKind::Sphere && wheel.shape.kind != CarShapeKind::Box) {
      error = "wheel " + std::to_string(i) + ": collision mesh or unsupported shape is not implemented";
      return false;
    }
    auto body = physics::createBodyProto(wheel.shape.mass, static_cast<uint32_t>(wheel.shape.kind),
                                        wheel.shape.dims[0], wheel.shape.dims[1], wheel.shape.dims[2],
                                        nullptr, error);
    if (!body) {
      error = "wheel " + std::to_string(i) + ": " + error;
      return false;
    }
    CarWheelPrototype entry;
    entry.wheelIndex = i;
    entry.wheelBody = std::move(*body);
    entry.wheelBody.flags &= ~physics::kProtoCreated;
    if (wheel.wheelBones) {
      entry.boneBody = physics::createBodyProto(1, static_cast<uint32_t>(physics::ShapeType::Sphere),
                                                 0.1f, 0, 0, nullptr, error);
      if (!entry.boneBody) {
        error = "wheel " + std::to_string(i) + ": " + error;
        return false;
      }
    }
    entry.position = wheel.position;
    entry.orientation = wheel.orientation;
    entry.friction = wheel.friction;
    entry.spring = wheel.spring;
    entry.wheelParameters = {wheel.wheelA, wheel.wheelB, wheel.wheelC * partWheelFactors[0],
                             wheel.wheelD * partWheelFactors[1]};
    entry.bonePosition = wheel.bonePos;
    entry.wheelDeform = wheel.wheelDeform;
    entry.armValue = wheel.armValue;
    entry.armAnchor = wheel.armAnchor;
    entry.armDirection = wheel.armDirection;
    entry.hubValue = wheel.hubValue;
    entry.hubValues = wheel.hubValues;
    entry.hubAnchor = wheel.hubAnchor;
    entry.instantCenterA = wheel.instantCenterA;
    entry.instantCenterB = wheel.instantCenterB;
    entry.hasWheel = wheel.hasWheel;
    result.push_back(std::move(entry));
  }
  return true;
}
