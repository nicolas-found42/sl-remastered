#include "camera_device_body.hpp"
#include "part_config.hpp"
#include <cmath>

std::shared_ptr<const physics::BodyProto>
makeCameraDevicePrototype(std::string &error) {
  error.clear();
  auto prototype = physics::createBodyProto(0.05f, 1, 0.15f, 0, 0, nullptr, error);
  if (!prototype) return {};
  prototype->linearDrag *= 10.0f;
  prototype->angularDrag = -0.2f / prototype->invMass;
  prototype->shape.friction = 0;
  prototype->flags &= ~physics::kProtoCreated;
  return std::make_shared<physics::BodyProto>(std::move(*prototype));
}

std::unique_ptr<CameraDeviceBody>
makeCameraDeviceBody(std::shared_ptr<const physics::BodyProto> prototype,
                     const CameraDeviceArgs &args, float creationTime,
                     std::string &error) {
  error.clear();
  if (!prototype) {
    error = "missing camera body prototype";
    return {};
  }
  PartSlotDirective pose{};
  for (size_t i = 0; i < 3; ++i) {
    pose.position[i] = args.position[i];
    pose.orientation[i] = args.orientation[i];
  }
  PartSlotState initial;
  if (!std::isfinite(creationTime) || !makePartSlotState(pose, initial)) {
    error = "invalid camera body pose";
    return {};
  }
  auto result = std::make_unique<CameraDeviceBody>(std::move(prototype));
  const auto &m = initial.rotation;
  const physics::Mat34 rotation{m[0], m[1], m[2], 0, m[3], m[4], m[5], 0,
                                m[6], m[7], m[8], 0};
  result->body = physics::createBody(*result->prototype, initial.position, &rotation, creationTime);
  result->body.flags |= 0x4002;
  if (!(args.flags & 0x10)) result->body.flags |= 0x20;
  return result;
}
