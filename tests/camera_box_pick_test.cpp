#include "camera_box_pick.hpp"
#include <cassert>
#include <cmath>
int main() {
  const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  ViewportInstance viewport{};
  viewport.width = viewport.height = 1;
  PhysicsBox distant;
  distant.halfExtents = {0.01f, 0.01f, 0.01f};
  distant.position = {{{0, 0, 0.08f}, {0, 0, 0.08f}}};
  PhysicsBox near = distant;
  near.position = {{{0, 0, 0.04f}, {0, 0, 0.04f}}};
  auto hit = pickCameraBox({0, 0}, viewport, identity, identity, {distant, near});
  assert(hit && hit->index == 1 && std::abs(hit->fraction - 0.3f) < 0.0001f);
  assert(!pickCameraBox({0.5f, 0}, viewport, identity, identity, {near}));
  assert(!pickCameraBox({2, 0}, viewport, identity, identity, {near}));
  // Identical boxes stacked on the ray give exactly equal hit fractions;
  // the strict `<` keeps the EARLIER input index.
  PhysicsBox twinA = near, twinB = near;
  const auto tie = pickCameraBox({0, 0}, viewport, identity, identity, {twinA, twinB});
  assert(tie && tie->index == 0 && std::abs(tie->fraction - 0.3f) < 0.0001f);
  const auto tie2 = pickCameraBox({0, 0}, viewport, identity, identity, {twinB, twinA});
  assert(tie2 && tie2->index == 0 && std::abs(tie2->fraction - 0.3f) < 0.0001f);
  // Nearest still wins over a farther box of equal size.
  const auto ordered = pickCameraBox({0, 0}, viewport, identity, identity, {distant, near});
  assert(ordered && ordered->index == 1 && std::abs(ordered->fraction - 0.3f) < 0.0001f);
  auto invalid = viewport;
  invalid.width = 0;
  assert(!pickCameraBox({0, 0}, invalid, identity, identity, {near}));
  // FUN_0049deb0: transpose the current body pose before testing the actual
  // box dimensions. A 90-degree rotation moves its long axis onto the ray.
  physics::BodyProto proto;
  proto.shape.type = physics::ShapeType::Box;
  proto.shape.boundRadius = 1;
  proto.shape.dim[0] = 0.02f;
  proto.shape.dim[1] = 0.01f;
  proto.shape.dim[2] = 0.005f;
  auto body = physics::createBody(proto, {0, 0, 0.05f});
  body.state[0].rot = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0};
  std::string error;
  auto rotated = pickCameraShapes({0, 0}, viewport, identity, identity,
                                  {{nullptr, &body}}, error);
  assert(error.empty() && rotated && std::fabs(rotated->fraction - 0.3f) < 1e-6f);
  assert(std::fabs(rotated->worldContact[2] - 0.03f) < 1e-6f);
  assert(std::fabs(rotated->localContact[0] - 0.02f) < 1e-6f);
  // Actual rotated triangle geometry, not its enclosing sphere or AABB.
  proto.shape.type = physics::ShapeType::Hull;
  proto.shape.vertices = {{0, -0.02f, -0.02f}, {0, -0.02f, 0.02f}, {0, 0.02f, 0}};
  proto.shape.triangles = {{{0, 1, 2}, {1, 0, 0}}};
  auto mesh = pickCameraShapes({0, 0}, viewport, identity, identity,
                               {{nullptr, &body}}, error);
  assert(error.empty() && mesh && std::fabs(mesh->fraction - 0.5f) < 1e-6f);
  assert(!pickCameraShapes({0.15f, -0.15f}, viewport, identity, identity,
                           {{nullptr, &body}}, error) &&
         error.empty());
  proto.shape.triangles[0].normal = {-1, 0, 0};
  assert(!pickCameraShapes({0, 0}, viewport, identity, identity,
                           {{nullptr, &body}}, error) &&
         error.empty());
  proto.shape.triangles[0].normal = {1, 0, 0};
  body.flags |= 0x4000;
  assert(!pickCameraShapes({0, 0}, viewport, identity, identity,
                           {{nullptr, &body}}, error) &&
         error.empty());
  body.flags &= ~uint32_t{0x4000};
  auto mixed = pickCameraShapes({0, 0}, viewport, identity, identity,
                                {{nullptr, &body}, {&near, nullptr}}, error);
  assert(error.empty() && mixed && mixed->index == 1);
  proto.shape.triangles[0].indices[0] = 99;
  assert(!pickCameraShapes({0, 0}, viewport, identity, identity,
                           {{nullptr, &body}}, error) &&
         !error.empty());
  proto.shape.type = physics::ShapeType::Sphere;
  proto.shape.boundRadius = 0.01f;
  auto sphere = pickCameraShapes({0, 0}, viewport, identity, identity,
                                 {{nullptr, &body}}, error);
  assert(error.empty() && sphere && std::fabs(sphere->fraction - 0.4f) < 1e-6f);
  proto.shape.type = physics::ShapeType::CylinderX;
  assert(!pickCameraShapes({0, 0}, viewport, identity, identity,
                           {{nullptr, &body}}, error) &&
         error.empty());
}
