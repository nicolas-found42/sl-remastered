// Unit tests for src/physics_body.*. Expected numbers are either hand-derived
// from the stock decompile (see docs/physics-evidence.md) or closed forms
// computed in double precision. The collision regression also has a stock
// executable comparison.
#include "physics_body.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace physics;

static uint32_t bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, sizeof u);
  return u;
}

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static void testConstants() {
  // Values read from the stock executable data (addresses in the header).
  assert(bits(k::skin) == 0x3d4ccccdu);
  assert(bits(k::linearDragPerMass) == 0xbba3d70au);
  assert(bits(k::angularDragPerMass) == 0xbc23d70au);
  assert(bits(k::minimumStep) == 0x38d1b717u);
  assert(bits(k::sixth) == 0x3e2aaaabu);
  assert(bits(k::third) == 0x3eaaaaabu);
  assert(bits(k::angularSpeedLimitScale) == 0x3b23d70au);
  assert(bits(k::linearSpeedLimitSquared) == 0x46afc800u);
  assert(bits(k::sphereVolumeFactor) == 0x40860a92u);
  assert(bits(k::twelfth) == 0x3daaaaabu);
  assert(bits(k::tenth) == 0x3dcccccdu);
  assert(bits(k::restSmoothKeep) == 0x3f666666u);
  assert(bits(k::restAngularSq) == 0x3a83126fu);
  assert(bits(k::restForceSq) == 0x3c23d70au);
  assert(bits(k::defaultFriction) == 0x3f19999au);
  assert(bits(k::defaultBounce) == 0x3e4ccccdu);
  assert(bits(k::gravityY) == 0xc11cf5c3u);
  assert(bits(k::airDensity) == 0x3fa57a78u);
  assert(bits(k::quarter) == 0x3e800000u);
  assert(bits(k::angularDampScale) == 0x42c80000u);
}

