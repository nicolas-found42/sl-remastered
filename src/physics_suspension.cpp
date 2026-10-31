#include "physics_suspension.hpp"
#include "run_log.hpp"

#include <cmath>

namespace physics {

SuspensionSettings configureSuspension(float force, float damping, float restLength,
                                       float maxLength, float minLength,
                                       float reboundDamping, float auxiliaryLength) {
  return {force, damping, restLength,
          maxLength > 0.0f ? maxLength : restLength,
          minLength > 0.0f ? minLength : 0.0f,
          auxiliaryLength > 0.0f ? auxiliaryLength : restLength,
          auxiliaryLength > 0.0f ? auxiliaryLength : 0.0f,
          reboundDamping <= 0.0f ? damping * 0.7f : reboundDamping};
}

SuspensionScalarResult suspensionScalar(const SuspensionScalarInput& input) {
  const auto& s = input.settings;
  const float nextDistanceSquared = s.maxLength * s.maxLength + 0.1f;
  if (s.maxLength * s.maxLength <= input.distanceSquared) {
    return {s.maxLength, nextDistanceSquared, 0.0f, 0.0f, false};
  }
  float length = std::sqrt(input.distanceSquared);
  if (s.maxLength < length) length = s.maxLength;
  float previous = input.previousLength;
  if (s.maxLength < previous) previous = s.maxLength;
  const float velocity = input.projectedVelocity + input.projectedVelocity;
  const float damping = velocity >= 0.0f ? s.reboundDamping : s.damping;
  const float dampingForce = damping * velocity;
  const float work = std::fabs(dampingForce * (length - previous));
  const float force = input.accumulatedForce -
                      (dampingForce + (length - s.restLength) * s.force);
  if (force < 0.0f) return {length, nextDistanceSquared, 0.0f, work, false};
  return {length, nextDistanceSquared, force, work, true};
}

} // namespace physics

