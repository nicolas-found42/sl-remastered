#include "physics_contact_geometry.hpp"
#include <cassert>
using namespace physics;
int main() {
  BodyProto p, wall;
  p.invMass = 1;
  wall.flags = kProtoStatic;
  Body a, b;
  a.proto = &p;
  b.proto = &wall;
  a.state[1].pos = {1.03f, 0, 0};
  a.state[1].vel = {-2, 0, 0};
  DirectContact c{&a, &b};
  Vec3 point{};
  auto pair = sphereCoreClosest(a.state[1], b.state[1]);
  assert(updateDirectContactGeometry(c, pair, a.state[1], b.state[1], 0.5f,
                                     0.5f,
                                     point) == ContactGeometryResult::Updated);
  assert(c.pointA[0] == -0.5f && c.pointB[0] == 0.5f && c.normal[0] == 1);
  DirectContactWorld world;
  world.step = 0.01f;
  assert(correctDirectContact(c, world) == 1 && a.state[0].vel[0] > 0);
  auto before = c;
  auto beforePoint = point;
  pair.distance = 1.01f;
  assert(updateDirectContactGeometry(c, pair, a.state[1], b.state[1], 0.5f,
                                     0.5f, point) ==
         ContactGeometryResult::Unsupported);
  assert(c.pointA == before.pointA && c.separation == before.separation &&
         point == beforePoint);
  pair.distance = 1.1f;
  assert(updateDirectContactGeometry(c, pair, a.state[1], b.state[1], 0.5f,
                                     0.5f,
                                     point) == ContactGeometryResult::Dropped);
  assert(c.pointA == before.pointA && point == beforePoint);
  a.flags = kBodyActive;
  b.flags = 0;
  a.state[0].pos = {1.2f, 0, 0};
  a.state[1].pos = {0.9f, 0, 0};
  a.state[1].stepFlags = 3;
  ContactGeometryWorld geometryWorld;
  geometryWorld.stepReady = 5;
  auto query = [](const State &x, const State &y) -> std::optional<CorePair> {
    return sphereCoreClosest(x, y);
  };
  pair = query(a.state[1], b.state[1]).value();
  assert(updateDirectContactGeometryWithSearch(c, pair, 0.5f, 0.5f, point,
                                               geometryWorld, query) ==
         ContactGeometryResult::Updated);
  assert(geometryWorld.queries == 12 && geometryWorld.stepReady == 0 &&
         a.state[1].stepFlags == 2);
  before = c;
  beforePoint = point;
  auto saved = geometryWorld;
  a.state[0].quat = {0, 0, 2, 0};
  a.state[0].rot = matrixFromQuat(a.state[0].quat);
  assert(updateDirectContactGeometryWithSearch(c, pair, 0.5f, 0.5f, point,
                                               geometryWorld, query) ==
         ContactGeometryResult::Unsupported);
  assert(c.pointA == before.pointA && point == beforePoint &&
         geometryWorld.queries == saved.queries);
  BodyProto sphere, floor;
  sphere.invMass = 1;
  sphere.shape.type = ShapeType::Sphere;
  sphere.shape.radius = 0.5f;
  sphere.shape.core = std::make_shared<CollisionCore>(pointCore());
  sphere.shape.friction = 0.7f;
  sphere.shape.bounce = 0.4f;
  floor.flags = kProtoStatic;
  floor.shape.friction = 0.4f;
  floor.shape.bounce = 0.2f;
  Body ball, ground;
  ball.proto = &sphere;
  ball.flags = kBodyActive;
  ground.proto = &floor;
  ball.state[0].pos = ball.state[1].pos = {0, 0.53f, 0};
  const std::array<Vec3, 3> triangle{{{-2, 0, -2}, {0, 0, 2}, {2, 0, -2}}};
  GroundFacePair face;
  std::string error;
  assert(createGroundFacePair(ball, ground, triangle, {0, 1, 0}, face, error));
  assert(face.contact.a == &ball && face.contact.b == &ground);
  assert(face.contact.friction == 0.4f && face.contact.bounce == 0.3f);
  ContactGeometryWorld faceWorld;
  assert(updateGroundFacePair(face, faceWorld, point) ==
         ContactGeometryResult::Updated);
  assert(face.contact.separation > 0.029f && face.contact.separation < 0.031f);
  // Ground contacts carry the stored PHYS face normal (0049b830 constructs
  // them from the face); the aligned core normal passes the deviation check.
  assert(face.contact.normal == Vec3({0, 1, 0}));
  // Deviation check: aligned and anti-aligned pass (anti-aligned flips),
  // normals beyond 45 degrees from the face are rejected.
  const auto up = groundFaceContactNormal({0.1f, 1, 0}, {0, 1, 0});
  assert(up && *up == Vec3({0, 1, 0}));
  const auto down = groundFaceContactNormal({0, -1, 0.2f}, {0, 1, 0});
  assert(down && *down == Vec3({0, -1, 0}));
  assert(!groundFaceContactNormal({1, 0.7f, 0}, {0, 1, 0}));
  const auto at45 = groundFaceContactNormal({0.7071f, 0.7071f, 0}, {0, 1, 0});
  assert(at45 && *at45 == Vec3({0, 1, 0}));
  ball.state[1].pos[1] = 2;
  assert(updateGroundFacePair(face, faceWorld, point) ==
         ContactGeometryResult::Dropped);
  ground.state[0].pos[0] = 1;
  assert(updateGroundFacePair(face, faceWorld, point) ==
         ContactGeometryResult::Unsupported);
  Body first, second;
  first.proto = second.proto = &sphere;
  first.flags = second.flags = kBodyActive;
  first.state[0].pos = first.state[1].pos = {0, 1.03f, 0};
  DynamicBodyPair dynamic;
  assert(createDynamicBodyPair(first, second, dynamic, error));
  ContactGeometryWorld dynamicWorld;
  assert(updateDynamicBodyPair(dynamic, dynamicWorld, point) ==
         ContactGeometryResult::Updated);
  assert(dynamic.contact.separation > 0.029f &&
         dynamic.contact.separation < 0.031f);
  first.state[1].pos[1] = 2;
  assert(updateDynamicBodyPair(dynamic, dynamicWorld, point) ==
         ContactGeometryResult::Dropped);
  second.flags |= 0x40u;
  assert(updateDynamicBodyPair(dynamic, dynamicWorld, point) ==
         ContactGeometryResult::Unsupported);
}