static void testPrototypes() {
  std::string err;
  // Sphere, mass 2, radius 1.
  auto s = createBodyProto(2.0f, 1, 1.0f, 0, 0, nullptr, err);
  assert(s);
  assert(s->invMass == 0.5f);
  assert(s->linearDrag == 2.0f * -0.005f);
  assert(s->angularDrag == 2.0f * -0.01f);
  assert(s->flags == (kProtoCreated | kProtoAlways));
  assert(s->shape.radius == 1.0f && s->shape.dim[0] == 1.0f);
  assert(s->shape.boundRadius == 1.0f + k::skin);
  assert(s->shape.extent[0] == 1.0f + k::skin && s->shape.extent[2] == 1.0f + k::skin);
  assert(s->shape.friction == 0.6f && s->shape.bounce == 0.2f);
  assert(s->invInertia[0] == 1.25f && s->invInertia[5] == 1.25f && s->invInertia[10] == 1.25f);
  assert(near(s->inertia[0], 0.8, 1e-6) && near(s->inertia[10], 0.8, 1e-6));
  assert(s->inertia[1] == 0.0f && s->inertia[4] == 0.0f);
  assert(near(s->volume, 4.18879, 1e-4) && s->volume == s->shape.volume);

  // Static box with half extents 1, 2, 3: zero mass keeps zero matrices and sets the static bit.
  auto sb = createBodyProto(0.0f, 5, 1.0f, 2.0f, 3.0f, nullptr, err);
  assert(sb);
  assert(sb->invMass == 0 && sb->linearDrag == 0 && sb->angularDrag == 0);
  assert(sb->flags == (kProtoCreated | kProtoStatic | kProtoAlways));  // 0x405
  assert(sb->flags == 0x405u);
  assert(sb->invInertia == matrixZero() && sb->inertia == matrixZero());
  assert(sb->shape.boundRadius == std::sqrt(3.0f * 3.0f + 2.0f * 2.0f + 1.0f * 1.0f) + k::skin);
  assert(sb->shape.radius == 0);
  assert(sb->shape.extent[0] == 1.0f + k::skin && sb->shape.extent[1] == 2.0f + k::skin &&
         sb->shape.extent[2] == 3.0f + k::skin);
  assert(sb->volume == 0);  // static: 004a6010 returns before computing a volume

  // Dynamic box mass 6.
  auto db = createBodyProto(6.0f, 5, 1.0f, 2.0f, 3.0f, nullptr, err);
  assert(db);
  assert(near(db->invInertia[0], 3.0 / (13.0 * 6.0), 1e-7));
  assert(near(db->invInertia[5], 3.0 / (10.0 * 6.0), 1e-7));
  assert(near(db->invInertia[10], 3.0 / (5.0 * 6.0), 1e-7));
  assert(near(db->inertia[0], 13.0 * 6.0 / 3.0, 1e-4));
  assert(near(db->volume, 48.0, 1e-5));

  // Cylinder along Z, mass 3, radius 0.5, half length 1.
  auto cz = createBodyProto(3.0f, 4, 1.0f, 0.5f, 0, nullptr, err);
  assert(cz);
  assert(near(cz->invInertia[10], 2.0 / (0.25 * 3.0), 1e-6));
  assert(near(cz->invInertia[0], 1.0 / ((0.25 * 0.25 + 4.0 / 12.0) * 3.0), 1e-6));
  assert(cz->invInertia[0] == cz->invInertia[5]);
  assert(cz->shape.radius == 0.5f && cz->shape.dim[0] == 1.0f);
  assert(cz->shape.boundRadius == 1.0f + 0.5f + k::skin);
  assert(cz->shape.extent[2] == 1.0f + 0.5f + k::skin && cz->shape.extent[0] == 0.5f + k::skin);
  assert(cz->volume == 0);
  auto cx = createBodyProto(3.0f, 2, 1.0f, 0.5f, 0, nullptr, err);
  assert(cx && cx->invInertia[0] == cz->invInertia[10] && cx->shape.extent[0] == cz->shape.extent[2]);
  auto cy = createBodyProto(3.0f, 3, 1.0f, 0.5f, 0, nullptr, err);
  assert(cy && cy->invInertia[5] == cz->invInertia[10] && cy->shape.extent[1] == cz->shape.extent[2]);

  // Empty (type 0): world root bodies.
  auto e = createBodyProto(0.0f, 0, 0, 0, 0, nullptr, err);
  assert(e && e->invInertia == matrixZero() && e->volume == 0 && e->flags == 0x405u);

  // Vertex cloud body (type 7).
  const std::vector<Vec3> verts{{1, 0, 0}, {0, 2, 0}, {0, 0, -3}};
  auto h = createBodyProto(2.0f, 7, 0, 0, 0, &verts, err);
  assert(h);
  assert(h->shape.boundRadius == 3.0f + k::skin);
  assert(h->invInertia[0] == k::sphereInertiaFactor / (2.0f * (3.0f + k::skin)));
  assert(h->volume == 0 && (h->shape.flags & 1));
  auto h0 = createBodyProto(2.0f, 7, 0, 0, 0, nullptr, err);  // zero mesh handle: radius 1
  assert(h0 && h0->invInertia[0] == 2.5f / (2.0f * 1.0f));

  // Mesh type: static is fine, dynamic needs 004aa330 (not ported).
  auto m0 = createBodyProto(0.0f, 6, 0, 0, 0, &verts, err);
  assert(m0 && m0->massPropertiesSupported && m0->shape.boundRadius == 3.0f + k::skin);
  auto m1 = createBodyProto(5.0f, 6, 0, 0, 0, &verts, err);
  assert(m1 && !m1->massPropertiesSupported);
  auto m2 = createBodyProto(5.0f, 6, 0, 0, 0, nullptr, err);  // handle 0: unit sphere stand-in
  assert(m2 && m2->invInertia[0] == 2.5f / (5.0f * 1.0f));

  assert(!createBodyProto(1.0f, 9, 0, 0, 0, nullptr, err) && !err.empty());
}