namespace physics {
namespace {
float dot(const Vec3& a, const Vec3& b) {
  return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
Body* rootPoint(Body* b, Vec3& p) {
  while (b->attachment) {
    const auto& a = *b->attachment;
    p = matrixTransformRows(a.relative.rotation(), p);
    for (int i = 0; i < 3; ++i) p[i] += a.relative.position[i];
    b = a.parent;
  }
  return b;
}
bool validChain(Body* b) {
  Body* slow = b; Body* fast = b;
  while (fast && fast->attachment) {
    fast = fast->attachment->parent;
    if (fast && fast->attachment) fast = fast->attachment->parent;
    else break;
    slow = slow->attachment->parent;
    if (slow == fast) return false;
  }
  for (; b; b = b->attachment ? b->attachment->parent : nullptr) {
    if (!b->proto || b->current > 1 || (b->flags & kBodyAsleep) ||
        bool(b->flags & kBodyChild) != bool(b->attachment) ||
        (b->attachment && !b->attachment->parent)) return false;
  }
  return true;
}
Body* contactRoot(Body* body) {
  Body* slow = body;
  Body* fast = body;
  while (fast && fast->attachment) {
    fast = fast->attachment->parent;
    if (fast && fast->attachment) fast = fast->attachment->parent;
    else break;
    slow = slow->attachment ? slow->attachment->parent : nullptr;
    if (slow == fast) return nullptr;
  }
  while (body && body->attachment) body = body->attachment->parent;
  return body;
}
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
float effectiveMass(Body* b, Vec3 p, const Vec3& axis) {
  if (b->proto->flags & kProtoStatic) return 0;
  b = rootPoint(b, p);
  const State& s = b->state[b->current];
  const Vec3 r = matrixTransformRows(s.rot, p);
  const Vec3 angular = matrixTransformRows(s.invInertiaWorld, cross(r, axis));
  return b->proto->invMass + dot(cross(angular, r), axis);
}
Vec3 pointVelocity(const Body& b, const Vec3& p) {
  Vec3 rootOffset = p;
  Body* root = rootPoint(const_cast<Body*>(&b), rootOffset);
  const State& s = root->state[root->current];
  const Vec3 r = matrixTransformRows(s.rot, rootOffset);
  return {r[2]*s.omega[1] - r[1]*s.omega[2] + s.vel[0],
          r[0]*s.omega[2] - r[2]*s.omega[0] + s.vel[1],
          r[1]*s.omega[0] - r[0]*s.omega[1] + s.vel[2]};
}
}

SuspensionContactResult
selectSuspensionContact(SuspensionUpdate &u, const SuspensionContactCandidate &c,
                        SuspensionSelectionDiag *diag) {
  if (!u.wheelBody || !c.a || !c.b || !c.otherOwner || !c.otherPrototype ||
      c.otherOwner->proto != c.otherPrototype.get()) return SuspensionContactResult::Unsupported;
  const bool wheelFirst = c.a == u.wheelBody;
  if (!wheelFirst && c.b != u.wheelBody) return SuspensionContactResult::Unsupported;
  Body* other = wheelFirst ? c.b : c.a;
  if (other != c.otherOwner.get()) return SuspensionContactResult::Unsupported;
  Body* aRoot = contactRoot(u.wheelBody);
  Body* bRoot = contactRoot(other);
  if (!aRoot || !bRoot) return SuspensionContactResult::Unsupported;
  if (aRoot == bRoot) return SuspensionContactResult::Rejected;
  // Until ownership of an attached contact body's parent is modeled, only a
  // root can be retained safely across calls.
  if (other->attachment || (other->flags & kBodyChild)) return SuspensionContactResult::Unsupported;
  const auto finiteVec = [](const Vec3& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
  };
  if (!finiteVec(c.pointA) || !finiteVec(c.pointB) || !finiteVec(c.normal) ||
      !finiteVec(u.anchor) || !std::isfinite(u.length) ||
      !std::isfinite(u.settings.auxiliaryLength) || !std::isfinite(u.distanceSquared))
    return SuspensionContactResult::Unsupported;
  const Vec3& wheelPoint = wheelFirst ? c.pointA : c.pointB;
  const float x = u.anchor[0] - wheelPoint[0];
  const float y = u.anchor[1] - wheelPoint[1];
  const float z = u.anchor[2] - wheelPoint[2];
  const float distanceSquared = x*x + y*y + z*z;
  if (!std::isfinite(distanceSquared)) return SuspensionContactResult::Unsupported;
  if (u.length - std::sqrt(distanceSquared) > u.settings.auxiliaryLength ||
      !(distanceSquared < u.distanceSquared)) {
    if (diag) {
      diag->length = u.length;
      diag->distance = std::sqrt(distanceSquared);
      diag->auxiliary = u.settings.auxiliaryLength;
      diag->storedDistanceSquared = u.distanceSquared;
    }
    {
      static double lastRejectLog = -1e9;
      static unsigned rejectCount = 0;
      const double nowRejectLog = runlog::monotonicSeconds();
      if (nowRejectLog - lastRejectLog >= 2.0 && rejectCount < 6) {
        lastRejectLog = nowRejectLog;
        ++rejectCount;
        runlog::event(runlog::EventKind::Note,
                      "susp reject: anchorY=%.3f pointAY=%.3f dist=%.3f "
                      "max=%.3f len=%.3f stored=%.3f",
                      u.anchor[1], wheelPoint[1], std::sqrt(distanceSquared),
                      u.settings.maxLength, u.length,
                      std::sqrt(u.distanceSquared));
      }
    }
    return SuspensionContactResult::Rejected;
  }
  u.distanceSquared = distanceSquared;
  if (diag) {
    diag->length = u.length;
    diag->distance = std::sqrt(distanceSquared);
    diag->auxiliary = u.settings.auxiliaryLength;
    diag->storedDistanceSquared = distanceSquared;
  }
  u.contactOwner = c.otherOwner;
  u.contactPrototype = c.otherPrototype;
  u.contactBody = other;
  u.contactPoint = wheelFirst ? c.pointB : c.pointA;
  u.normal = c.normal;
  if (c.surface) {
    u.contactFeature = c.surface->featureIndex;
    u.contactSurface = c.surface;
  } else {
    u.contactFeature = 0;
  }
  return SuspensionContactResult::Selected;
}

SuspensionUpdateResult updateSuspension(SuspensionUpdate& u) {
  Body* a = u.wheelBody;
  Body* b = u.contactBody;
  if (!a || !validChain(a) || (b && !validChain(b)))
    return SuspensionUpdateResult::Unsupported;
  if ((a->proto->flags & kProtoStatic) && (!b || (b->proto->flags & kProtoStatic)))
    return SuspensionUpdateResult::Unsupported;

  const auto& s = u.settings;
  const bool separated = !b || s.maxLength*s.maxLength <= u.distanceSquared;
  Vec3 axis{}, p{}, va{}, vb{};
  float velocity = 0;
  if (!separated) {
    float length = std::sqrt(u.distanceSquared);
    if (s.maxLength < length) length = s.maxLength;
    p = {u.anchor[0], u.anchor[1] - length, u.anchor[2]};
    va = pointVelocity(*a, p);
    vb = pointVelocity(*b, u.contactPoint);
    const State& as = a->state[a->current];
    axis = {as.rot[1], as.rot[5], as.rot[9]};
    if (dot(axis, u.normal) >= 0) {
      const Vec3 d{va[0]-vb[0], va[1]-vb[1], va[2]-vb[2]};
      const float projection = -dot(d, u.normal);
      const Vec3 corrected{projection*u.normal[0]+va[0], projection*u.normal[1]+va[1], projection*u.normal[2]+va[2]};
      velocity = (d[1]-corrected[1])*axis[1] + (d[2]-corrected[2])*axis[2] + (d[0]-corrected[0])*axis[0];
      if (length < s.minLength && velocity < 0 &&
          (u.timeStep <= 0 || effectiveMass(a, p, axis) + effectiveMass(b, u.contactPoint, axis) == 0))
        return SuspensionUpdateResult::Unsupported;
    }
  }
  u.normalForce = {};
  u.dampingWork = 0;
  auto clear = [&] { u.lateralForce = {}; u.accumulatedForce = 0; };
  if (separated) {
    u.length = s.maxLength; u.force = 0; u.projectedVelocity = 0;
    u.distanceSquared = s.maxLength*s.maxLength + 0.1f;
    clear(); return SuspensionUpdateResult::Inactive;
  }
  const float previous = u.length;
  const float distanceSquared = u.distanceSquared;
  u.length = std::sqrt(u.distanceSquared);
  u.distanceSquared = s.maxLength*s.maxLength + 0.1f;
  if (s.maxLength < u.length) u.length = s.maxLength;
  u.wheelPoint = p;
  if (dot(axis, u.normal) < 0) { u.force = 0; clear(); return SuspensionUpdateResult::Inactive; }
  const auto scalar = suspensionScalar({s, distanceSquared, previous, velocity, u.accumulatedForce});
  u.projectedVelocity = velocity; u.dampingWork = scalar.dampingWork; u.force = scalar.force;
  if (!scalar.active) { clear(); return SuspensionUpdateResult::Inactive; }
  Vec3 normalApplied{u.force*axis[0], u.force*axis[1], u.force*axis[2]};
  float totalForce = u.force;
  if (u.length < s.minLength && velocity < 0) {
    const float mass = effectiveMass(a, p, axis) + effectiveMass(b, u.contactPoint, axis);
    totalForce += (1.0f / u.timeStep) * (-velocity / mass);
  }
  Vec3 applied{totalForce*axis[0], totalForce*axis[1], totalForce*axis[2]};
  const State& as = a->state[a->current];
  const Vec3 zAxis{as.rot[2], as.rot[6], as.rot[10]};
  const float zDot = dot(zAxis, u.normal);
  if (std::fabs(zDot) > 0.0001f) {
    const Vec3 t{zAxis[0]-zDot*u.normal[0], zAxis[1]-zDot*u.normal[1], zAxis[2]-zDot*u.normal[2]};
    float f = dot(t, normalApplied);
    for (int i=0;i<3;i++) normalApplied[i] -= t[i]*f;
    f = dot(t, applied);
    for (int i=0;i<3;i++) applied[i] -= t[i]*f;
  }
  if (dot(u.lateralForce, u.lateralForce) > 0.0001f) {
    Vec3 t = matrixTransformRows(as.rot, u.lateralForce);
    const float f = dot(t, u.normal);
    for (int i=0;i<3;i++) normalApplied[i] -= t[i] - f*u.normal[i];
  }
  if (dot(applied, applied) > 0.00001f) {
    const float f = dot(normalApplied, u.normal);
    for (int i=0;i<3;i++) u.normalForce[i] = f*u.normal[i];
    Vec3 rootOffset = p;
    if (u.forcePointThreshold != 0) {
      const Vec3 xAxis{as.rot[0], as.rot[4], as.rot[8]};
      const float d = dot(xAxis, u.normal);
      const float factor = std::fabs(d) <= u.forcePointThreshold ? d / u.forcePointThreshold : (d < 0 ? -1.0f : d > 0 ? 1.0f : 0.0f);
      rootOffset[0] += u.forcePointOffset - factor * u.forcePointScale;
    }
    Body* root = rootPoint(a, rootOffset);
    Vec3 offset = matrixTransformRows(root->state[root->current].rot, rootOffset);
    addForce(*root, applied, &offset);
    root->state[root->current ^ 1].stepFlags &= ~1u;
    rootOffset = u.contactPoint;
    root = rootPoint(b, rootOffset);
    if (!(root->proto->flags & kProtoStatic)) {
      Vec3 opposite{-applied[0],-applied[1],-applied[2]};
      offset = matrixTransformRows(root->state[root->current].rot, rootOffset);
      addForce(*root, opposite, &offset);
      root->state[root->current ^ 1].stepFlags &= ~1u;
    }
  }
  clear(); return SuspensionUpdateResult::Active;
}
}
