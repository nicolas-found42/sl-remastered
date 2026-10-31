#include "physics_friction.hpp"
#include <cmath>
#include <vector>
namespace physics {
namespace {
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
Vec3 contactVelocity(const State& s, const Vec3& point) {
  Vec3 r = matrixTransformRows(s.rot, point);
  Vec3 v = cross(s.omega, r);
  for (unsigned i=0; i<3; ++i) v[i] += s.vel[i];
  return v;
}
Vec3 response(const State& s, const Vec3& r, const Vec3& direction) {
  return cross(matrixTransformRows(s.invInertiaWorld, cross(r, direction)), r);
}
void impulse(Body& b, const Vec3& r, const Vec3& j) {
  State& s = b.state[b.current];
  Vec3 angular = cross(r, j);
  for (unsigned i=0; i<3; ++i) {
    s.vel[i] += b.proto->invMass * j[i];
    s.angMom[i] += angular[i];
  }
  s.omega = matrixTransformRows(s.invInertiaWorld, s.angMom);
}
void force(Body& b, const Vec3& r, const Vec3& f) {
  State& s = b.state[b.current];
  Vec3 torque = cross(r, f);
  for (unsigned i=0; i<3; ++i) { s.force[i] += f[i]; s.torque[i] += torque[i]; }
}
bool resolve(Body*& body, Vec3& point, bool first) {
  std::vector<Body*> visited;
  while (body && (body->flags & kBodyChild)) {
    if ((body->flags & 0x20000) || !body->attachment || !body->attachment->parent) return false;
    for (Body* seen : visited) if (seen == body) return false;
    visited.push_back(body);
    const auto& pose = body->attachment->relative;
    const Mat34& m = pose.rotation();
    Vec3 transformed;
    if (first) {
      transformed = {point[0]*m[0]+point[2]*m[2]+point[1]*m[1],
                     point[2]*m[6]+point[1]*m[5]+point[0]*m[4],
                     point[2]*m[10]+point[1]*m[9]+point[0]*m[8]};
    } else {
      transformed = {point[0]*m[0]+point[1]*m[1]+point[2]*m[2],
                     point[1]*m[5]+point[2]*m[6]+point[0]*m[4],
                     point[1]*m[9]+point[2]*m[10]+point[0]*m[8]};
    }
    for (unsigned i=0; i<3; ++i) point[i] = transformed[i]+pose.position[i];
    body = body->attachment->parent;
  }
  return body != nullptr;
}
float effectiveMass(const Body& a, const Body& b, bool da, bool db,
                    const Vec3& ra, const Vec3& rb, const Vec3& direction) {
  Vec3 x = da ? response(a.state[a.current], ra, direction) : Vec3{};
  Vec3 y = db ? response(b.state[b.current], rb, direction) : Vec3{};
  return (x[1]+y[1])*direction[1] + (x[0]+y[0])*direction[0] +
         (x[2]+y[2])*direction[2] + (db ? b.proto->invMass : 0) + (da ? a.proto->invMass : 0);
}
}
int correctDirectContact(DirectContact& c, DirectContactWorld& world) {
  if (!c.a || !c.b) return 0;
  if (c.separation > k::skin) return 0;
  Body* rootA = c.a; Body* rootB = c.b;
  Vec3 pointA = c.pointA, pointB = c.pointB;
  if (!resolve(rootA, pointA, true) || !resolve(rootB, pointB, false)) return -1;
  Body& a = *rootA; Body& b = *rootB;
  if (!a.proto || !b.proto || a.current > 1 || b.current > 1 || &a == &b ||
      (a.flags & (kBodyChild | kBodyAsleep | 0x20000)) ||
      (b.flags & (kBodyChild | kBodyAsleep | 0x20000))) return -1;
  if (!std::isfinite(c.separation)) return -1;
  for (unsigned i = 0; i < 3; ++i) {
    if (!std::isfinite(c.normal[i]) || !std::isfinite(a.state[a.current].shift[i]) ||
        !std::isfinite(b.state[b.current].shift[i]) ||
        !std::isfinite(a.state[a.current ^ 1].vel[i]) ||
        !std::isfinite(b.state[b.current ^ 1].vel[i]) ||
        !std::isfinite(a.state[a.current ^ 1].omega[i]) || !std::isfinite(b.state[b.current ^ 1].omega[i]) ||
        !std::isfinite(pointA[i]) || !std::isfinite(pointB[i])) return -1;
  }
  for (const Body* body : {&a, &b}) {
    for (const State& state : body->state) {
      for (float v : state.rot) if (!std::isfinite(v)) return -1;
      for (float v : state.invInertiaWorld) if (!std::isfinite(v)) return -1;
      for (float v : state.vel) if (!std::isfinite(v)) return -1;
      for (float v : state.angMom) if (!std::isfinite(v)) return -1;
      for (float v : state.force) if (!std::isfinite(v)) return -1;
      for (float v : state.torque) if (!std::isfinite(v)) return -1;
    }
  }
  const bool da = !(a.proto->flags & kProtoStatic), db = !(b.proto->flags & kProtoStatic);
  if (!da && !db) return 0;
  Vec3 va = contactVelocity(a.state[a.current ^ 1], pointA);
  Vec3 vb = contactVelocity(b.state[b.current ^ 1], pointB);
  Vec3 ra = matrixTransformRows(a.state[a.current].rot, pointA);
  Vec3 rb = matrixTransformRows(b.state[b.current].rot, pointB);
  const float velocity = (va[1]-vb[1])*c.normal[1] + (va[0]-vb[0])*c.normal[0] + (va[2]-vb[2])*c.normal[2];
  if (!std::isfinite(c.friction) || !std::isfinite(c.bounce) ||
      !std::isfinite(c.normalImpulse) || !std::isfinite(c.frictionForce)) return -1;
  const float drive = velocity < -0.2f ? -((velocity - (-0.2f)) * (c.bounce + 1.0f)) : 0;
  if (drive > 0 && (!std::isfinite(world.step) || world.step <= 0 ||
      (da && (!std::isfinite(a.proto->invMass) || a.proto->invMass <= 0)) ||
      (db && (!std::isfinite(b.proto->invMass) || b.proto->invMass <= 0)))) return -1;
  int result = 0;
  if (drive > 0) {
    float denom = effectiveMass(a, b, da, db, ra, rb, c.normal);
    if (!std::isfinite(denom) || denom <= 0) return -1;
    float jmag = drive / denom;
    float normalForce = (1.0f / world.step) * jmag;
    Vec3 tangent{};
    for (unsigned i=0;i<3;++i) tangent[i] = -velocity*c.normal[i] + (va[i]-vb[i]);
    float speed2 = tangent[2]*tangent[2] + tangent[1]*tangent[1] + tangent[0]*tangent[0];
    if (!std::isfinite(jmag) || !std::isfinite(normalForce) || !std::isfinite(speed2)) return -1;
    float speed = 0, tangentDenom = 1;
    if (normalForce > 0.001f && c.friction > 0 && speed2 > 0.001f) {
      speed = std::sqrt(tangent[1]*tangent[1] + tangent[2]*tangent[2] + tangent[0]*tangent[0]);
      float inv = 1.0f / speed;
      for (float& v : tangent) v *= inv;
      tangentDenom = effectiveMass(a, b, da, db, ra, rb, tangent);
      if (!std::isfinite(tangentDenom) || tangentDenom <= 0 ||
          !std::isfinite((1.0f/world.step)*(speed/tangentDenom)) ||
          !std::isfinite(normalForce*c.friction)) return -1;
    }
    Vec3 j{}, opposite{};
    for (unsigned i=0;i<3;++i) { j[i] = jmag * c.normal[i]; opposite[i] = -jmag * c.normal[i]; }
    if (da) impulse(a, ra, j);
    if (db) impulse(b, rb, opposite);
    c.normalImpulse += jmag;
    result = 1;
    if (normalForce > 0.001f && c.friction > 0 && speed2 > 0.001f) {
      float forceMagnitude = (1.0f/world.step)*(speed/tangentDenom);
      float limit = normalForce*c.friction;
      if (forceMagnitude > limit) { forceMagnitude = limit; result = 2; }
      Vec3 f{}, oppositeForce{};
      for (unsigned i=0;i<3;++i) { f[i] = tangent[i]*forceMagnitude; oppositeForce[i] = tangent[i]*-forceMagnitude; }
      if (da) force(a, ra, oppositeForce);
      if (db) force(b, rb, f);
      c.frictionForce += forceMagnitude;
    }
  }
  // NOTE (open regression): stock 00496050 applies only the impulse, with no
  // positional correction. Removing this shift made the compound sink through
  // the spawn overlap (impulses alone under-correct the 36-contact pile);
  // keeping it makes the spawn launch past the wheel-catch window when many
  // part contacts overlap. The settle is being reworked against the stock
  // correction path; see docs/continuation.md.
  if (!(c.separation < 0.021f)) {
    if (result) { if (da) a.state[a.current ^ 1].stepFlags &= ~1u; if (db) b.state[b.current ^ 1].stepFlags &= ~1u; ++world.corrections; }
    return result;
  }
  Vec3& sa = a.state[a.current].shift; Vec3& sb = b.state[b.current].shift;
  const float amount = (0.021f-c.separation) - ((sa[0]*c.normal[0]+sa[1]*c.normal[1]+sa[2]*c.normal[2]) - (sb[0]*c.normal[0]+sb[1]*c.normal[1]+sb[2]*c.normal[2]));
  if (!(amount > 0)) {
    if (result) { if (da) a.state[a.current ^ 1].stepFlags &= ~1u; if (db) b.state[b.current ^ 1].stepFlags &= ~1u; ++world.corrections; }
    return result;
  }
  for (unsigned i=0;i<3;++i) {
    if (da) sa[i] += (db ? amount * 0.5f : amount) * c.normal[i];
    if (db) sb[i] += (da ? amount * -0.5f : -amount) * c.normal[i];
  }
  if (da) a.state[a.current ^ 1].stepFlags &= ~1u;
  if (db) b.state[b.current ^ 1].stepFlags &= ~1u;
  ++world.corrections;
  return 1;
}
}