static void testMath() {
  // Quaternion product against the Hamilton product (x, y, z, w order).
  uint32_t seed = 12345;
  auto rnd = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<int32_t>(seed >> 8) % 2000) / 1000.0f - 1.0f;
  };
  for (int i = 0; i < 200; ++i) {
    const Quat a{rnd(), rnd(), rnd(), rnd()}, b{rnd(), rnd(), rnd(), rnd()};
    const Quat r = quatMultiply(a, b);
    const double w = double(a[3]) * b[3] - (double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2]);
    const double x = double(a[3]) * b[0] + double(b[3]) * a[0] + (double(a[1]) * b[2] - double(a[2]) * b[1]);
    const double y = double(a[3]) * b[1] + double(b[3]) * a[1] + (double(a[2]) * b[0] - double(a[0]) * b[2]);
    const double z = double(a[3]) * b[2] + double(b[3]) * a[2] + (double(a[0]) * b[1] - double(a[1]) * b[0]);
    assert(near(r[0], x, 1e-5) && near(r[1], y, 1e-5) && near(r[2], z, 1e-5) && near(r[3], w, 1e-5));
  }
  // Rotation matrix of 90 degrees about z.
  const float s = std::sqrt(0.5f);
  const Mat34 rz = matrixFromQuat(Quat{0, 0, s, s});
  assert(near(rz[0], 0, 1e-6) && near(rz[1], -1, 1e-6) && near(rz[4], 1, 1e-6) &&
         near(rz[5], 0, 1e-6) && near(rz[10], 1, 1e-6));
  assert(matrixFromQuat(Quat{0, 0, 0, 1}) == matrixIdentity());
  // Inverse of a general matrix times itself is the identity.
  Mat34 m{};
  m[0] = 2; m[1] = 1; m[2] = 0; m[4] = 0; m[5] = 3; m[6] = 1; m[8] = 1; m[9] = 0; m[10] = 4;
  const Mat34 prod = matrixMultiply(m, matrixInverse(m));
  const Mat34 id = matrixIdentity();
  for (size_t i : {0, 1, 2, 4, 5, 6, 8, 9, 10}) assert(near(prod[i], id[i], 1e-6));
  assert(matrixTranspose(matrixTranspose(m)) == m);
  // normalize: zero quaternion becomes identity.
  assert(quatNormalize(Quat{0, 0, 0, 0}) == (Quat{0, 0, 0, 1}));
  const Quat n = quatNormalize(Quat{0, 0, 3, 4});
  assert(near(n[2], 0.6, 1e-6) && near(n[3], 0.8, 1e-6));
  // Point transform: rotate (1,0,0) by 90 degrees about z and translate.
  const Vec3 p = transformPoint({10, 20, 30}, rz, {1, 0, 0});
  assert(near(p[0], 10, 1e-5) && near(p[1], 21, 1e-5) && near(p[2], 30, 1e-5));
}

