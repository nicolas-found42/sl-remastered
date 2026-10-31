#include "physics_contact_geometry.hpp"
#include "physics_core_pair.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
namespace physics {
static ContactGeometryResult convert(DirectContact &c, const CorePair &pair,
                                     const State &a, const State &b,
                                     float radiusA, float radiusB,
                                     Vec3 &worldPoint, bool allowClose) {
  if (!c.a || !c.b || !std::isfinite(pair.distance) ||
      !std::isfinite(radiusA) || !std::isfinite(radiusB) || radiusA < 0 ||
      radiusB < 0)
    return ContactGeometryResult::Unsupported;
  const float radii = radiusB + radiusA;
  if (!std::isfinite(radii))
    return ContactGeometryResult::Unsupported;
  if (!allowClose && (pair.distance == 0 || radii + k::skin < pair.distance))
    return ContactGeometryResult::Dropped;
  // Closer pairs also clear root step flags; overlaps additionally bisect time.
  if (!allowClose && pair.distance < radii + 0.02f)
    return ContactGeometryResult::Unsupported;
  for (unsigned i = 0; i < 3; ++i) {
    if (!std::isfinite(pair.normal[i]) || !std::isfinite(pair.pointA[i]) ||
        !std::isfinite(pair.pointB[i]) || !std::isfinite(a.pos[i]))
      return ContactGeometryResult::Unsupported;
  }
  for (float x : a.rot)
    if (!std::isfinite(x))
      return ContactGeometryResult::Unsupported;
  for (float x : b.rot)
    if (!std::isfinite(x))
      return ContactGeometryResult::Unsupported;
  Vec3 pa = pair.pointA, pb = pair.pointB;
  float separation = pair.distance;
  if (radiusA > 0) {
    separation -= radiusA;
    for (unsigned i = 0; i < 3; ++i)
      pa[i] += (a.rot[i] * pair.normal[0] + a.rot[8 + i] * pair.normal[2] +
                a.rot[4 + i] * pair.normal[1]) *
               -radiusA;
  }
  if (radiusB > 0) {
    separation -= radiusB;
    for (unsigned i = 0; i < 3; ++i)
      pb[i] += (b.rot[i] * pair.normal[0] + b.rot[8 + i] * pair.normal[2] +
                b.rot[4 + i] * pair.normal[1]) *
               radiusB;
  }
  Vec3 point;
  for (unsigned i = 0; i < 3; ++i)
    point[i] = (a.rot[i * 4] * pa[0] + a.rot[i * 4 + 2] * pa[2] +
                a.rot[i * 4 + 1] * pa[1]) +
               a.pos[i];
  c.pointA = pa;
  c.pointB = pb;
  c.normal = pair.normal;
  c.separation = separation;
  worldPoint = point;
  return ContactGeometryResult::Updated;
}
ContactGeometryResult updateDirectContactGeometry(DirectContact &c,
                                                  const CorePair &pair,
                                                  const State &a,
                                                  const State &b, float ra,
                                                  float rb, Vec3 &worldPoint) {
  return convert(c, pair, a, b, ra, rb, worldPoint, false);
}
namespace {
Body *root(Body *b) {
  std::vector<Body *> seen;
  while (b && (b->flags & kBodyChild)) {
    for (Body *p : seen)
      if (p == b)
        return nullptr;
    seen.push_back(b);
    if (!b->attachment)
      return nullptr;
    b = b->attachment->parent;
  }
  return b;
}
bool validRotation(const State &s) {
  if (s.rot != matrixFromQuat(s.quat))
    return false;
  float norm = ((s.quat[2] * s.quat[2] + s.quat[1] * s.quat[1]) +
                s.quat[3] * s.quat[3]) +
               s.quat[0] * s.quat[0];
  return std::isfinite(norm) && std::fabs(norm - 1.0f) < 0.0001f;
}
State sampleInternal(const Body &b, float t) {
  State s;
  if (!(b.flags & kBodyActive))
    return s;
  const State &predicted = b.state[b.current ^ 1],
              &current = b.state[b.current];
  for (unsigned i = 0; i < 3; i++)
    s.pos[i] = (1 - t) * current.pos[i] + t * predicted.pos[i];
  float dot = ((current.quat[2] * predicted.quat[2] +
                current.quat[1] * predicted.quat[1]) +
               current.quat[3] * predicted.quat[3]) +
              current.quat[0] * predicted.quat[0];
  const float sign = dot < 0 ? -1.0f : 1.0f;
  dot = std::fabs(dot);
  float a = 1 - t, z = t;
  if (1 - dot > 0.0001) {
    float product = (1 + dot) * (1 - dot);
    float root = std::sqrt(product);
    float angle = static_cast<float>(
        std::atan2(static_cast<double>(root), static_cast<double>(dot)));
    float divisor = static_cast<float>(std::sin(static_cast<double>(angle)));
    float left = (1 - t) * angle, right = angle * t;
    a = static_cast<float>(std::sin(static_cast<double>(left)) / divisor);
    z = static_cast<float>(std::sin(static_cast<double>(right)) / divisor);
  }
  for (unsigned i = 0; i < 4; i++)
    s.quat[i] = a * current.quat[i] + z * (sign * predicted.quat[i]);
  s.rot = matrixFromQuat(s.quat);
  s.time = t * predicted.time + (1 - t) * current.time;
  return s;
}
} // namespace
State interpolateContactPose(const Body &body, float fraction) {
  return sampleInternal(body, fraction);
}
ContactGeometryResult updateDirectContactGeometryWithSearch(
    DirectContact &c, const CorePair &predictedPair, float ra, float rb,
    Vec3 &worldPoint, ContactGeometryWorld &world,
    const ContactCoreQuery &query) {
  if (!c.a || !c.b || c.a->current > 1 || c.b->current > 1 ||
      !std::isfinite(ra) || !std::isfinite(rb) || ra < 0 || rb < 0 ||
      !std::isfinite(predictedPair.distance))
    return ContactGeometryResult::Unsupported;
  Body *a = root(c.a);
  Body *b = root(c.b);
  if (!a || !b || !a->proto || !b->proto || a->current > 1 || b->current > 1)
    return ContactGeometryResult::Unsupported;
  bool da = !(a->proto->flags & kProtoStatic),
       db = !(b->proto->flags & kProtoStatic);
  if (!da && !db)
    return ContactGeometryResult::Unsupported;
  float radii = rb + ra;
  if (!std::isfinite(radii))
    return ContactGeometryResult::Unsupported;
  if (predictedPair.distance == 0 || radii + k::skin < predictedPair.distance) {
    ++world.queries;
    return ContactGeometryResult::Dropped;
  }
  const State &initialA = c.a->state[c.a->current ^ 1];
  const State &initialB = c.b->state[c.b->current ^ 1];
  if (predictedPair.distance <= -radii && c.separation < radii) {
    DirectContact candidate = c;
    Vec3 unused;
    auto result = convert(candidate, predictedPair, initialA, initialB, ra, rb,
                          unused, true);
    if (result != ContactGeometryResult::Updated)
      return result;
    ++world.queries;
    c = candidate;
    return ContactGeometryResult::Updated;
  }
  State const *poseA = &c.a->state[c.a->current ^ 1];
  State const *poseB = &c.b->state[c.b->current ^ 1];
  State displayA = *poseA;
  CorePair pair = predictedPair;
  uint32_t extraQueries = 0;
  if (pair.distance <= radii) {
    if (!query)
      return ContactGeometryResult::Unsupported;
    for (Body *body : {c.a, c.b})
      if ((body->flags & kBodyActive) &&
          (!validRotation(body->state[body->current]) ||
           !validRotation(body->state[body->current ^ 1])))
        return ContactGeometryResult::Unsupported;
    float t = 0.5f, delta = 0.25f;
    for (int iteration = 0; iteration < 10; iteration++) {
      State sa = sampleInternal(*c.a, t), sb = sampleInternal(*c.b, t);
      auto evaluated = query(sa, sb);
      if (!evaluated || !std::isfinite(evaluated->distance))
        return ContactGeometryResult::Unsupported;
      pair = *evaluated;
      ++extraQueries;
      if (c.a->flags & kBodyActive)
        displayA = sa;
      if (pair.distance > radii) {
        if (pair.distance < radii + 0.02f) {
          Vec3 wa =
              transformPoint(c.a->state[c.a->current ^ 1].pos,
                             c.a->state[c.a->current ^ 1].rot, pair.pointA);
          Vec3 wb =
              transformPoint(c.b->state[c.b->current ^ 1].pos,
                             c.b->state[c.b->current ^ 1].rot, pair.pointB);
          float dot = (wa[0] - wb[0]) * pair.normal[0] +
                      (wa[1] - wb[1]) * pair.normal[1] +
                      (wa[2] - wb[2]) * pair.normal[2];
          if (dot < 0)
            break;
        }
        t += delta;
      } else
        t -= delta;
      delta *= 0.5f;
    }
    if (pair.distance <= radii) {
      poseA = &c.a->state[c.a->current];
      poseB = &c.b->state[c.b->current];
      auto evaluated = query(*poseA, *poseB);
      if (!evaluated)
        return ContactGeometryResult::Unsupported;
      pair = *evaluated;
      ++extraQueries;
    }
  }
  DirectContact candidate = c;
  Vec3 ignored;
  // The search branch can intentionally return an overlapping current pose.
  CorePair adjusted = pair;
  auto result =
      convert(candidate, adjusted, *poseA, *poseB, ra, rb, ignored, true);
  if (result != ContactGeometryResult::Updated)
    return result;
  Vec3 point;
  for (unsigned i = 0; i < 3; i++)
    point[i] = (displayA.rot[i * 4] * candidate.pointA[0] +
                displayA.rot[i * 4 + 2] * candidate.pointA[2] +
                displayA.rot[i * 4 + 1] * candidate.pointA[1]) +
               displayA.pos[i];
  if (predictedPair.distance < radii + 0.02f) {
    if (da)
      a->state[a->current ^ 1].stepFlags &= ~1u;
    if (db)
      b->state[b->current ^ 1].stepFlags &= ~1u;
    world.stepReady = 0;
  }
  world.queries += 1 + extraQueries;
  c = candidate;
  worldPoint = point;
  return ContactGeometryResult::Updated;
}

std::optional<Vec3> groundFaceContactNormal(const Vec3 &coreNormal,
                                            const Vec3 &faceNormal) {
  float dot = 0;
  for (unsigned i = 0; i < 3; ++i)
    dot += coreNormal[i] * faceNormal[i];
  if (dot < 0.7071f && dot > -0.7071f)
    return std::nullopt;
  Vec3 oriented{};
  for (unsigned i = 0; i < 3; ++i)
    oriented[i] = dot < 0 ? -faceNormal[i] : faceNormal[i];
  return oriented;
}

bool createGroundFacePair(Body &body, Body &ground,
                          const std::array<Vec3, 3> &vertices,
                          const Vec3 &normal, GroundFacePair &out,
                          std::string &error) {
  const char *why = nullptr;
  if (!body.proto || !ground.proto)
    why = "missing prototype";
  else if (body.current > 1 || ground.current > 1)
    why = "invalid state index";
  else if (!(ground.proto->flags & kProtoStatic) || ground.attachment)
    why = "ground is not a static root";
  else if (body.flags & 0x48u)
    why = "body flag 0x40/0x08 (wheel or special) set";
  else if (body.flags & 0x20020u)
    why = "body flag 0x20000/0x20 (excluded branch) set";
  else if (!body.proto->shape.core)
    why = "body shape has no collision core";
  else if (!std::isfinite(body.proto->shape.radius) ||
           body.proto->shape.radius < 0)
    why = "invalid body radius";
  else if (!std::isfinite(body.proto->shape.friction) ||
           !std::isfinite(ground.proto->shape.friction) ||
           !std::isfinite(body.proto->shape.bounce) ||
           !std::isfinite(ground.proto->shape.bounce))
    why = "non-finite friction or bounce";
  if (why) {
    error = std::string("unsupported body/ground face pair: ") + why +
            " (body flags 0x" + [&] {
              char b[16];
              std::snprintf(b, sizeof b, "%x", body.flags);
              return std::string(b);
            }() +
            ", shape type " +
            std::to_string(
                body.proto ? static_cast<int>(body.proto->shape.type) : -1) +
            ")";
    return false;
  }
  for (const State &state : ground.state)
    if (state.pos != Vec3{} || state.rot != matrixIdentity()) {
      error = "transformed ground face unsupported";
      return false;
    }
  const auto type = body.proto->shape.type;
  if (type != ShapeType::Sphere && type != ShapeType::CylinderX &&
      type != ShapeType::CylinderY && type != ShapeType::CylinderZ &&
      type != ShapeType::Box && type != ShapeType::Mesh) {
    error = "unsupported body core";
    return false;
  }
  GroundFacePair result;
  result.faceNormal = normal;
  if (!triangleCore(vertices, normal, result.triangle, error))
    return false;
  result.contact.a = &body;
  result.contact.b = &ground;
  result.contact.friction =
      std::min(body.proto->shape.friction, ground.proto->shape.friction);
  result.contact.bounce =
      (body.proto->shape.bounce + ground.proto->shape.bounce) * 0.5f;
  out = std::move(result);
  error.clear();
  return true;
}

ContactGeometryResult updateGroundFacePair(GroundFacePair &pair,
                                           ContactGeometryWorld &world,
                                           Vec3 &worldPoint) {
  Body *body = pair.contact.a;
  Body *ground = pair.contact.b;
  if (!body || !ground || !body->proto || !ground->proto ||
      !body->proto->shape.core || body->current > 1 || ground->current > 1 ||
      !(ground->proto->flags & kProtoStatic) || ground->attachment ||
      (body->flags & (0x48u | 0x20020u))) {
    return ContactGeometryResult::Unsupported;}
  for (const State &state : ground->state)
    if (state.pos != Vec3{} || state.rot != matrixIdentity()) {
      return ContactGeometryResult::Unsupported;}
  const auto &core = *body->proto->shape.core;
  auto bodyFeature = pair.bodyFeature;
  auto triangleFeature = pair.triangleFeature;
  const auto query = [&](const State &a,
                         const State &b) -> std::optional<CorePair> {
    switch (body->proto->shape.type) {
    case ShapeType::Sphere:
      return closestPointPolyhedron(a, pair.triangle, b, triangleFeature, true);
    case ShapeType::CylinderX:
    case ShapeType::CylinderY:
    case ShapeType::CylinderZ:
      return closestSegmentPolyhedron(core, a, pair.triangle, b, bodyFeature,
                                      triangleFeature, true);
    case ShapeType::Box:
    case ShapeType::Mesh:
      return closestPolyhedronCores(core, a, pair.triangle, b, bodyFeature,
                                    triangleFeature);
    default:
      return corePairUnavailable(PairSiteFile::ContactGeometry, __LINE__);
    }
  };
  auto predicted =
      query(body->state[body->current ^ 1], ground->state[ground->current ^ 1]);
  if (!predicted) {
    return ContactGeometryResult::Unsupported;}
  DirectContact contact = pair.contact;
  auto result = updateDirectContactGeometryWithSearch(
      contact, *predicted, body->proto->shape.radius, 0, worldPoint, world,
      query);
  // A body/ground-face contact carries the face normal in stock (0049b830
  // stores the face data on creation); a deviating closest-core normal means
  // the core query matched a non-ground feature and the pair is dropped.
  if (result == ContactGeometryResult::Updated) {
    const auto oriented = groundFaceContactNormal(contact.normal,
                                                  pair.faceNormal);
    if (!oriented)
      return ContactGeometryResult::Dropped;
    contact.normal = *oriented;
  }
  if (result != ContactGeometryResult::Unsupported) {
    pair.bodyFeature = bodyFeature;
    pair.triangleFeature = triangleFeature;
    pair.contact = contact;
  }
  return result;
}

bool createDynamicBodyPair(Body &first, Body &second, DynamicBodyPair &out,
                           std::string &error) {
  const auto valid = [](const Body &body) {
    if (!body.proto || body.current > 1 ||
        (body.flags & (0x20u | 0x40u | 0x400u | 0x800u | 0x10000u | 0x20000u)))
      return false;
    const Shape &shape = body.proto->shape;
    const bool sphere = shape.type == ShapeType::Sphere;
    const bool supported = sphere || shape.type == ShapeType::CylinderX ||
                           shape.type == ShapeType::CylinderY ||
                           shape.type == ShapeType::CylinderZ ||
                           shape.type == ShapeType::Box ||
                           shape.type == ShapeType::Mesh;
    return supported && (sphere || shape.core) && std::isfinite(shape.radius) &&
           shape.radius >= 0 && std::isfinite(shape.friction) &&
           std::isfinite(shape.bounce);
  };
  if (&first == &second || !valid(first) || !valid(second)) {
    error = "dynamic pair: unsupported bodies or cores";
    return false;
  }
  DynamicBodyPair result;
  result.contact.a = &first;
  result.contact.b = &second;
  result.contact.friction =
      std::min(first.proto->shape.friction, second.proto->shape.friction);
  result.contact.bounce =
      (first.proto->shape.bounce + second.proto->shape.bounce) * 0.5f;
  out = std::move(result);
  error.clear();
  return true;
}

ContactGeometryResult updateDynamicBodyPair(DynamicBodyPair &pair,
                                            ContactGeometryWorld &world,
                                            Vec3 &worldPoint) {
  Body *a = pair.contact.a, *b = pair.contact.b;
  if (!a || !b || !a->proto || !b->proto || a->current > 1 || b->current > 1 ||
      ((a->flags | b->flags) &
       (0x20u | 0x40u | 0x400u | 0x800u | 0x10000u | 0x20000u)))
    return ContactGeometryResult::Unsupported;
  const Shape &sa = a->proto->shape, &sb = b->proto->shape;
  const auto kind = [](ShapeType type) {
    if (type == ShapeType::Sphere)
      return 0;
    if (type == ShapeType::CylinderX || type == ShapeType::CylinderY ||
        type == ShapeType::CylinderZ)
      return 1;
    if (type == ShapeType::Box || type == ShapeType::Mesh)
      return 2;
    return -1;
  };
  const int ka = kind(sa.type), kb = kind(sb.type);
  if (ka < 0 || kb < 0 || (ka && !sa.core) || (kb && !sb.core))
    return ContactGeometryResult::Unsupported;
  auto fa = pair.firstFeature, fb = pair.secondFeature;
  const auto query = [&](const State &pa,
                         const State &pb) -> std::optional<CorePair> {
    if (ka == 0 && kb == 0)
      return sphereCoreClosest(pa, pb);
    if (ka == 0 && kb == 2)
      return closestPointPolyhedron(pa, *sb.core, pb, fb, true);
    if (ka == 2 && kb == 0)
      return closestPointPolyhedron(pb, *sa.core, pa, fa, false);
    if (ka == 1 && kb == 1)
      return closestSegmentCores(*sa.core, pa, *sb.core, pb, fa, fb);
    if (ka == 1 && kb == 2)
      return closestSegmentPolyhedron(*sa.core, pa, *sb.core, pb, fa, fb, true);
    if (ka == 2 && kb == 1)
      return closestSegmentPolyhedron(*sb.core, pb, *sa.core, pa, fb, fa,
                                      false);
    if (ka == 2 && kb == 2)
      return closestPolyhedronCores(*sa.core, pa, *sb.core, pb, fa, fb);
    static const CollisionCore point = pointCore();
    return closestPointSegmentCores(ka == 0 ? point : *sa.core, pa,
                                    kb == 0 ? point : *sb.core, pb);
  };
  auto predicted = query(a->state[a->current ^ 1], b->state[b->current ^ 1]);
  if (!predicted)
    return ContactGeometryResult::Unsupported;
  DirectContact contact = pair.contact;
  const auto result = updateDirectContactGeometryWithSearch(
      contact, *predicted, sa.radius, sb.radius, worldPoint, world, query);
  if (result != ContactGeometryResult::Unsupported) {
    pair.firstFeature = fa;
    pair.secondFeature = fb;
    pair.contact = contact;
  }
  return result;
}

} // namespace physics
