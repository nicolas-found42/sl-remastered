#include "physics_body.hpp"

#include <cmath>

namespace physics {

// ------------------------------------------------------------------ math
Mat34 matrixIdentity() {
  Mat34 m{};
  m[0] = m[5] = m[10] = 1.0f;
  return m;
}

Mat34 matrixZero() { return Mat34{}; }

Mat34 matrixTranspose(const Mat34& a) {
  Mat34 r{};
  r[0] = a[0]; r[1] = a[4]; r[2] = a[8];
  r[4] = a[1]; r[5] = a[5]; r[6] = a[9];
  r[8] = a[2]; r[9] = a[6]; r[10] = a[10];
  return r;
}

Mat34 matrixMultiply(const Mat34& b_, const Mat34& c_) {
  // 0054e9c0: this = param_2 * param_3. Sum order follows the decompile.
  const Mat34& a = b_;
  const Mat34& b = c_;
  Mat34 r{};
  r[0] = a[2] * b[8] + a[1] * b[4] + a[0] * b[0];
  r[1] = b[9] * a[2] + b[1] * a[0] + a[1] * b[5];
  r[2] = b[10] * a[2] + b[2] * a[0] + a[1] * b[6];
  r[4] = a[4] * b[0] + a[5] * b[4] + a[6] * b[8];
  r[5] = a[4] * b[1] + a[5] * b[5] + a[6] * b[9];
  r[6] = a[4] * b[2] + a[5] * b[6] + a[6] * b[10];
  r[8] = a[9] * b[4] + a[10] * b[8] + a[8] * b[0];
  r[9] = b[1] * a[8] + b[5] * a[9] + b[9] * a[10];
  r[10] = b[2] * a[8] + b[6] * a[9] + b[10] * a[10];
  return r;
}

Mat34 matrixInverse(const Mat34& m) {
  // 0054e3b0 writes the cofactor matrix; 0054e510 then scales the diagonal by
  // 1/det and swaps (transposes) the off-diagonal entries. Padding stays zero
  // here (stock leaves it as stack garbage).
  Mat34 c{};
  c[0] = m[5] * m[10] - m[9] * m[6];
  c[1] = m[8] * m[6] - m[4] * m[10];
  c[2] = m[9] * m[4] - m[8] * m[5];
  c[4] = m[9] * m[2] - m[1] * m[10];
  c[5] = m[0] * m[10] - m[8] * m[2];
  c[6] = m[8] * m[1] - m[9] * m[0];
  c[8] = m[1] * m[6] - m[2] * m[5];
  c[9] = m[4] * m[2] - m[0] * m[6];
  c[10] = m[5] * m[0] - m[4] * m[1];
  const float det = (((m[8] * m[1] * m[6] + m[2] * m[4] * m[9] + m[10] * m[0] * m[5]) -
                      m[0] * m[9] * m[6]) -
                     m[10] * m[1] * m[4]) -
                    m[8] * m[2] * m[5];
  const float s = k::one / det;
  Mat34 r{};
  r[0] = s * c[0];
  r[5] = s * c[5];
  r[10] = s * c[10];
  r[1] = s * c[4];
  r[4] = s * c[1];
  r[2] = s * c[8];
  r[8] = s * c[2];
  r[6] = s * c[9];
  r[9] = s * c[6];
  return r;
}

Mat34 matrixFromQuat(const Quat& q) {
  const float x2 = q[0] + q[0];
  const float y2 = q[1] + q[1];
  const float z2 = q[2] + q[2];
  const float x2x = x2 * q[0];
  const float x2w = x2 * q[3];
  Mat34 m{};
  m[0] = k::one - (z2 * q[2] + y2 * q[1]);
  m[1] = y2 * q[0] - z2 * q[3];
  m[2] = y2 * q[3] + z2 * q[0];
  m[4] = z2 * q[3] + y2 * q[0];
  m[5] = k::one - (z2 * q[2] + x2x);
  m[6] = z2 * q[1] - x2w;
  m[8] = z2 * q[0] - y2 * q[3];
  m[9] = x2w + z2 * q[1];
  m[10] = k::one - (y2 * q[1] + x2x);
  return m;
}

Quat quatFromMatrix(const Mat34& m) {
  const float trace = m[5] + m[0] + m[10];
  Quat q{};
  if (trace > 0.0) {
    const float root = std::sqrt(trace + 1.0f);
    q[3] = static_cast<float>(root * 0.5f);
    const float scale = 0.5f / root;
    q[0] = static_cast<float>((m[9] - m[6]) * scale);
    q[1] = static_cast<float>((m[2] - m[8]) * scale);
    q[2] = static_cast<float>((m[4] - m[1]) * scale);
    return q;
  }
  const size_t next[] = {1, 2, 0};
  size_t i = m[0] < m[5] ? 1 : 0;
  if (m[i * 5] < m[10]) i = 2;
  const size_t j = next[i], l = next[j];
  float root = std::sqrt((m[i * 5] - (m[l * 5] + m[j * 5])) + 1.0f);
  q[i] = static_cast<float>(root * 0.5f);
  if (root != 0.0) root = 0.5f / root;
  q[3] = static_cast<float>((m[l * 4 + j] - m[j * 4 + l]) * root);
  q[j] = static_cast<float>((m[j * 4 + i] + m[i * 4 + j]) * root);
  q[l] = static_cast<float>((m[l * 4 + i] + m[i * 4 + l]) * root);
  return q;
}

Quat quatMultiply(const Quat& a, const Quat& b) {
  // 0054fb50, a 9-multiply product. Temporaries keep the stock names' roles.
  const float t13 = (b[0] + b[1]) * (a[0] + a[2]);
  const float t14 = (b[0] - b[1]) * (a[0] - a[2]);
  const float t15 = (a[3] + a[1]) * (b[3] - b[2]);
  const float t16 = (a[3] - a[1]) * (b[3] + b[2]);
  Quat r;
  r[0] = (a[3] + a[0]) * (b[3] + b[0]) - (t16 + t15 + t14 + t13) * k::half;
  r[1] = (((t13 - t14) + t15) - t16) * k::half + (b[2] + b[1]) * (a[3] - a[0]);
  r[2] = (((t13 - t14) - t15) + t16) * k::half + (a[1] + a[2]) * (b[3] - b[0]);
  r[3] = ((t16 - (t14 + t13)) + t15) * k::half + (b[1] - b[2]) * (a[2] - a[1]);
  return r;
}

Quat quatNormalize(const Quat& q) {
  float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (len == k::zero) return Quat{0.0f, 0.0f, 0.0f, 1.0f};
  const float inv = k::one / len;
  return Quat{inv * q[0], inv * q[1], inv * q[2], inv * q[3]};
}

Quat quatInverse(const Quat& q) {
  const float scale = 1.0f / (q[0]*q[0] + q[3]*q[3] + q[1]*q[1] + q[2]*q[2]);
  return {-q[0]*scale, -q[1]*scale, -q[2]*scale, q[3]*scale};
}

Vec3 matrixTransformRows(const Mat34& m, const Vec3& v) {
  return Vec3{m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
              m[5] * v[1] + m[4] * v[0] + m[6] * v[2],
              m[9] * v[1] + m[8] * v[0] + m[10] * v[2]};
}

Vec3 transformPoint(const Vec3& pos, const Mat34& rot, const Vec3& v) {
  // 004b1460: the stock sums the first row as x*r0 + r2*z + r1*y.
  Vec3 r{v[0] * rot[0] + rot[2] * v[2] + rot[1] * v[1],
         rot[6] * v[2] + rot[4] * v[0] + rot[5] * v[1],
         rot[10] * v[2] + rot[8] * v[0] + rot[9] * v[1]};
  r[0] = pos[0] + r[0];
  r[1] = pos[1] + r[1];
  r[2] = pos[2] + r[2];
  return r;
}

// -------------------------------------------------------------- geometry
namespace {

// 004a6010. Fills invInertia, inertia and the shape volume. Returns false for
// the Mesh type, whose inertia comes from 004aa330 (not read).
bool computeMassProperties(BodyProto& p) {
  Shape& s = p.shape;
  if (p.invMass == k::zero) {
    p.invInertia = matrixZero();
    p.inertia = matrixZero();
    return true;
  }
  const float mass = k::one / p.invMass;
  Mat34 inv{};
  switch (s.type) {
    case ShapeType::Empty:
    case ShapeType::Sphere: {
      const float d = k::sphereInertiaFactor / (s.dim[0] * s.dim[0] * mass);
      inv[0] = d;
      inv[5] = d;
      inv[10] = d;
      s.volume = s.dim[0] * s.dim[0] * s.dim[0] * k::sphereVolumeFactor;
      break;
    }
    case ShapeType::CylinderX:
    case ShapeType::CylinderY:
    case ShapeType::CylinderZ: {
      const float axis = k::cylinderAxisFactor / (s.radius * s.radius * mass);
      const float len = s.dim[0] + s.dim[0];
      const float perp = k::one / ((s.radius * s.radius * k::quarter + len * len * k::twelfth) * mass);
      if (s.type == ShapeType::CylinderX) {
        inv[0] = axis; inv[5] = perp; inv[10] = perp;
      } else if (s.type == ShapeType::CylinderY) {
        inv[0] = perp; inv[5] = axis; inv[10] = perp;
      } else {
        inv[0] = perp; inv[5] = perp; inv[10] = axis;
      }
      s.volume = 0;
      break;
    }
    case ShapeType::Box: {
      const float hx = s.dim[0], hy = s.dim[1], hz = s.dim[2];
      inv[0] = k::boxInertiaFactor / ((hy * hy + hz * hz) * mass);
      inv[5] = k::boxInertiaFactor / ((hx * hx + hz * hz) * mass);
      inv[10] = k::boxInertiaFactor / ((hx * hx + hy * hy) * mass);
      s.volume = hz * hy * hx * k::boxVolumeFactor;
      break;
    }
    case ShapeType::Mesh:
      return false;
    case ShapeType::Hull:
      // 004a6010 has no case 7: the default arm only inverts the matrix that
      // is already stored.
      p.inertia = matrixInverse(p.invInertia);
      return true;
  }
  s.centerOffset[0] = s.centerOffset[1] = s.centerOffset[2] = 0;
  p.invInertia = inv;
  p.inertia = matrixInverse(inv);
  return true;
}

// 004a6350(1, radius): inertia of the stand-in sphere used for vertex clouds.
void computeStandInSphere(BodyProto& p, float radius) {
  Shape& s = p.shape;
  if (p.invMass == k::zero) {
    p.invInertia = matrixZero();
    p.inertia = matrixZero();
    return;
  }
  const float mass = k::one / p.invMass;
  const float d = k::sphereInertiaFactor / (mass * radius);
  Mat34 inv{};
  inv[0] = d;
  inv[5] = d;
  inv[10] = d;
  s.centerOffset[0] = s.centerOffset[1] = s.centerOffset[2] = 0;
  s.volume = radius * radius * radius * k::sphereVolumeFactor;
  p.invInertia = inv;
  p.inertia = matrixInverse(inv);
}

float maxVertexRadius(const std::vector<Vec3>& v) {
  float best = k::zero;
  for (const auto& p : v) {
    const float sq = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
    if (best < sq) best = sq;
  }
  return std::sqrt(best) + k::skin;
}

}  // namespace

std::optional<BodyProto> createBodyProto(float mass, uint32_t type, float p4, float p5, float p6,
                                         const std::vector<Vec3>* meshVertices,
                                         std::string& error) {
  if (type > 7) {
    error = "physics shape type out of range";
    return std::nullopt;
  }
  BodyProto p;
  Shape& s = p.shape;
  s.type = static_cast<ShapeType>(type);
  p.flags = kProtoCreated;
  if (mass == k::zero) {
    p.invMass = 0;
    p.linearDrag = 0;
    p.angularDrag = 0;
    p.flags |= kProtoStatic;
  } else {
    p.invMass = k::one / mass;
    p.linearDrag = mass * k::linearDragPerMass;
    p.angularDrag = mass * k::angularDragPerMass;
  }
  p.flags |= kProtoAlways;
  const float s05 = k::skin;
  switch (s.type) {
    case ShapeType::Empty:
      s.volume = 0;
      p.invInertia = matrixZero();
      p.inertia = matrixZero();
      s.extent[0] = s.extent[1] = s.extent[2] = 0;
      break;
    case ShapeType::Sphere: {
      const float r = p4 + s05;
      s.dim[0] = p4;
      s.radius = p4;
      s.boundRadius = r;
      p.massPropertiesSupported = computeMassProperties(p);
      s.extent[0] = s.extent[1] = s.extent[2] = r;
      break;
    }
    case ShapeType::CylinderX:
    case ShapeType::CylinderY:
    case ShapeType::CylinderZ: {
      s.radius = p5;
      const float total = p4 + p5 + s05;
      s.dim[0] = p4;
      s.boundRadius = total;
      p.massPropertiesSupported = computeMassProperties(p);
      const float side = p5 + s05;
      s.extent[0] = s.extent[1] = s.extent[2] = side;
      s.extent[static_cast<size_t>(type) - 2] = total;
      break;
    }
    case ShapeType::Box: {
      s.dim[0] = p4;
      s.dim[1] = p5;
      s.dim[2] = p6;
      s.radius = 0;
      s.boundRadius = std::sqrt(p6 * p6 + p5 * p5 + p4 * p4) + s05;
      p.massPropertiesSupported = computeMassProperties(p);
      s.extent[0] = p4 + s05;
      s.extent[1] = p5 + s05;
      s.extent[2] = p6 + s05;
      break;
    }
    case ShapeType::Mesh: {
      if (!meshVertices) {
        computeStandInSphere(p, k::one);
        break;
      }
      s.vertices = *meshVertices;
      p.massPropertiesSupported = computeMassProperties(p);
      s.boundRadius = maxVertexRadius(s.vertices);
      break;
    }
    case ShapeType::Hull: {
      float radius = k::one;
      if (meshVertices) {
        s.vertices = *meshVertices;
        radius = maxVertexRadius(s.vertices);
        s.boundRadius = radius;
      }
      computeStandInSphere(p, radius);
      s.flags |= 1;
      s.volume = 0;
      break;
    }
  }
  p.volume = s.volume;
  return p;
}

// ---------------------------------------------------------------- bodies
void updateWorldInertia(const BodyProto& proto, State& s) {
  const Mat34 rt = matrixTranspose(s.rot);
  const Mat34 tmp = matrixMultiply(proto.invInertia, rt);
  s.invInertiaWorld = matrixMultiply(s.rot, tmp);
}

Body createBody(const BodyProto& proto, const Vec3& pos, const Mat34* orientation, float creationTime) {
  Body b;
  b.proto = &proto;
  State& s = b.state[0];
  s.pos = pos;
  s.time = creationTime;
  s.stepFlags |= 1u;
  s.quat = orientation ? quatFromMatrix(*orientation) : Quat{0, 0, 0, 1};
  s.rot = orientation ? matrixFromQuat(s.quat) : matrixIdentity();
  if ((proto.flags & kProtoStatic) == 0) b.flags |= kBodyActive;
  if (proto.flags & kProtoBit8) b.flags |= 0x20;
  if (proto.flags & kProtoNoRotation) b.flags |= kBodyNoRotation;
  if (proto.flags & kProtoNoGravity) b.flags |= kBodyNoGravity;
  updateWorldInertia(proto, s);
  b.state[1] = b.state[0];
  return b;
}

void addForce(Body& body, const Vec3& f, const Vec3* p) {
  State& s = body.state[body.current];
  s.force[0] = s.force[0] + f[0];
  s.force[1] = f[1] + s.force[1];
  s.force[2] = s.force[2] + f[2];
  if (p) {
    s.torque[0] = (p[0][1] * f[2] - p[0][2] * f[1]) + s.torque[0];
    s.torque[1] = (p[0][2] * f[0] - p[0][0] * f[2]) + s.torque[1];
    s.torque[2] = (f[1] * p[0][0] - p[0][1] * f[0]) + s.torque[2];
  }
}

bool accumulateWorldForces(const WorldParams& world, Body& body) {
  if ((body.flags & kBodyActive) == 0 || (body.flags & kBodyAsleep) != 0) return false;
  const BodyProto& proto = *body.proto;
  if (proto.flags & kProtoStatic) return false;
  if ((body.flags & kBodyChild) == 0 && proto.invMass == k::zero) return false;
  // Mark the other buffer as not yet produced by a step (state +0x3c bit 0).
  body.state[body.current ^ 1].stepFlags &= ~1u;
  State& s = body.state[body.current];
  if ((body.flags & kBodyChild) == 0) {
    if (proto.linearDrag < k::zero) {
      const Vec3 f{proto.linearDrag * s.vel[0], proto.linearDrag * s.vel[1],
                   proto.linearDrag * s.vel[2]};
      addForce(body, f, nullptr);
    }
    if (world.gravityEnabled && (body.flags & kBodyNoGravity) == 0) {
      const float mass = k::one / proto.invMass;
      const Vec3 f{mass * world.gravity[0], mass * world.gravity[1], mass * world.gravity[2]};
      addForce(body, f, nullptr);
    }
    if (proto.flags & kProtoBuoyant) {
      const float lift = -(proto.volume * world.airDensity);
      const Vec3 f{lift * world.gravity[0], lift * world.gravity[1], lift * world.gravity[2]};
      addForce(body, f, nullptr);
    }
  }
  return true;
}

namespace {

struct Deriv {
  Vec3 pos{}, vel{}, force{}, omega{}, angMom{}, torque{};
  Quat quat{0, 0, 0, 0};
};

Quat omegaTimesQuat(const Vec3& omega, const Quat& q, float scale) {
  // vec4(omega, 0) * q, then every component times `scale` (0054f730,
  // 0054fb50, 0054f7e0).
  Quat r = quatMultiply(Quat{omega[0], omega[1], omega[2], 0.0f}, q);
  for (float& c : r) c = scale * c;
  return r;
}

// 004a58e0: out = s * k + a.
Vec3 axpy(const Vec3& a, const Vec3& kv, float s) {
  return Vec3{s * kv[0] + a[0], s * kv[1] + a[1], s * kv[2] + a[2]};
}

// 004a9180 / the first stage of 004a7c10: derivative of a state.
Deriv derivative(const State& st, float invMass, float dt, float halfDt) {
  Deriv d;
  d.pos = Vec3{dt * st.vel[0], dt * st.vel[1], dt * st.vel[2]};
  const float f = dt * invMass;
  d.vel = Vec3{f * st.force[0], f * st.force[1], f * st.force[2]};
  d.quat = omegaTimesQuat(st.omega, st.quat, halfDt);
  d.angMom = Vec3{dt * st.torque[0], dt * st.torque[1], dt * st.torque[2]};
  return d;
}

State advance(const State& base, const Deriv& d, float s) {
  // The mid-point states of 004a7c10: base + s * derivative on the fields the
  // routine copies (position, velocity, force, quaternion, omega, momentum,
  // torque); other fields keep the base values.
  State r = base;
  r.pos = axpy(base.pos, d.pos, s);
  r.vel = axpy(base.vel, d.vel, s);
  r.force = axpy(base.force, d.force, s);
  for (size_t i = 0; i < 4; ++i) r.quat[i] = base.quat[i] + s * d.quat[i];
  r.omega = axpy(base.omega, d.omega, s);
  r.angMom = axpy(base.angMom, d.angMom, s);
  r.torque = axpy(base.torque, d.torque, s);
  return r;
}

// 004a9000: out = a + s * b on the same field set.
void accumulate(State& out, const State& a, const Deriv& b, float s) {
  out.pos = axpy(a.pos, b.pos, s);
  out.vel = axpy(a.vel, b.vel, s);
  out.force = axpy(a.force, b.force, s);
  for (size_t i = 0; i < 4; ++i) out.quat[i] = a.quat[i] + s * b.quat[i];
  out.omega = axpy(a.omega, b.omega, s);
  out.angMom = axpy(a.angMom, b.angMom, s);
  out.torque = axpy(a.torque, b.torque, s);
}

// The shared tail of 004a7c10 from address 004a82bd.
void finishState(const BodyProto& proto, const State& in, State& out, float dt) {
  out.pos[0] = out.pos[0] + in.shift[0];
  out.pos[1] = in.shift[1] + out.pos[1];
  out.pos[2] = in.shift[2] + out.pos[2];
  out.quat = quatNormalize(out.quat);
  out.rot = matrixFromQuat(out.quat);
  if (proto.angularDrag < k::zero) {
    const float f = proto.invMass * proto.angularDrag * dt * k::angularDampScale + k::one;
    out.angMom[0] = f * out.angMom[0];
    out.angMom[1] = f * out.angMom[1];
    out.angMom[2] = f * out.angMom[2];
  }
  out.time = dt + in.time;
  updateWorldInertia(proto, out);
  Vec3 w = matrixTransformRows(out.invInertiaWorld, out.angMom);
  // 004907e0 returns the squared length of its operand.
  const float limit = (w[0] * w[0] + w[1] * w[1] + w[2] * w[2]) * k::angularSpeedLimitScale;
  if (limit > k::one) {
    const float scale = k::one / limit;
    for (float& c : out.angMom) c = scale * c;
    w = matrixTransformRows(out.invInertiaWorld, out.angMom);
  }
  out.omega = w;
  const float speedSq = out.vel[0] * out.vel[0] + out.vel[1] * out.vel[1] + out.vel[2] * out.vel[2];
  if (speedSq > k::linearSpeedLimitSquared) {
    const float len = std::sqrt(out.vel[2] * out.vel[2] + out.vel[1] * out.vel[1] +
                                out.vel[0] * out.vel[0]);
    const float scale = k::linearSpeedLimit / len;
    for (float& c : out.vel) c = scale * c;
  }
}

}  // namespace

bool integrate(const BodyProto& proto, uint32_t bodyFlags, const State& in, State& out,
               float dt) {
  if (bodyFlags & kBodyChild) return false;
  if (bodyFlags & kBodyNoRotation) {
    if (proto.flags & kProtoSimple) {
      out.pos[0] = dt * in.vel[0] + in.pos[0];
      out.pos[1] = dt * in.vel[1] + in.pos[1];
      out.pos[2] = dt * in.vel[2] + in.pos[2];
      const float f = dt * proto.invMass;
      out.vel[0] = f * in.force[0] + in.vel[0];
      out.vel[1] = f * in.force[1] + in.vel[1];
      out.vel[2] = f * in.force[2] + in.vel[2];
      out.time = dt + in.time;
      return true;
    }
    // Single Euler step at 004a7dd9. The derivative slots for force, omega
    // and torque are untouched zero statics in stock, so those fields carry over.
    const Vec3 accel{proto.invMass * in.force[0], proto.invMass * in.force[1],
                     proto.invMass * in.force[2]};
    Quat dq = quatMultiply(Quat{in.omega[0], in.omega[1], in.omega[2], 0.0f}, in.quat);
    for (float& c : dq) c = k::half * c;
    out.pos = axpy(in.pos, in.vel, dt);
    out.vel = axpy(in.vel, accel, dt);
    out.force = in.force;
    for (size_t i = 0; i < 4; ++i) out.quat[i] = in.quat[i] + dt * dq[i];
    out.omega = in.omega;
    out.angMom = axpy(in.angMom, in.torque, dt);
    out.torque = in.torque;
    finishState(proto, in, out, dt);
    return true;
  }
  const float halfDt = dt * k::half;
  const Deriv k1 = derivative(in, proto.invMass, dt, halfDt);
  const State s2 = advance(in, k1, k::half);
  const Deriv k2 = derivative(s2, proto.invMass, dt, halfDt);
  const State s3 = advance(in, k2, k::half);
  const Deriv k3 = derivative(s3, proto.invMass, dt, halfDt);
  // 004a8f00: s4 = in + k3 (weight 1).
  State s4 = in;
  s4.pos = Vec3{in.pos[0] + k3.pos[0], in.pos[1] + k3.pos[1], in.pos[2] + k3.pos[2]};
  s4.vel = Vec3{in.vel[0] + k3.vel[0], in.vel[1] + k3.vel[1], in.vel[2] + k3.vel[2]};
  s4.force = Vec3{in.force[0] + k3.force[0], in.force[1] + k3.force[1], in.force[2] + k3.force[2]};
  for (size_t i = 0; i < 4; ++i) s4.quat[i] = in.quat[i] + k3.quat[i];
  s4.omega = Vec3{in.omega[0] + k3.omega[0], in.omega[1] + k3.omega[1], in.omega[2] + k3.omega[2]};
  s4.angMom = Vec3{in.angMom[0] + k3.angMom[0], in.angMom[1] + k3.angMom[1], in.angMom[2] + k3.angMom[2]};
  s4.torque = Vec3{in.torque[0] + k3.torque[0], in.torque[1] + k3.torque[1], in.torque[2] + k3.torque[2]};
  const Deriv k4 = derivative(s4, proto.invMass, dt, halfDt);
  accumulate(out, in, k1, k::sixth);
  State t = out;
  accumulate(out, t, k2, k::third);
  t = out;
  accumulate(out, t, k3, k::third);
  t = out;
  accumulate(out, t, k4, k::sixth);
  finishState(proto, in, out, dt);
  return true;
}

bool stepBody(Body& body, float dt) {
  if ((body.flags & kBodyActive) == 0 || (body.flags & kBodyAsleep) != 0) return false;
  if (body.flags & (kBodyChild | kBodyHasChildren | kBodyBit10)) return false;
  const BodyProto& proto = *body.proto;
  State& out = body.state[body.current ^ 1];
  if (proto.flags & kProtoStatic) return false;
  if (out.stepFlags & 1u) return false;
  if (proto.invMass == k::zero) return false;
  if (!integrate(proto, body.flags, body.state[body.current], out, dt)) return false;
  out.stepFlags = (out.stepFlags & ~2u) | 1u;
  return true;
}

CommitResult commit(Body& body) {
  const BodyProto& proto = *body.proto;
  const uint32_t cur = body.current;
  const uint32_t nxt = cur ^ 1;
  // Outer gate of 004a4ed0: the body is active and either asleep/child (flags
  // 0x84) or its next state was produced by a step and it is not static.
  if ((body.flags & kBodyActive) == 0) return CommitResult::Unchanged;
  const uint32_t gate = body.flags & 0x84;
  if (gate == 0 && !((body.state[nxt].stepFlags & 1u) != 0 && (proto.flags & kProtoStatic) == 0))
    return CommitResult::Unchanged;
  if (gate == 0) {
    // Smoothed force and the rest test.
    const State& c = body.state[cur];
    const State& n = body.state[nxt];
    for (size_t i = 0; i < 3; ++i) body.activity[i] = body.activity[i] * k::restSmoothKeep;
    for (size_t i = 0; i < 3; ++i) body.activity[i] = c.force[i] * k::tenth + body.activity[i];
    const float velSq = n.vel[2] * n.vel[2] + n.vel[1] * n.vel[1] + n.vel[0] * n.vel[0];
    const float omegaSq = n.omega[2] * n.omega[2] + n.omega[1] * n.omega[1] + n.omega[0] * n.omega[0];
    const float tq = c.torque[2] * c.torque[2] + c.torque[1] * c.torque[1] + c.torque[0] * c.torque[0];
    const float act = body.activity[2] * body.activity[2] + body.activity[1] * body.activity[1] +
                      body.activity[0] * body.activity[0];
    const float m2 = proto.invMass * proto.invMass;
    if (velSq < k::tenth && omegaSq < k::restAngularSq && m2 * tq < k::restForceSq &&
        m2 * act < k::restForceSq)
      return (body.flags & kBodyAsleep) ? CommitResult::Unchanged : CommitResult::WentToSleep;
  }
  body.current = nxt;
  if ((body.flags & kBodyChild) == 0) {
    State& s = body.state[nxt];
    s.force = Vec3{0, 0, 0};
    s.shift = Vec3{0, 0, 0};
    s.torque = Vec3{0, 0, 0};
    if ((body.flags & kBodyNoRotation) == 0) updateWorldInertia(proto, s);
  }
  return CommitResult::Flipped;
}

void sleepBodyState(Body& body) {
  if (body.flags & kBodyAsleep) return;
  body.flags |= kBodyAsleep;
  State& current = body.state[body.current];
  current.vel = {};
  current.omega = {};
  current.angMom = {};
  current.force = {};
  current.torque = {};
  current.stepFlags &= ~2u;
  body.state[body.current ^ 1] = current;
}

FreeBodyStepResult World::stepFreeBodies(float dt) {
  for (const auto* body : bodies)
    if (!body || !body->proto || body->current > 1 || body->attachment ||
        (body->flags & (kBodyChild | kBodyHasChildren | kBodyBit10))) return FreeBodyStepResult::Unsupported;
  const float total = dt + pending;
  pending = total;
  if (total < k::minimumStep) return FreeBodyStepResult::Accumulated;
  pending = pending - total;
  lastStep = total;
  for (Body* b : bodies) accumulateWorldForces(params, *b);
  for (Body* b : bodies)
    if (stepBody(*b, total)) ++stepsDone;
  for (Body* b : bodies)
    if (commit(*b) == CommitResult::WentToSleep) sleepBodyState(*b);
  time = total + time;
  return FreeBodyStepResult::Advanced;
}

// -------------------------------------------------------------- collision
CorePair sphereCoreClosest(const State& a, const State& b) {
  // 004af970 case 0x101: both cores are the vertex (0,0,0) of 004ac370. The
  // stock path copies the local vertices out, transforms each with 004b1460
  // and takes direction = a - b.
  const Vec3 origin{0, 0, 0};
  CorePair r;
  const auto worldA = transformPoint(a.pos, a.rot, origin);
  const auto worldB = transformPoint(b.pos, b.rot, origin);
  r.normal = Vec3{worldA[0] - worldB[0], worldA[1] - worldB[1], worldA[2] - worldB[2]};
  // 004369b0 sums x*x + y*y before adding z*z.
  const float dist = std::sqrt(r.normal[0] * r.normal[0] + r.normal[1] * r.normal[1] +
                               r.normal[2] * r.normal[2]);
  r.distance = dist;
  if (dist > k::zero) {
    const float inv = k::one / dist;
    r.normal[0] = inv * r.normal[0];
    r.normal[1] = inv * r.normal[1];
    r.normal[2] = inv * r.normal[2];
  }
  return r;
}

bool pairDropped(float distance, float radiusA, float radiusB) {
  const float sum = radiusB + radiusA;
  return distance == k::zero || sum + k::skin < distance;
}

}  // namespace physics