static void testOrientedCreation() {
  assert(quatFromMatrix(matrixIdentity()) == (Quat{0, 0, 0, 1}));
  std::string error;
  const auto proto = createBodyProto(12, 5, 1, 2, 3, nullptr, error);
  assert(proto);
  for (size_t axis = 0; axis < 3; ++axis) {
    Mat34 halfTurn = matrixIdentity();
    for (size_t i = 0; i < 3; ++i) halfTurn[i * 5] = i == axis ? 1.0f : -1.0f;
    Quat expected{};
    expected[axis] = 1;
    assert(quatFromMatrix(halfTurn) == expected);
    const auto body = createBody(*proto, {1, 2, 3}, &halfTurn);
    for (const auto& state : body.state) {
      assert(state.pos == (Vec3{1, 2, 3}));
      assert(state.quat == expected);
      assert(state.rot == halfTurn);
      assert(state.invInertiaWorld == proto->invInertia);
    }
  }
  Mat34 quarterTurn{};
  quarterTurn[1] = -1; quarterTurn[4] = 1; quarterTurn[10] = 1;
  const auto body = createBody(*proto, {4, 5, 6}, &quarterTurn);
  assert(near(body.state[0].invInertiaWorld[0], proto->invInertia[5], 1e-6));
  assert(near(body.state[0].invInertiaWorld[5], proto->invInertia[0], 1e-6));
  assert(body.state[0].quat == body.state[1].quat);
  assert(body.state[0].rot == body.state[1].rot);
  assert(body.state[0].invInertiaWorld == body.state[1].invInertiaWorld);
  const Quat turns[] = {{0.8f, 0, 0, 0.6f}, {0, 0.8f, 0, 0.6f},
                        {0, 0, 0.8f, 0.6f}, {0, 0, -0.6f, 0.8f},
                        {0.5f, 0.5f, 0.5f, 0.5f}};
  for (const auto& turn : turns) {
    const auto matrix = matrixFromQuat(turn);
    const auto roundTrip = matrixFromQuat(quatFromMatrix(matrix));
    for (size_t i : {0, 1, 2, 4, 5, 6, 8, 9, 10})
      assert(near(roundTrip[i], matrix[i], 1e-6));
  }
  const Mat34 stockInput{0.36f, -0.48f, 0.8f, 0, 0.8f, 0.60f, 0, 0,
                         -0.48f, 0.64f, 0.60f, 0};
  const auto stockQuat = quatFromMatrix(stockInput);
  const uint32_t expectedQuat[]{0x3e4ccccc, 0x3ecccccc, 0x3ecccccc, 0x3f4ccccd};
  const uint32_t expectedMatrix[]{0x3eb851ee, 0xbef5c290, 0x3f4ccccc, 0,
                                  0x3f4ccccc, 0x3f19999a, 0xb3000000, 0,
                                  0xbef5c290, 0x3f23d70a, 0x3f19999a, 0};
  for (size_t i = 0; i < 4; ++i) assert(bits(stockQuat[i]) == expectedQuat[i]);
  const auto stockMatrix = matrixFromQuat(stockQuat);
  for (size_t i = 0; i < 12; ++i) assert(bits(stockMatrix[i]) == expectedMatrix[i]);
}

static void testForces() {
  std::string err;
  auto proto = createBodyProto(2.0f, 1, 1.0f, 0, 0, nullptr, err);
  Body b = createBody(*proto, {0, 0, 0});
  const auto later = createBody(*proto, {0, 0, 0}, nullptr, 12.5f);
  assert(later.state[0].time == 12.5f && later.state[1].time == 12.5f);
  assert(b.flags == kBodyActive);
  assert(!stepBody(b, 0.01f));
  b.state[0].vel = {1, 2, 3};
  WorldParams w;
  assert(accumulateWorldForces(w, b));
  // drag: mass * -0.005 * vel, then gravity mass * g.
  const State& s = b.state[0];
  assert(near(s.force[0], 2.0 * -0.005 * 1, 1e-6));
  assert(near(s.force[1], 2.0 * -0.005 * 2 + 2.0 * -9.81, 1e-5));
  assert(near(s.force[2], 2.0 * -0.005 * 3, 1e-6));
  // Gating: asleep, gravity-off, static.
  Body asleep = b;
  asleep.flags |= kBodyAsleep;
  assert(!accumulateWorldForces(w, asleep));
  auto stat = createBodyProto(0.0f, 5, 1, 1, 1, nullptr, err);
  Body sb = createBody(*stat, {0, 0, 0});
  assert(sb.flags == 0 && !accumulateWorldForces(w, sb));
  Body ng = createBody(*proto, {0, 0, 0});
  ng.flags |= kBodyNoGravity;
  assert(accumulateWorldForces(w, ng) && ng.state[0].force[1] == 0.0f);
  // addForce at an offset: torque = offset x force.
  Body c = createBody(*proto, {0, 0, 0});
  const Vec3 off{1, 0, 0}, f{0, 0, 5};
  addForce(c, f, &off);
  assert(c.state[0].force[2] == 5.0f);
  assert(c.state[0].torque[0] == 0.0f && c.state[0].torque[1] == -5.0f && c.state[0].torque[2] == 0.0f);
  // Marks the other buffer as not stepped.
  c.state[1].stepFlags = 1;
  accumulateWorldForces(w, c);
  assert((c.state[1].stepFlags & 1u) == 0);
}

