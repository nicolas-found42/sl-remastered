#include "physics_contact_geometry.hpp"
#include "physics_core_pair.hpp"
#include <bit>
#include <cassert>
#include <cstring>
using namespace physics;
int main() {
  {
    // Constructor-start original 0049ec00, step39 cache and first step40 row.
    // first-input analysis 43038a54...a364; original/repeat64/control32 agree.
    // Preserve native pending/Created flags and ground friction .6 here;
    // the real object-world fixture independently exercises Port's flags/material.
    const uint32_t cacheStates[] = {
        1075838976u,
        1051003004u,
        0u,
        0u,
        3234918575u,
        0u,
        0u,
        0u,
        0u,
        1059760811u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        1067030938u,
        0u,
        0u,
        0u,
        0u,
        1067030938u,
        0u,
        0u,
        0u,
        0u,
        1067030938u,
        0u,
        1075838976u,
        1054608928u,
        0u,
        0u,
        3234576803u,
        0u,
        0u,
        0u,
        0u,
        1059481191u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        1067030938u,
        0u,
        0u,
        0u,
        0u,
        1067030938u,
        0u,
        0u,
        0u,
        0u,
        1067030938u,
        0u,
    };
    const uint32_t landingStates[] = {
        1075838976u,
        1051003004u,
        0u,
        0u,
        3234918575u,
        0u,
        0u,
        0u,
        0u,
        1059760811u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        1067030938u,
        0u,
        0u,
        0u,
        0u,
        1067030938u,
        0u,
        0u,
        0u,
        0u,
        1067030938u,
        0u,
        1075838976u,
        1046035887u,
        0u,
        0u,
        3235260317u,
        0u,
        0u,
        0u,
        0u,
        1060040431u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        1067030938u,
        0u,
        0u,
        0u,
        0u,
        1067030938u,
        0u,
        0u,
        0u,
        0u,
        1067030938u,
        0u,
    };
    const uint32_t groundStates[] = {
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        1065353216u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
    };
    const uint32_t expectedContact[] = {
        1048576000u,
        3196059648u,
        3196059648u,
        1076887552u,
        0u,
        3196059650u,
        0u,
        1065353216u,
        912786740u,
        1015888544u,
        1058642330u,
        1045220557u,
        0u,
        0u,
        0u,
    };

    std::string error;
    auto proto = createBodyProto(20, 5, .25f, .25f, .25f, nullptr, error);
    auto staticProto = createBodyProto(0, 0, 0, 0, 0, nullptr, error);
    assert(proto && staticProto);
    assert(buildShapeCore(*proto, proto->shape.core, error));
    Body parent, body, ground;
    parent.proto = &*proto;
    parent.flags = 1033;
    body.proto = &*proto;
    body.flags = 1029;
    body.attachment = BodyAttachment{&parent, {}};
    body.current = 1;
    std::memcpy(body.state, cacheStates, sizeof cacheStates);
    ground.proto = &*staticProto;
    std::memcpy(ground.state, groundStates, sizeof groundStates);
    const std::array<std::array<Vec3, 3>, 4> vertices{{{{{2, 0, -1}, {3, 0, 0}, {3, 0, -1}}},
                                                       {{{2, 0, -1}, {2, 0, 0}, {3, 0, 0}}},
                                                       {{{2, 0, 0}, {3, 0, 1}, {3, 0, 0}}},
                                                       {{{2, 0, 0}, {2, 0, 1}, {3, 0, 1}}}}};
    std::array<GroundFacePair, 4> pairs;
    std::vector<GroundFacePair*> candidates;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
      assert(createGroundFacePair(body, ground, vertices[i], {0, 1, 0}, pairs[i], error));
      candidates.push_back(&pairs[i]);
    }
    ContactGeometryWorld world;
    std::optional<std::size_t> selected;
    assert(selectGroundFaceCandidate(candidates, world, selected) == ContactGeometryResult::Dropped);
    assert(!selected);
    assert(pairs[0].bodyFeature && pairs[0].triangleFeature);
    body.current = 0;
    std::memcpy(body.state, landingStates, sizeof landingStates);
    assert(selectGroundFaceCandidate(candidates, world, selected) == ContactGeometryResult::Updated);
    assert(selected == 0 && pairs[0].bodyFeature == 6 && pairs[0].triangleFeature == 3);
    const auto& contact = pairs[0].contact;
    const float actual[] = {contact.pointA[0], contact.pointA[1], contact.pointA[2],
                            contact.pointB[0], contact.pointB[1], contact.pointB[2],
                            contact.normal[0], contact.normal[1], contact.normal[2],
                            contact.separation, contact.friction, contact.bounce,
                            contact.normalImpulse, contact.normalForce, contact.frictionForce};
    for (std::size_t i = 0; i < std::size(actual); ++i)
      assert(std::bit_cast<uint32_t>(actual[i]) == expectedContact[i]);
    assert(contact.pendingPriority && std::bit_cast<uint32_t>(*contact.pendingPriority) == 1056964608u);
  }
  {
    // Original 0049ec00 selector, frozen rotated Port poses at step42:
    // parity-selector-rotation-20261102/run-c1f92a2cf2. Two original runs
    // produce these exact point words with CW=007f and declared empty cache.
    // This checks interpolation arithmetic, not a historical Stock trajectory.
    const auto f = [](uint32_t word) { return std::bit_cast<float>(word); };
    Body rotating;
    rotating.flags = kBodyActive;
    rotating.current = 1;
    rotating.state[1].pos = {f(0x401fdd44), f(0x3eabf240), f(0x3b0aec96)};
    rotating.state[0].pos = {f(0x401f97cf), f(0x3eab90ab), f(0x3bd0603e)};
    rotating.state[1].quat = {f(0x3d89d778), f(0xb4c00000), f(0x3d89d7a2), f(0x3f7ed673)};
    rotating.state[0].quat = {f(0x3df6f978), f(0xb4c00000), f(0x3df6f996), f(0x3f7c3fe5)};
    const auto pose = interpolateContactPose(rotating, 0.5f);
    const auto point = transformPoint(pose.pos, pose.rot, {-0.25f, -0.25f, 0.25f});
    assert(std::bit_cast<uint32_t>(point[0]) == 1075005110u);
    assert(std::bit_cast<uint32_t>(point[1]) == 982673408u);
    assert(std::bit_cast<uint32_t>(point[2]) == 1045140618u);
  }
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
  auto query = [](const State& x, const State& y) -> std::optional<CorePair> {
    return sphereCoreClosest(x, y);
  };
  pair = query(a.state[1], b.state[1]).value();
  assert(updateDirectContactGeometryWithSearch(c, pair, 0.5f, 0.5f, point,
                                               geometryWorld, query) ==
         ContactGeometryResult::Updated);
  assert(geometryWorld.stepReady == 0 && a.state[1].stepFlags == 2);
  assert(std::fabs(c.separation - 0.2f) < 0.000001f);
  {
    // FUN_004a2d10: if time search cannot separate a retained pair, use
    // its step-start geometry. A radius-0.5 point starts 0.2 from the box
    // face and ends inside it: the retained separation is 0.2 - 0.5.
    const auto box = boxCore({1, 1, 1});
    Body moving = a;
    moving.state[0].pos = {1.2f, 0, 0};
    moving.state[1].pos = {0.9f, 0, 0};
    DirectContact retained{&moving, &b};
    auto boxQuery = [&](const State& x, const State& y) {
      std::optional<size_t> feature;
      return closestPointPolyhedron(x, box, y, feature);
    };
    const auto predicted = boxQuery(moving.state[1], b.state[1]);
    assert(predicted);
    ContactGeometryWorld retainedWorld;
    Vec3 retainedPoint{};
    assert(updateDirectContactGeometryWithSearch(retained, *predicted,
                                                 0.5f, 0, retainedPoint, retainedWorld, boxQuery) ==
           ContactGeometryResult::Updated);
    assert(std::fabs(retained.separation + 0.3f) < 0.000001f);
    // Stock publishes with the last sampled pose (t=1/1024), even though
    // the local contact geometry above was refreshed at the current pose.
    assert(std::fabs(retainedPoint[0] - 0.69970703125f) < 0.000001f);
  }
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
  {
    Body crossing = ball;
    crossing.state[0].pos = crossing.state[1].pos = {-2.01f, 0.53f, -2.01f};
    GroundFacePair retained;
    assert(createGroundFacePair(crossing, ground, triangle, {0, 1, 0},
                                retained, error));
    assert(updateGroundFacePair(retained, faceWorld, point) ==
           ContactGeometryResult::Updated);
    const auto oldFeature = retained.triangleFeature;
    retained.contact.resting = true;
    retained.contact.normalImpulse = 3;
    retained.contact.normalForce = 4;
    retained.contact.frictionForce = 5;
    crossing.state[0].pos = crossing.state[1].pos = {0, 0.53f, 0};
    // 004af970 rejects the old fixed identity; 004a2d10 destroys that row.
    // A fresh 0049ec00 candidate has independent features and admission band.
    assert(updateGroundFacePair(retained, faceWorld, point) ==
           ContactGeometryResult::Dropped);
    assert(retained.triangleFeature == oldFeature && retained.contact.resting);
    assert(retained.contact.normalImpulse == 3 && retained.contact.normalForce == 4 &&
           retained.contact.frictionForce == 5);
    GroundFacePair replacement;
    assert(createGroundFacePair(crossing, ground, triangle, {0, 1, 0}, replacement, error));
    std::optional<std::size_t> selected;
    assert(selectGroundFaceCandidate({&replacement}, faceWorld, selected) == ContactGeometryResult::Dropped);
    assert(!selected);
    crossing.state[0].pos = crossing.state[1].pos = {0, 0.515f, 0};
    assert(selectGroundFaceCandidate({&replacement}, faceWorld, selected) == ContactGeometryResult::Updated);
    assert(selected == 0 && replacement.triangleFeature != oldFeature);
    assert(!replacement.contact.resting && replacement.contact.normalImpulse == 0 &&
           replacement.contact.normalForce == 0 && replacement.contact.frictionForce == 0);
  }
  {
    // Captured from the deterministic box landing: 004af4f0 accepts this
    // cached vertex inside the face even though a full rewalk prefers an
    // edge. Retaining it must preserve the support row and local point.
    BodyProto box;
    box.invMass = 0.05f;
    box.shape.type = ShapeType::Box;
    box.shape.core = std::make_shared<CollisionCore>(boxCore({0.25f, 0.25f, 0.25f}));
    Body landing;
    landing.proto = &box;
    landing.flags = kBodyActive;
    landing.state[0].pos = {2.70539117f, 0.346628428f, 0.0134900305f};
    landing.state[0].quat = {0.126091242f, 0.664773941f, -0.112605035f, 0.727665305f};
    landing.state[0].rot = matrixFromQuat(landing.state[0].quat);
    landing.state[1] = landing.state[0];
    const std::array<Vec3, 3> triangle{{{2, 0, -1}, {3, 0, 0}, {3, 0, -1}}};
    GroundFacePair retained;
    assert(createGroundFacePair(landing, ground, triangle, {0, 1, 0}, retained, error));
    const Vec3 vertex{0.25f, -0.25f, 0.25f};
    const auto& core = *box.shape.core;
    for (size_t i = 0; i < core.features.size(); ++i) {
      const auto ref = core.features[i].ref;
      if (ref.kind == CoreFeatureKind::Vertex && core.vertices[ref.index].position == vertex)
        retained.bodyFeature = i;
    }
    for (size_t i = 0; i < retained.triangle.features.size(); ++i) {
      const auto ref = retained.triangle.features[i].ref;
      if (ref.kind == CoreFeatureKind::Face && retained.triangle.faces[ref.index].normal == Vec3({0, 1, 0}))
        retained.triangleFeature = i;
    }
    assert(retained.bodyFeature && retained.triangleFeature);
    const auto bodyFeature = retained.bodyFeature;
    const auto triangleFeature = retained.triangleFeature;
    retained.contact.resting = true;
    retained.contact.normalImpulse = 3;
    retained.contact.normalForce = 4;
    retained.contact.frictionForce = 5;
    assert(updateGroundFacePair(retained, faceWorld, point) == ContactGeometryResult::Updated);
    assert(retained.bodyFeature == bodyFeature && retained.triangleFeature == triangleFeature);
    assert(retained.contact.pointA == vertex && retained.contact.resting);
    assert(retained.contact.normalImpulse == 3 && retained.contact.normalForce == 4 &&
           retained.contact.frictionForce == 5);
    // The same fixed-feature lifetime also applies when both sides are
    // body collision cores, rather than a specialized ground-face record.
    BodyProto triangleBody;
    triangleBody.flags = kProtoStatic;
    triangleBody.shape.type = ShapeType::Mesh;
    triangleBody.shape.core = std::make_shared<CollisionCore>(retained.triangle);
    Body obstacle;
    obstacle.proto = &triangleBody;
    DynamicBodyPair bodyPair;
    assert(createDynamicBodyPair(landing, obstacle, bodyPair, error));
    bodyPair.firstFeature = bodyFeature;
    bodyPair.secondFeature = triangleFeature;
    bodyPair.contact.resting = true;
    bodyPair.contact.normalImpulse = 3;
    bodyPair.contact.normalForce = 4;
    bodyPair.contact.frictionForce = 5;
    assert(updateDynamicBodyPair(bodyPair, faceWorld, point) == ContactGeometryResult::Updated);
    assert(bodyPair.firstFeature == bodyFeature && bodyPair.secondFeature == triangleFeature);
    assert(bodyPair.contact.pointA == vertex && bodyPair.contact.resting);
    assert(bodyPair.contact.normalImpulse == 3 && bodyPair.contact.normalForce == 4 &&
           bodyPair.contact.frictionForce == 5);
    DynamicBodyPair reversed;
    assert(createDynamicBodyPair(obstacle, landing, reversed, error));
    reversed.firstFeature = triangleFeature;
    reversed.secondFeature = bodyFeature;
    reversed.contact.resting = true;
    assert(updateDynamicBodyPair(reversed, faceWorld, point) == ContactGeometryResult::Updated);
    assert(reversed.firstFeature == triangleFeature && reversed.secondFeature == bodyFeature);
    assert(reversed.contact.pointB == vertex && reversed.contact.resting);
    assert(reversed.contact.normal == Vec3({0, -1, 0}));
  }
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
         ContactGeometryResult::Dropped);
  {
    // Retained body/body rows obey the same 004a2d10 fixed-feature
    // lifetime as ground rows. A vertex-to-face transition is a new impact.
    BodyProto box;
    box.flags = kProtoStatic;
    box.shape.type = ShapeType::Box;
    box.shape.core = std::make_shared<CollisionCore>(boxCore({1, 1, 1}));
    Body moving, obstacle;
    moving.proto = &sphere;
    moving.flags = kBodyActive;
    obstacle.proto = &box;
    moving.state[0].pos = moving.state[1].pos = {1.3f, 1.3f, 1.3f};
    DynamicBodyPair retained;
    ContactGeometryWorld geometry;
    assert(createDynamicBodyPair(moving, obstacle, retained, error));
    assert(updateDynamicBodyPair(retained, geometry, point) == ContactGeometryResult::Updated);
    const auto oldFeature = retained.secondFeature;
    retained.contact.resting = true;
    retained.contact.normalImpulse = 3;
    retained.contact.normalForce = 4;
    retained.contact.frictionForce = 5;
    assert(updateDynamicBodyPair(retained, geometry, point) == ContactGeometryResult::Updated);
    assert(retained.secondFeature == oldFeature && retained.contact.resting &&
           retained.contact.normalImpulse == 3 && retained.contact.normalForce == 4 &&
           retained.contact.frictionForce == 5);
    moving.state[0].pos = moving.state[1].pos = {1.53f, 0, 0};
    assert(updateDynamicBodyPair(retained, geometry, point) == ContactGeometryResult::Updated);
    assert(retained.secondFeature != oldFeature);
    assert(!retained.contact.resting && retained.contact.normalImpulse == 0 &&
           retained.contact.normalForce == 0 && retained.contact.frictionForce == 0);
  }
  {
    // FUN_0049c8f0: endpoint spheres miss, but the displacement segment
    // passes the object. A bound-only implementation must not pass the miss.
    Body swept, obstacle;
    swept.proto = obstacle.proto = &sphere;
    swept.flags = kBodyActive | 0x800u;
    obstacle.flags = kBodyActive | kBodyAsleep;
    swept.state[0].pos = {-2, 0.75f, 0};
    swept.state[1].pos = {2, 0.75f, 0};
    DynamicBodyPair crossing;
    assert(createDynamicBodyPair(swept, obstacle, crossing, error));
    ContactGeometryWorld crossingWorld;
    assert(updateDynamicBodyPair(crossing, crossingWorld, point) ==
           ContactGeometryResult::Updated);
    // FUN_0049c8f0 (all.c 116233-116246) accepts a separated sample
    // within the 0.02 band when the predicted-space witness dot is negative.
    // Fractions .5,.25,.375,.3125,.34375,.328125 select x=-.6875:
    // sqrt(.6875^2 + .75^2) - (ra+rb) = .017426287, not < .01.
    assert(crossingWorld.queries == 7);
    assert(crossing.contact.separation > 0.0174f &&
           crossing.contact.separation < 0.0175f);
    // Rotating the swept body must not rotate its world displacement: the
    // replacement segment lives in the predicted *local* frame.
    swept.state[0].quat = swept.state[1].quat = {0, 0, 0.70710677f, 0.70710677f};
    swept.state[0].rot = swept.state[1].rot = matrixFromQuat(swept.state[0].quat);
    assert(updateDynamicBodyPair(crossing, crossingWorld, point) ==
           ContactGeometryResult::Updated);
    // Same near broad-phase pair, no geometric intersection with its radius.
    swept.state[0].pos[1] = swept.state[1].pos[1] = 1.5f;
    assert(updateDynamicBodyPair(crossing, crossingWorld, point) ==
           ContactGeometryResult::Dropped);
  }
  {
    // FUN_0049c540: wheel edge / rounded object tilts the normal along the
    // axle and subtracts only the object's radius, with no ten-step search.
    BodyProto wheelProto = sphere;
    wheelProto.shape.type = ShapeType::CylinderX;
    wheelProto.shape.core = std::make_shared<CollisionCore>(
        segmentCore({-0.2f, 0, 0}, {0.2f, 0, 0}));
    Body wheel, obstacle;
    wheel.proto = &wheelProto;
    obstacle.proto = &sphere;
    wheel.flags = kBodyActive | 0x40u;
    obstacle.flags = kBodyActive | kBodyAsleep;
    obstacle.state[0].pos = obstacle.state[1].pos = {0, 0.75f, 0};
    DynamicBodyPair wheelPair;
    assert(createDynamicBodyPair(wheel, obstacle, wheelPair, error));
    ContactGeometryWorld wheelWorld;
    assert(updateDynamicBodyPair(wheelPair, wheelWorld, point) ==
           ContactGeometryResult::Updated);
    assert(wheelWorld.queries == 1);
    assert(wheelPair.contact.separation > 0.249f &&
           wheelPair.contact.separation < 0.251f);
    assert(wheelPair.contact.normal[0] > 0.66f &&
           wheelPair.contact.normal[0] < 0.67f);
    assert(wheelPair.contact.normal[1] == -0.75f);
    DynamicBodyPair reversed;
    assert(createDynamicBodyPair(obstacle, wheel, reversed, error));
    assert(updateDynamicBodyPair(reversed, wheelWorld, point) ==
           ContactGeometryResult::Updated);
    assert(reversed.contact.normal[0] == -wheelPair.contact.normal[0]);
    assert(reversed.contact.normal[1] == -wheelPair.contact.normal[1]);
    assert(reversed.contact.pointB == wheelPair.contact.pointA);
    assert(reversed.contact.separation == wheelPair.contact.separation);
    obstacle.state[0].pos[1] = obstacle.state[1].pos[1] = 1.5f;
    assert(updateDynamicBodyPair(wheelPair, wheelWorld, point) ==
           ContactGeometryResult::Dropped);
  }
}
