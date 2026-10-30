#include "camera_device_body.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

int main() {
  static_assert(!std::is_copy_constructible_v<CameraDeviceBody>);
  static_assert(!std::is_move_constructible_v<CameraDeviceBody>);
  const auto args = parseCameraDeviceArgs("1,2,3,0.5,0.2,-0.1,0", 0.2f, 200);
  std::string error;
  auto prototype = makeCameraDevicePrototype(error);
  if (!prototype) return 1;
  std::weak_ptr<const physics::BodyProto> observed = prototype;
  auto camera = makeCameraDeviceBody(prototype, args, 12, error);
  if (!camera || !error.empty()) return 1;
  const auto &proto = *camera->prototype;
  const auto &body = camera->body;
  if (body.proto != &proto || proto.shape.type != physics::ShapeType::Sphere ||
      proto.shape.radius != 0.15f || proto.invMass != 20.0f ||
      proto.shape.friction != 0 || (proto.flags & physics::kProtoCreated) ||
      std::fabs(proto.linearDrag + 0.0025f) > 0.000001f ||
      std::fabs(proto.angularDrag + 0.01f) > 0.000001f ||
      (body.flags & 0x4022) != 0x4022) return 2;
  if (body.state[0].pos != args.position || body.state[1].pos != args.position ||
      body.state[0].rot != body.state[1].rot || body.state[0].time != 12 ||
      body.state[0].rot == physics::matrixIdentity()) return 3;
  auto moved = std::move(camera);
  if (moved->body.proto != moved->prototype.get()) return 4;
  auto noExtraFlag = args;
  noExtraFlag.flags = 0x10;
  auto other = makeCameraDeviceBody(prototype, noExtraFlag, 0, error);
  if (!other || (other->body.flags & 0x20) ||
      other->body.proto != moved->body.proto) return 5;
  prototype.reset();
  moved.reset();
  if (observed.expired() || other->body.proto != other->prototype.get() ||
      other->body.proto->shape.radius != 0.15f) return 6;
  noExtraFlag.position[0] = std::numeric_limits<float>::infinity();
  if (makeCameraDeviceBody(other->prototype, noExtraFlag, 0, error) || error.empty()) return 7;
  other.reset();
  if (!observed.expired()) return 8;
  std::puts("camera_device_body_test: ok");
}