static void testFreeFall() {
  std::string err;
  auto proto = createBodyProto(1.0f, 1, 0.5f, 0, 0, nullptr, err);
  Body b = createBody(*proto, {0, 10, 0});
  World world;
  world.bodies.push_back(&b);
  const float dt = 0.01f;
  assert(world.stepFreeBodies(dt) == FreeBodyStepResult::Advanced);
  assert(world.stepsDone == 1 && world.lastStep == dt && world.pending == 0.0f && world.time == dt);
  assert(b.current == 1);
  const State& s1 = b.state[b.current];
  const double g = -9.81;
  // Constant force: the RK4 step is exact, pos = 0.5 g dt^2, vel = g dt.
  // y = 10 has a float spacing of 9.5e-7, so allow three units in the last place.
  assert(near(s1.pos[1], 10.0 + 0.5 * g * 0.01 * 0.01, 3e-6));
  assert(near(s1.vel[1], g * 0.01, 1e-6));
  assert(s1.pos[0] == 0.0f && s1.pos[2] == 0.0f && s1.vel[0] == 0.0f);
  assert(near(s1.time, 0.01, 1e-7));
  assert(s1.force == (Vec3{0, 0, 0}) && s1.torque == (Vec3{0, 0, 0}));
  assert((b.state[0].stepFlags & 1u) != 0 && (b.state[1].stepFlags & 1u) != 0);
  // Second step includes drag from the first velocity.
  assert(world.stepFreeBodies(dt) == FreeBodyStepResult::Advanced);
  const State& s2 = b.state[b.current];
  const double v1 = s1.vel[1];
  const double a = (-0.005 * v1 + g);  // mass 1
  assert(near(s2.vel[1], v1 + a * 0.01, 1e-6));
  assert(near(s2.pos[1], double(s1.pos[1]) + v1 * 0.01 + 0.5 * a * 0.01 * 0.01, 3e-6));
  assert(near(s2.time, 0.02, 1e-6));
  // Speed limit: a very fast body is clamped to 150.
  State in = b.state[b.current], out = in;
  in.vel = {300, 0, 400};
  in.force = {0, 0, 0};
  assert(integrate(*proto, b.flags, in, out, 0.001f));
  const double speed = std::sqrt(double(out.vel[0]) * out.vel[0] + double(out.vel[2]) * out.vel[2]);
  assert(near(speed, 150.0, 1e-3) && near(out.vel[0] / speed, 0.6, 1e-5));
  // Pending time: below the minimum step nothing happens.
  World w2;
  Body b2 = createBody(*proto, {0, 0, 0});
  w2.bodies.push_back(&b2);
  assert(w2.stepFreeBodies(0.00005f) == FreeBodyStepResult::Accumulated);
  assert(w2.pending == 0.00005f && b2.current == 0);
  b2.flags |= kBodyHasChildren;
  assert(w2.stepFreeBodies(0.00006f) == FreeBodyStepResult::Unsupported);
  assert(w2.pending == 0.00005f && w2.time == 0 && b2.current == 0);
  b2.flags &= ~kBodyHasChildren;
  assert(w2.stepFreeBodies(0.00006f) == FreeBodyStepResult::Advanced);
  assert(w2.pending == 0.0f && w2.lastStep == 0.00005f + 0.00006f);
}

