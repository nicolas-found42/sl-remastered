#include "physics_friction.hpp"
#include <cmath>
#include <algorithm>
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
  const auto parent = [](Body* node) -> Body* {
    return node && (node->flags & kBodyChild) && node->attachment ?
        node->attachment->parent : nullptr;
  };
  Body* slow = body;
  Body* fast = body;
  while (body && (body->flags & kBodyChild)) {
    if ((body->flags & 0x20000) || !body->attachment || !body->attachment->parent) return false;
    slow = parent(slow);
    fast = parent(parent(fast));
    if (slow && slow == fast) return false;
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
// FUN_004a3900 invokes 004925d0/004927f0 for every row before any
// 00492ad0 application. Its allocated matrix is not assembled or factored.
int solveRestingContacts(std::vector<DirectContact>& contacts, DirectContactWorld& world) {
  struct Row { DirectContact* contact; Body* a; Body* b; Vec3 ra, rb; float value; };
  std::vector<Row> rows;
  if (!std::isfinite(world.step) || world.step <= 0) return -1;
  for (auto& c : contacts) {
    if (!c.constraintRow) continue;
    if (rows.empty()) rows.reserve(contacts.size());
    Body* a=c.a; Body* b=c.b;
    Vec3 pa=c.pointA, pb=c.pointB;
    if (!resolve(a,pa,true) || !resolve(b,pb,false) || !a->proto || !b->proto ||
        a==b || a->current>1 || b->current>1) return -1;
    const bool da=!(a->proto->flags & kProtoStatic), db=!(b->proto->flags & kProtoStatic);
    if ((!da || (a->flags & kBodyAsleep)) && (!db || (b->flags & kBodyAsleep))) continue;
    const State& sa=a->state[a->current]; const State& sb=b->state[b->current];
    const State& na=a->state[a->current^1]; const State& nb=b->state[b->current^1];
    const Vec3 va=contactVelocity(na,pa), vb=contactVelocity(nb,pb);
    const Vec3 ra=matrixTransformRows(sa.rot,pa), rb=matrixTransformRows(sb.rot,pb);
    float rhs=c.resting ? c.separation : 0;
    if (c.resting && ((na.stepFlags & 1u)==0 || (nb.stepFlags & 1u)==0) && rhs<0.03f)
      rhs=(0.03f-rhs)*10.0f;
    const float velocity=(va[0]-vb[0])*c.normal[0]+(va[1]-vb[1])*c.normal[1]+(va[2]-vb[2])*c.normal[2];
    if (velocity < -0.2f) rhs=rhs-(c.bounce+1.0f)*
        (c.resting ? ((velocity-(-0.2f))-(-0.2f)) : (velocity-(-0.2f)));
    // 004927f0 uses both prototypes/inertias, including the static side.
    const Vec3 ar=response(sa,ra,c.normal), br=response(sb,rb,c.normal);
    const float denominator=b->proto->invMass+a->proto->invMass+
        (br[0]+ar[0])*c.normal[0]+(br[2]+ar[2])*c.normal[2]+
        (br[1]+ar[1])*c.normal[1];
    const float value=rhs/denominator;
    if (!std::isfinite(rhs) || !std::isfinite(denominator) || denominator==0 ||
        !std::isfinite(value) || !std::isfinite(value/world.step)) return -1;
    for (unsigned i=0;i<3;++i)
      if (!std::isfinite(c.normal[i]) || !std::isfinite(ra[i]) || !std::isfinite(rb[i])) return -1;
    rows.push_back({&c,a,b,ra,rb,value});
  }
  int changed=0;
  for (const auto& row:rows) {
    const float magnitude=row.contact->resting ? (1.0f/world.step)*row.value : row.value;
    Vec3 f{}, opposite{};
    for(unsigned i=0;i<3;++i) { f[i]=magnitude*row.contact->normal[i]; opposite[i]=-f[i]; }
    if (row.contact->resting) {
      if (!(row.a->proto->flags & kProtoStatic)) force(*row.a,row.ra,f);
      if (!(row.b->proto->flags & kProtoStatic)) force(*row.b,row.rb,opposite);
      row.contact->normalForce+=magnitude;
    } else {
      if (!(row.a->proto->flags & kProtoStatic)) impulse(*row.a,row.ra,f);
      if (!(row.b->proto->flags & kProtoStatic)) impulse(*row.b,row.rb,opposite);
      row.contact->normalImpulse+=magnitude;
    }
    row.contact->processed=true;
    changed |= row.value!=0;
  }
  return changed;
}
int correctDirectContact(DirectContact& c, DirectContactWorld& world) {
  if (!c.a || !c.b) return 0;
  if (c.separation > k::skin) return 0;
  Body* rootA = c.a; Body* rootB = c.b;
  Vec3 pointA = c.pointA, pointB = c.pointB;
  if (!resolve(rootA, pointA, true) || !resolve(rootB, pointB, false)) return -1;
  Body& a = *rootA; Body& b = *rootB;
  if (!a.proto || !b.proto || a.current > 1 || b.current > 1 || &a == &b ||
      (a.flags & (kBodyChild | 0x20000)) ||
      (b.flags & (kBodyChild | 0x20000))) return -1;
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
  if ((!da || (a.flags & kBodyAsleep)) &&
      (!db || (b.flags & kBodyAsleep))) return 0;
  Vec3 va = contactVelocity(a.state[a.current ^ 1], pointA);
  Vec3 vb = contactVelocity(b.state[b.current ^ 1], pointB);
  Vec3 ra = matrixTransformRows(a.state[a.current].rot, pointA);
  Vec3 rb = matrixTransformRows(b.state[b.current].rot, pointB);
  const float velocity = (va[1]-vb[1])*c.normal[1] + (va[0]-vb[0])*c.normal[0] + (va[2]-vb[2])*c.normal[2];
  if (!std::isfinite(c.friction) || !std::isfinite(c.bounce) ||
      !std::isfinite(c.normalImpulse) || !std::isfinite(c.frictionForce) ||
      !std::isfinite(c.normalForce)) return -1;
  // No nonnegative bound on the accumulated +0xa4 normal force: 00496050
  // only accumulates it and never validates the sign, matching the signed
  // resting responses the 004927f0 diagonal keeps.
  const bool bothPredicted=(a.state[a.current^1].stepFlags & 1u) && (b.state[b.current^1].stepFlags & 1u);
  float drive=c.resting && !bothPredicted && c.separation<0.03f ? (0.03f-c.separation)*10.0f : 0;
  if (velocity < -0.2f) drive-=((velocity-(-0.2f))*(c.bounce+1.0f));
  int result=0;
  if (drive > 0) {
    if (!std::isfinite(world.step) || world.step<=0) return -1;
    const float denominator=effectiveMass(a,b,da,db,ra,rb,c.normal);
    if (!std::isfinite(denominator) || denominator<=0) return -1;
    const float jmag=drive/denominator;
    const float normalForce=(1.0f/world.step)*jmag;
    Vec3 tangent{};
    for(unsigned i=0;i<3;++i) tangent[i]=-velocity*c.normal[i]+(va[i]-vb[i]);
    const float speed2=tangent[2]*tangent[2]+tangent[1]*tangent[1]+tangent[0]*tangent[0];
    if (!std::isfinite(jmag) || !std::isfinite(normalForce) || !std::isfinite(speed2)) return -1;
    float frictionMagnitude=0;
    bool limited=false;
    if (normalForce>0.001f && c.friction>0 && speed2>0.001f) {
      const float speed=std::sqrt(tangent[1]*tangent[1]+tangent[2]*tangent[2]+tangent[0]*tangent[0]);
      const float inverse=1.0f/speed;
      for(float& v:tangent) v*=inverse;
      const float mass=effectiveMass(a,b,da,db,ra,rb,tangent);
      if (!std::isfinite(mass) || mass<=0) return -1;
      frictionMagnitude=(1.0f/world.step)*(speed/mass);
      const float bound=normalForce*c.friction;
      if (!std::isfinite(frictionMagnitude) || !std::isfinite(bound)) return -1;
      if (frictionMagnitude>bound) { frictionMagnitude=bound; limited=true; }
    }
    Vec3 j{}, opposite{}, f{}, inverseForce{};
    for(unsigned i=0;i<3;++i) {
      j[i]=jmag*c.normal[i]; opposite[i]=-j[i];
      f[i]=tangent[i]*frictionMagnitude; inverseForce[i]=-f[i];
    }
    if (c.resting) {
      for(unsigned i=0;i<3;++i) { j[i]=normalForce*c.normal[i]; opposite[i]=-j[i]; }
      if (da) force(a,ra,j);
      if (db) force(b,rb,opposite);
      c.normalForce+=normalForce;
    } else {
      if (da) impulse(a,ra,j);
      if (db) impulse(b,rb,opposite);
      c.normalImpulse+=jmag;
    }
    if (da) force(a,ra,inverseForce);
    if (db) force(b,rb,f);
    c.frictionForce+=frictionMagnitude;
    result=limited ? 2 : (jmag!=0 ? 1 : 0);
  }
  const auto finish = [&](int status) {
    if (!status) return status;
    for (Body* body : {da ? &a : nullptr, db ? &b : nullptr}) {
      if (!body) continue;
      body->state[body->current ^ 1].stepFlags &= ~1u;
      // FUN_00496050 -> FUN_004a7990 wakes only an actually corrected
      // contact. Multi-root propagates this to the registered descendants.
      if (body->flags & kBodyAsleep) {
        body->flags &= ~kBodyAsleep;
        body->activity = {0, 100000, 0};
      }
    }
    ++world.corrections;
    return status;
  };
  // Positional correction: stock 00496050 (all.c 111158-111198) applies a
  // shift at body+0x7c+cur*0xd4 and splits it 0.5/-0.5 when both bodies are
  // dynamic. _DAT_005f1428 is 0.021f (raw exe bytes 31 08 ac 3c,
  // REA ev_96d450eee0caa783a7383d8404dafd1956534629a5d26e585756076cbb8bbcd1).
  if (!(c.separation < 0.021f)) {
    return finish(result);
  }
  Vec3& sa = a.state[a.current].shift; Vec3& sb = b.state[b.current].shift;
  const float amount = (0.021f-c.separation) - ((sa[0]*c.normal[0]+sa[1]*c.normal[1]+sa[2]*c.normal[2]) - (sb[0]*c.normal[0]+sb[1]*c.normal[1]+sb[2]*c.normal[2]));
  if (!(amount > 0)) {
    return finish(result);
  }
  for (unsigned i=0;i<3;++i) {
    if (da) sa[i] += (db ? amount * 0.5f : amount) * c.normal[i];
    if (db) sb[i] += (da ? amount * -0.5f : -amount) * c.normal[i];
  }
  return finish(1);
}
}
