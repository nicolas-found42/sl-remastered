#include "wheel_tire_force.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::printf("FAIL %d: %s\n", __LINE__, #x);                              \
      ++failures;                                                              \
    }                                                                          \
  } while (0)
int main() {
  int failures = 0;
  auto load = tireEffectiveLoad({100, 50, 0, {0, 1, 0}});
  CHECK(load && *load == 50);
  load = tireEffectiveLoad({100, 50, 0, {1, 0, 0}});
  CHECK(load && *load == 25);
  load = tireEffectiveLoad({50, 50, 1, {0, 1, 0}});
  CHECK(load && std::fabs(*load - std::atan(0x1.921fb6p+0f) * 50 *
                                      0x1.45f306p-1f) < 1e-4f);
  CHECK(!tireEffectiveLoad({1, 0, 0, {0, 1, 0}}));
  physics::BodyProto proto{};
  physics::Body root{};
  root.proto = &proto;
  root.state[1].stepFlags = 1;
  physics::Body wheel{};
  wheel.proto = &proto;
  wheel.flags = physics::kBodyChild;
  wheel.attachment = physics::BodyAttachment{
      &root, physics::RelativeBodyPose{{1, 0, 0}, {0, 0, 0, 1}}};
  CHECK(routeTireForce(wheel, {0, 2, 0}, {0, 0, 0},
                       TireGeometryCorrections::None));
  CHECK(root.state[0].force[1] == 2 && root.state[0].torque[2] == 2 &&
        root.state[1].stepFlags == 0);
  root.state[0].force = {};
  root.state[0].torque = {};
  const physics::Mat34 quarterTurn{0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
  root.state[0].rot = quarterTurn;
  wheel.state[0].rot = quarterTurn;
  CHECK(routeTireForce(wheel, {0, 2, 0}, {0, 0, 0},
                       TireGeometryCorrections::None));
  CHECK(root.state[0].force[0] == -2 && root.state[0].force[1] == 0 &&
        root.state[0].torque[2] == 2);
  const auto before = root.state[0];
  CHECK(!routeTireForce(wheel, {0, 2, 0}, {0, 0, 0},
                        TireGeometryCorrections::RequiredOrUnknown));
  CHECK(root.state[0].force == before.force &&
        root.state[0].torque == before.torque);
  wheel.attachment->parent = &wheel;
  CHECK(!routeTireForce(wheel, {0, 2, 0}, {0, 0, 0},
                        TireGeometryCorrections::None));
  CHECK(root.state[0].force == before.force);
  wheel.attachment->parent = &root;
  root.state[0].rot = wheel.state[0].rot = physics::matrixIdentity();
  root.state[0].force = {};
  root.state[0].torque = {};
  TireForceGeometry geometry{{0, 0, 0}, {2, 2, 0}, {0, 3, 3}, 1, 1, 2};
  CHECK(routeTireForce(wheel, {2, 0, 3}, {0, 0, 0},
                       TireGeometryCorrections::Resolved, &geometry));
  CHECK(root.state[0].force == (physics::Vec3{2, 5, 3}));
  root.state[0].force = {};
  root.state[0].torque = {};
  geometry.instantCenterA[2] = 1000;
  geometry.instantCenterB[0] = 1000;
  CHECK(routeTireForce(wheel, {2, 0, 3}, {0, 0, 0},
                       TireGeometryCorrections::Resolved, &geometry));
  CHECK(root.state[0].force == (physics::Vec3{2, 0, 3}));
  return failures ? 1 : 0;
}