static void testRotation() {
  std::string err;
  auto proto = createBodyProto(2.0f, 5, 1.0f, 1.0f, 1.0f, nullptr, err);
  Body b = createBody(*proto, {0, 0, 0});
  World world;
  world.params.gravityEnabled = false;
  world.bodies.push_back(&b);
  // Torque about z, applied for two steps.
  // Large enough that the rest test (omega^2 < 0.001) does not put the body to sleep.
  const float dt = 0.01f, tau = 40.0f;
  b.state[0].torque = {0, 0, tau};
  assert(world.stepFreeBodies(dt) == FreeBodyStepResult::Advanced);
  const State& s1 = b.state[b.current];
  // Angular momentum = tau*dt, then angular damping 1 - dt (mass-independent).
  const double damp = double(proto->invMass) * (2.0 * -0.01) * 0.01 * 100.0 + 1.0;
  assert(near(damp, 0.99, 1e-6));
  assert(near(s1.angMom[2], tau * 0.01 * damp, 1e-6));
  // omega = invI * L, invI_zz = 3/((1+1)*2) = 0.75.
  assert(near(s1.omega[2], 0.75 * tau * 0.01 * damp, 1e-6));
  assert(s1.quat == (Quat{0, 0, 0, 1}));  // omega was zero going in
  b.state[b.current].torque = {0, 0, 0};
  const double omegaIn = s1.omega[2];
  assert(world.stepFreeBodies(dt) == FreeBodyStepResult::Advanced);
  const State& s2 = b.state[b.current];
  const double theta = omegaIn * 0.01;
  assert(near(s2.quat[2], std::sin(theta / 2), 1e-6) && near(s2.quat[3], std::cos(theta / 2), 1e-6));
  assert(near(s2.quat[0], 0, 1e-7) && near(s2.quat[1], 0, 1e-7));
  assert(near(s2.rot[1], -std::sin(theta), 1e-6) && near(s2.rot[4], std::sin(theta), 1e-6));
  // World inertia of a cube stays diagonal after rotating about z.
  assert(near(s2.invInertiaWorld[0], 0.75, 1e-5) && near(s2.invInertiaWorld[1], 0, 1e-6));
  // The angular limit: |omega|^2 * 0.0025 > 1 scales momentum by 1/(that value).
  State in = s2, out = s2;
  in.angMom = {0, 0, 1000.0f};
  in.omega = {0, 0, 0};
  in.torque = {0, 0, 0};
  assert(integrate(*proto, b.flags, in, out, 0.001f));
  const double wRaw = 0.75 * 1000.0 * (double(proto->invMass) * -0.02 * 0.001 * 100.0 + 1.0);
  const double limit = wRaw * wRaw * 0.0025;
  assert(limit > 1.0);
  assert(near(out.omega[2], wRaw / limit, wRaw / limit * 1e-4));
}

static void testBranchesAndCommit() {
  std::string err;
  auto proto = createBodyProto(1.0f, 1, 1.0f, 0, 0, nullptr, err);
  // Child bodies need the parent chain: refused, output untouched.
  State in, out;
  out.pos = {7, 7, 7};
  assert(!integrate(*proto, kBodyActive | kBodyChild, in, out, 0.01f) && out.pos[0] == 7.0f);
  // Simple branch: flags 0x100 on the body and 0x200 on the prototype.
  auto simple = createBodyProto(2.0f, 1, 1.0f, 0, 0, nullptr, err);
  simple->flags |= kProtoSimple;
  in.pos = {1, 2, 3};
  in.vel = {1, 0, -1};
  in.force = {4, 0, 0};
  in.time = 1.0f;
  assert(integrate(*simple, kBodyActive | kBodyNoRotation, in, out, 0.5f));
  assert(out.pos == (Vec3{1.5f, 2.0f, 2.5f}));
  assert(out.vel == (Vec3{2.0f, 0.0f, -1.0f}));  // 0.5 * 0.5 * 4 + 1
  assert(out.time == 1.5f);
  // No-rotation Euler branch: pos = pos + dt*vel, vel = vel + dt*invMass*force.
  assert(integrate(*proto, kBodyActive | kBodyNoRotation, in, out, 0.5f));
  assert(near(out.pos[0], 1.5, 1e-6) && near(out.vel[0], 3.0, 1e-6));
  assert(out.force == in.force && out.omega == (Vec3{0, 0, 0}));

  // Rest: a body with no forces and no motion goes to sleep instead of flipping.
  Body b = createBody(*proto, {0, 0, 0});
  WorldParams w;
  w.gravityEnabled = false;
  accumulateWorldForces(w, b);
  assert(stepBody(b, 0.01f));
  assert(commit(b) == CommitResult::WentToSleep && b.current == 0);
  // A body that is already asleep is not stepped.
  b.flags |= kBodyAsleep;
  assert(!stepBody(b, 0.01f));
  // A static body is never stepped and never flips.
  auto stat = createBodyProto(0.0f, 5, 1, 1, 1, nullptr, err);
  Body sb = createBody(*stat, {0, 0, 0});
  assert(!stepBody(sb, 0.01f) && commit(sb) == CommitResult::Unchanged);
  // A falling body flips and the new current state has clear accumulators.
  Body f = createBody(*proto, {0, 5, 0});
  WorldParams g;
  accumulateWorldForces(g, f);
  assert(f.state[0].force[1] != 0.0f);
  assert(stepBody(f, 0.01f));
  // A second step in the same frame is skipped (stepFlags bit 0 on the output).
  assert(!stepBody(f, 0.01f));
  assert(commit(f) == CommitResult::Flipped && f.current == 1);
  assert(f.state[1].force == (Vec3{0, 0, 0}));
  // The smoothed force is 0.1 * force after the first commit.
  assert(near(f.activity[1], 0.1 * -9.81, 1e-5));
  // A body with a step flag clear on the next buffer is left alone.
  Body idle = createBody(*proto, {0, 0, 0});
  accumulateWorldForces(w, idle);
  assert(commit(idle) == CommitResult::Unchanged && idle.current == 0);
  World restingWorld;
  restingWorld.params.gravityEnabled = false;
  Body sleeper = createBody(*proto, {1, 2, 3});
  restingWorld.bodies.push_back(&sleeper);
  assert(restingWorld.stepFreeBodies(0.01f) == FreeBodyStepResult::Advanced);
  assert(sleeper.flags & kBodyAsleep);
  assert(sleeper.current == 0 && sleeper.state[0].time == 0);
  assert(sleeper.state[0].pos == (Vec3{1,2,3}));
  assert(sleeper.state[1].pos == sleeper.state[0].pos);
  sleeper.flags |= kBodyHasChildren;
  const float previousTime = restingWorld.time;
  assert(restingWorld.stepFreeBodies(0.01f) == FreeBodyStepResult::Unsupported && restingWorld.time == previousTime);
}

