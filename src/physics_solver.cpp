#include "physics_solver.hpp"

namespace physics {
namespace {
bool fixed(const Body& b) { return (b.proto->flags & 4u) != 0; }
bool unavailable(const Body& b) { return fixed(b) || (b.flags & kBodyAsleep) != 0; }
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
Vec3 transform(const Mat34& m, const Vec3& v) {
  return {m[0]*v[0]+m[1]*v[1]+m[2]*v[2],
          m[4]*v[0]+m[5]*v[1]+m[6]*v[2],
          m[8]*v[0]+m[9]*v[1]+m[10]*v[2]};
}
Vec3 pointVelocity(const Body& b, const Vec3& offset) {
  if (b.attachment) {
    const auto& attachment = *b.attachment;
    const Vec3 rotated = transform(attachment.relative.rotation(), offset);
    const Vec3 parentOffset{rotated[0] + attachment.relative.position[0],
                            rotated[1] + attachment.relative.position[1],
                            rotated[2] + attachment.relative.position[2]};
    return pointVelocity(*attachment.parent, parentOffset);
  }
  const State& s = b.state[b.current ^ 1u];
  const Vec3 r = transform(s.rot, offset);
  const Vec3 angular = cross(s.omega, r);
  return {s.vel[0]+angular[0], s.vel[1]+angular[1], s.vel[2]+angular[2]};
}
Vec3 angularResponse(const Body& b, const Vec3& offset, const Vec3& normal) {
  const State& s = b.state[b.current];
  const Vec3 r = transform(s.rot, offset);
  const Vec3 inverseAngular = transform(s.invInertiaWorld, cross(r, normal));
  return cross(inverseAngular, r);
}
void apply(Body& b, const Vec3& offset, const Vec3& impulse) {
  if (fixed(b)) return;
  State& s = b.state[b.current];
  for (unsigned i=0; i!=3; ++i) s.vel[i] += b.proto->invMass * impulse[i];
  const Vec3 torque = cross(transform(s.rot, offset), impulse);
  for (unsigned i=0; i!=3; ++i) s.angMom[i] += torque[i];
  s.omega = transform(s.invInertiaWorld, s.angMom);
}
}
ContactSolveResult solveContacts(std::vector<Contact>& contacts) {
  for (const auto& c : contacts) {
    if (!c.first || !c.second || !c.first->proto || !c.second->proto ||
        c.first->current > 1 || c.second->current > 1)
      return ContactSolveResult::InvalidBody;
    for (const Body* body : {c.first, c.second}) {
      const Body* slow = body;
      const Body* fast = body;
      while (fast->attachment && fast->attachment->parent &&
             fast->attachment->parent->attachment) {
        slow = slow->attachment->parent;
        fast = fast->attachment->parent->attachment->parent;
        if (!fast || slow == fast) return ContactSolveResult::InvalidBody;
      }
      while (body) {
        if (!body->proto || body->current > 1 ||
            bool(body->flags & kBodyChild) != bool(body->attachment))
          return ContactSolveResult::InvalidBody;
        if (!body->attachment) break;
        body = body->attachment->parent;
        if (!body) return ContactSolveResult::InvalidBody;
      }
    }
  }
  for (auto& c : contacts) {
    Body& a = *c.first;
    Body& b = *c.second;
    if (unavailable(a) && unavailable(b)) {
      c.target = 0;
      c.impulse = 0;
      continue;
    }
    const Vec3 av = pointVelocity(a, c.firstOffset);
    const Vec3 bv = pointVelocity(b, c.secondOffset);
    const float speed = (av[0] - bv[0]) * c.normal[0] +
                        (av[1] - bv[1]) * c.normal[1] +
                        (av[2] - bv[2]) * c.normal[2];
    if (speed < -0.2f) c.target = -((speed - (-0.2f)) * (c.bounce + 1.0f));
    const Vec3 aa = angularResponse(a, c.firstOffset, c.normal);
    const Vec3 ba = angularResponse(b, c.secondOffset, c.normal);
    const float denominator = a.proto->invMass + b.proto->invMass +
        (ba[0]+aa[0])*c.normal[0] + (ba[2]+aa[2])*c.normal[2] +
        (ba[1]+aa[1])*c.normal[1];
    c.impulse = c.target / denominator;
  }
  for (const auto& c : contacts) {
    Vec3 impulse{c.impulse*c.normal[0], c.impulse*c.normal[1], c.impulse*c.normal[2]};
    apply(*c.first, c.firstOffset, impulse);
    impulse = {-impulse[0], -impulse[1], -impulse[2]};
    apply(*c.second, c.secondOffset, impulse);
  }
  return ContactSolveResult::Solved;
}
} // namespace physics