static void testCollision() {
  std::string err;
  auto proto = createBodyProto(1.0f, 1, 1.0f, 0, 0, nullptr, err);
  Body a = createBody(*proto, {3, 4, 0});
  Body b = createBody(*proto, {0, 0, 0});
  const CorePair c = sphereCoreClosest(a.state[0], b.state[0]);
  assert(near(c.distance, 5.0, 1e-6));
  assert(near(c.normal[0], 0.6, 1e-6) && near(c.normal[1], 0.8, 1e-6) && c.normal[2] == 0.0f);
  assert(c.pointA == (Vec3{0, 0, 0}) && c.pointB == (Vec3{0, 0, 0}));
  // 004af970, x87 precision 0x007f: stock cached-query pointcase 2.
  State measured; measured.pos = {0.13f + 0.125f, -1.21f, 2.78f};
  const CorePair stockCase = sphereCoreClosest(measured, b.state[0]);
  assert(bits(stockCase.distance) == 0x4042ba41u);
  assert(bits(stockCase.normal[0]) == 0x3daba444u);
  assert(bits(stockCase.normal[1]) == 0xbecb9d38u);
  assert(bits(stockCase.normal[2]) == 0x3f69e761u);
  // Reversed pair flips the normal.
  const CorePair r = sphereCoreClosest(b.state[0], a.state[0]);
  assert(near(r.normal[0], -0.6, 1e-6) && near(r.distance, 5.0, 1e-6));
  // Coincident centres: distance exactly zero, normal stays the raw (zero) difference.
  const CorePair z = sphereCoreClosest(b.state[0], b.state[0]);
  assert(z.distance == 0.0f && z.normal == (Vec3{0, 0, 0}));
  // Pair drop rule: zero distance or beyond r1 + r2 + 0.05.
  assert(pairDropped(0.0f, 1.0f, 1.0f));
  assert(pairDropped(2.1f, 1.0f, 1.0f));
  assert(!pairDropped(2.04f, 1.0f, 1.0f));
  assert(!pairDropped(1.5f, 1.0f, 1.0f));
}

int main() {
  testConstants();
  testPrototypes();
  testMath();
  testOrientedCreation();
  testForces();
  testFreeFall();
  testRotation();
  testBranchesAndCommit();
  testCollision();
  std::puts("physics_body_test ok");
  return 0;
}
