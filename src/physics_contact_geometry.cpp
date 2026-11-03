#include "physics_contact_geometry.hpp"
#include "physics_core_pair.hpp"
#include "physics_cached.hpp"
#include "run_log.hpp"
#include <cstdio>
#include <cstdlib>
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
    // 00550270 stores the acos result with FST (not FSTP) at 0055030d:
    // the divisor's FSIN still consumes the unrounded register value. Only
    // the two numerator angles reload the float copy. Rounding before that
    // first sine changes the sampled contact point on rotating bodies.
    const double fullAngle = std::acos(static_cast<double>(dot));
    const float angle = static_cast<float>(fullAngle);
    const float divisor = static_cast<float>(std::sin(fullAngle));
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
    // FUN_004a2d10 (all.c:119902-119909) refreshes a retained pair from
    // the current poses when the search still cannot separate the shapes.
    // The rejection in new-pair creation (0049c8f0) is a different lifecycle.
    if (pair.distance <= radii) {
      const float sampledDistance = pair.distance;
      poseA = &c.a->state[c.a->current];
      poseB = &c.b->state[c.b->current];
      auto evaluated = query(*poseA, *poseB);
      if (!evaluated || !std::isfinite(evaluated->distance))
        return ContactGeometryResult::Unsupported;
      pair = *evaluated;
      // pfStack_1e8 still names the last interpolated pose for publication
      // of the world point (all.c:119954-119975).
      ++extraQueries;
      auto* channel = runlog::traceChannel("contacttrace");
      if (channel && std::getenv("SLRR_CAR_TRACE"))
        channel->stream.store(true, std::memory_order_relaxed);
      char line[384];
      std::snprintf(line, sizeof line,
                    "{\"kind\":\"overlap\",\"body\":\"%p\",\"other\":\"%p\","
                    "\"time\":%.6f,\"predicted\":%.9g,\"sampled\":%.9g,"
                    "\"radii\":%.9g,\"resting\":%d,\"current\":%.9g}",
                    static_cast<void*>(c.a), static_cast<void*>(c.b),
                    initialA.time, predictedPair.distance, sampledDistance,
                    radii, int(c.resting), pair.distance);
      runlog::tracePush(channel, line);
    }
  }
  DirectContact candidate = c;
  Vec3 ignored;
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
    // FUN_004af970 keeps valid cached vertex/face geometry. Rewalking a
    // different closest body feature here would discard a valid state-7 row.
    if (pair.contact.resting && pair.triangleFeature) {
      std::optional<CorePair> fixed;
      if (body->proto->shape.type == ShapeType::Sphere)
        fixed = cachedPointFace(a, pair.triangle, b, *pair.triangleFeature);
      else if (pair.bodyFeature)
        fixed = cachedVertexFace(core, a, *pair.bodyFeature, pair.triangle,
                                 b, *pair.triangleFeature);
      if (fixed) {
        static bool notedFixed = false;
        if (!notedFixed) {
          notedFixed = true;
          runlog::event(runlog::EventKind::Note,
                        "cached vertex/face retained: body=%p vertex=%lld face=%zu distance=%.9g",
                        static_cast<void*>(body), pair.bodyFeature ? static_cast<long long>(*pair.bodyFeature) : -1LL,
                        *pair.triangleFeature, fixed->distance);
        }
        bodyFeature = pair.bodyFeature;
        triangleFeature = pair.triangleFeature;
        return fixed;
      }
    }
    if (pair.contact.resting && pair.triangleFeature) {
      const bool fixedFace = *pair.triangleFeature < pair.triangle.features.size() &&
          pair.triangle.features[*pair.triangleFeature].ref.kind == CoreFeatureKind::Face;
      const bool fixedVertex = body->proto->shape.type == ShapeType::Sphere ||
          (pair.bodyFeature && *pair.bodyFeature < core.features.size() &&
           core.features[*pair.bodyFeature].ref.kind == CoreFeatureKind::Vertex);
      static bool notedRemainingCache = false;
      if ((!fixedFace || !fixedVertex) && !notedRemainingCache) {
        notedRemainingCache = true;
        runlog::event(runlog::EventKind::Note,
                      "retained contact subgap: remaining FUN_004af970 feature pairs use a full rewalk");
      }
    }
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
  if (pair.bodyFeature && pair.triangleFeature &&
      (bodyFeature != pair.bodyFeature || triangleFeature != pair.triangleFeature)) {
    // Retained 004a2d10 rows keep fixed features; identify the first rewalk
    // transition with the geometry and prediction flags that drove it.
    auto* channel = runlog::traceChannel("contacttrace");
    if (channel && std::getenv("SLRR_CAR_TRACE"))
      channel->stream.store(true, std::memory_order_relaxed);
    char line[1024];
    const auto& oldBody = core.features[*pair.bodyFeature].ref;
    const auto& oldFace = pair.triangle.features[*pair.triangleFeature].ref;
    const auto& newBody = core.features[*bodyFeature].ref;
    const auto& newFace = pair.triangle.features[*triangleFeature].ref;
    const auto oldWorld = transformPoint(body->state[body->current ^ 1].pos,
                                         body->state[body->current ^ 1].rot,
                                         pair.contact.pointA);
    std::snprintf(line, sizeof line,
                  "{\"kind\":\"features\",\"body\":\"%p\",\"time\":%.6f,\"resting\":%d,"
                  "\"old\":[%zu,%zu,%d,%d],\"new\":[%zu,%zu,%d,%d],"
                  "\"distance\":%.9g,\"pa\":[%.9g,%.9g,%.9g],"
                  "\"pb\":[%.9g,%.9g,%.9g],\"old_p\":[%.9g,%.9g,%.9g],"
                  "\"previous_sep\":%.9g,\"predicted\":[%u,%u]}",
                  static_cast<void*>(body), body->state[body->current ^ 1].time,
                  int(pair.contact.resting), *pair.bodyFeature, *pair.triangleFeature,
                  int(oldBody.kind), int(oldFace.kind), *bodyFeature, *triangleFeature,
                  int(newBody.kind), int(newFace.kind), predicted->distance,
                  predicted->pointA[0], predicted->pointA[1], predicted->pointA[2],
                  predicted->pointB[0], predicted->pointB[1], predicted->pointB[2],
                  oldWorld[0], oldWorld[1], oldWorld[2], pair.contact.separation,
                  root(body)->state[root(body)->current ^ 1].stepFlags,
                  root(ground)->state[root(ground)->current ^ 1].stepFlags);
    runlog::tracePush(channel, line);
  }
  DirectContact contact = pair.contact;
  auto result = updateDirectContactGeometryWithSearch(
      contact, *predicted, body->proto->shape.radius, 0, worldPoint, world,
      query);
  // A body/ground-face contact carries the face normal in stock (0049b830
  // stores the face data on creation); a deviating closest-core normal means
  // the core query matched a non-ground feature and the pair is dropped.
  if (result == ContactGeometryResult::Updated) {
    // FUN_004a2d10 uses fixed cached features through 004af970. Once a
    // feature leaves its Voronoi region the old row is destroyed; the full
    // walk above supplies a fresh pending contact, not a migrated state 7.
    if (contact.resting && (bodyFeature != pair.bodyFeature ||
                            triangleFeature != pair.triangleFeature)) {
      contact.resting = false;
      contact.normalImpulse = contact.normalForce = contact.frictionForce = 0;
    }
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
        (body.flags & (0x20u | 0x400u | 0x10000u | 0x20000u)))
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
       (0x20u | 0x400u | 0x10000u | 0x20000u)))
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
  // FUN_0049c8f0: swept flag 0x800 replaces the original core with the
  // displacement segment in the predicted local frame, retaining its radius.
  CollisionCore sweptA, sweptB;
  const auto sweep = [](const Body& body) {
    const State& pose = body.state[body.current ^ 1];
    const Vec3& start = body.state[body.current].pos;
    Vec3 local{};
    for (unsigned i = 0; i < 3; ++i)
      for (unsigned j = 0; j < 3; ++j)
        local[i] += pose.rot[j * 4 + i] * (start[j] - pose.pos[j]);
    return segmentCore({}, local);
  };
  const CollisionCore* ca = sa.core.get();
  const CollisionCore* cb = sb.core.get();
  if (a->flags & 0x800u) {
    sweptA = sweep(*a);
    ca = &sweptA;
  }
  if (b->flags & 0x800u) {
    sweptB = sweep(*b);
    cb = &sweptB;
  }
  const int ka = (a->flags & 0x800u) ? 1 : kind(sa.type);
  const int kb = (b->flags & 0x800u) ? 1 : kind(sb.type);
  if (ka < 0 || kb < 0 || (ka && !ca) || (kb && !cb))
    return ContactGeometryResult::Unsupported;
  // Swept geometry changes each prediction, so its feature cache must restart.
  auto fa = (a->flags & 0x800u) ? std::optional<std::size_t>{} : pair.firstFeature;
  auto fb = (b->flags & 0x800u) ? std::optional<std::size_t>{} : pair.secondFeature;
  const auto query = [&](const State& pa,
                         const State& pb) -> std::optional<CorePair> {
    // Ordinary retained 004af970 rows preserve valid vertex/face pairs.
    // Wheel and swept replacement-core dispatch retain their own paths.
    if (pair.contact.resting && !((a->flags | b->flags) & (0x40u | 0x800u))) {
      std::optional<CorePair> fixed;
      if (ka == 0 && kb == 2 && pair.secondFeature)
        fixed = cachedPointFace(pa, *cb, pb, *pair.secondFeature);
      else if (ka == 2 && kb == 0 && pair.firstFeature)
        fixed = cachedPointFace(pb, *ca, pa, *pair.firstFeature, false);
      else if (ka != 0 && kb == 2 && pair.firstFeature && pair.secondFeature)
        fixed = cachedVertexFace(*ca, pa, *pair.firstFeature, *cb, pb, *pair.secondFeature);
      if (!fixed && ka == 2 && kb != 0 && pair.firstFeature && pair.secondFeature)
        fixed = cachedVertexFace(*cb, pb, *pair.secondFeature, *ca, pa, *pair.firstFeature, false);
      if (fixed) {
        fa = pair.firstFeature;
        fb = pair.secondFeature;
        return fixed;
      }
    }
    if (ka == 0 && kb == 0)
      return sphereCoreClosest(pa, pb);
    if (ka == 0 && kb == 2)
      return closestPointPolyhedron(pa, *cb, pb, fb, true);
    if (ka == 2 && kb == 0)
      return closestPointPolyhedron(pb, *ca, pa, fa, false);
    if (ka == 1 && kb == 1)
      return closestSegmentCores(*ca, pa, *cb, pb, fa, fb);
    if (ka == 1 && kb == 2)
      return closestSegmentPolyhedron(*ca, pa, *cb, pb, fa, fb, true);
    if (ka == 2 && kb == 1)
      return closestSegmentPolyhedron(*cb, pb, *ca, pa, fb, fa, false);
    if (ka == 2 && kb == 2)
      return closestPolyhedronCores(*ca, pa, *cb, pb, fa, fb);
    static const CollisionCore point = pointCore();
    return closestPointSegmentCores(ka == 0 ? point : *ca, pa,
                                    kb == 0 ? point : *cb, pb);
  };
  auto predicted = query(a->state[a->current ^ 1], b->state[b->current ^ 1]);
  if (!predicted)
    return ContactGeometryResult::Unsupported;
  // The point/segment helper has no feature-cache output; recover its exact
  // selected segment feature for the wheel-specific conversion below.
  const auto pointFeature = [](const CollisionCore& core, const Vec3& point) {
    if (point == core.vertices[0].position)
      return std::size_t{0};
    if (point == core.vertices[1].position)
      return std::size_t{1};
    return std::size_t{2};
  };
  if (ka == 1 && kb == 0)
    fa = pointFeature(*ca, predicted->pointA);
  if (ka == 0 && kb == 1)
    fb = pointFeature(*cb, predicted->pointB);
  DirectContact contact = pair.contact;
  const auto renewFeatures = [&] {
    // FUN_004a2d10 retains fixed 004af970 features. A rewalk to another
    // feature replaces the row; its state-7 forces do not migrate with it.
    if (contact.resting && (fa != pair.firstFeature || fb != pair.secondFeature)) {
      contact.resting = false;
      contact.normalImpulse = contact.normalForce = contact.frictionForce = 0;
    }
  };
  // FUN_0049c540: wheel pairs bypass the generic ten-step time search.
  // The wheel core point is retained; only the other body's rounding radius
  // is removed from separation and from its local contact point.
  if ((a->flags | b->flags) & 0x40u) {
    ++world.queries;
    if ((a->flags & b->flags & 0x40u) != 0)
      return ContactGeometryResult::Dropped;
    const bool wheelFirst = (a->flags & 0x40u) != 0;
    const Body& wheel = wheelFirst ? *a : *b;
    const CollisionCore* core = wheelFirst ? ca : cb;
    const int wheelKind = wheelFirst ? ka : kb;
    const auto feature = wheelFirst ? fa : fb;
    const auto otherFeature = wheelFirst ? fb : fa;
    const CollisionCore* otherCore = wheelFirst ? cb : ca;
    const State& wheelPose = wheel.state[wheel.current ^ 1];
    const float wheelRadius = wheelFirst ? sa.radius : sb.radius;
    const float otherRadius = wheelFirst ? sb.radius : sa.radius;
    const float radii = wheelRadius + otherRadius;
    if (!(predicted->distance < radii))
      return ContactGeometryResult::Dropped;
    CorePair adjusted = *predicted;
    Vec3 normal = adjusted.normal;
    if (!wheelFirst)
      for (float& value : normal)
        value = -value;
    if (wheelKind == 1 && core && feature && *feature < core->features.size()) {
      const auto ref = core->features[*feature].ref;
      const Vec3 direction = core->edges[0].direction;
      Vec3 axis{};
      for (unsigned i = 0; i < 3; ++i)
        axis[i] = wheelPose.rot[i * 4] * direction[0] +
                  wheelPose.rot[i * 4 + 1] * direction[1] +
                  wheelPose.rot[i * 4 + 2] * direction[2];
      const float dot = axis[0] * normal[0] + axis[1] * normal[1] +
                        axis[2] * normal[2];
      const bool face = otherCore && otherFeature &&
                        *otherFeature < otherCore->features.size() &&
                        otherCore->features[*otherFeature].ref.kind == CoreFeatureKind::Face;
      if ((ref.kind == CoreFeatureKind::Edge && face && dot < 0) ||
          (ref.kind == CoreFeatureKind::Vertex && ref.index != 0 && dot < 0)) {
        // FUN_004b02e0: test the segment against the other convex faces,
        // excluding the already selected face. Keep the first contained
        // plane crossing in creation order, not a bounding-sphere fallback.
        const Body& otherBody = wheelFirst ? *b : *a;
        const State& otherPose = otherBody.state[otherBody.current ^ 1];
        bool crossed = false;
        if (otherCore) {
          const auto toOther = [&](const Vec3& local) {
            const Vec3 world = transformPoint(wheelPose.pos, wheelPose.rot, local);
            Vec3 result{};
            for (unsigned i = 0; i < 3; ++i)
              for (unsigned j = 0; j < 3; ++j)
                result[i] += otherPose.rot[j * 4 + i] *
                             (world[j] - otherPose.pos[j]);
            return result;
          };
          const Vec3 start = toOther(core->vertices[0].position);
          const Vec3 end = toOther(core->vertices[1].position);
          for (std::size_t fi = 0; fi < otherCore->faces.size(); ++fi) {
            if (face && otherCore->features[*otherFeature].ref.index == fi)
              continue;
            const auto& plane = otherCore->faces[fi];
            const Vec3& anchor = otherCore->vertices[plane.vertices[0]].position;
            float d0 = 0, d1 = 0;
            for (unsigned i = 0; i < 3; ++i) {
              d0 += (start[i] - anchor[i]) * plane.normal[i];
              d1 += (end[i] - anchor[i]) * plane.normal[i];
            }
            if (!(d0 * d1 < 0))
              continue;
            const float fraction = std::fabs(d0 / (d0 - d1));
            Vec3 hit{};
            for (unsigned i = 0; i < 3; ++i)
              hit[i] = start[i] + (end[i] - start[i]) * fraction;
            bool inside = true;
            for (const auto& boundary : otherCore->faces) {
              const Vec3& origin = otherCore->vertices[boundary.vertices[0]].position;
              float distance = 0;
              for (unsigned i = 0; i < 3; ++i)
                distance += (hit[i] - origin[i]) * boundary.normal[i];
              if (distance > 0.00001f) {
                inside = false;
                break;
              }
            }
            if (!inside)
              continue;
            Vec3& wheelPoint = wheelFirst ? adjusted.pointA : adjusted.pointB;
            Vec3& otherPoint = wheelFirst ? adjusted.pointB : adjusted.pointA;
            for (unsigned i = 0; i < 3; ++i) {
              wheelPoint[i] = core->vertices[0].position[i] +
                             (core->vertices[1].position[i] -
                              core->vertices[0].position[i]) * fraction;
              normal[i] = otherPose.rot[i * 4] * plane.normal[0] +
                          otherPose.rot[i * 4 + 1] * plane.normal[1] +
                          otherPose.rot[i * 4 + 2] * plane.normal[2];
            }
            otherPoint = hit;
            if (ref.kind == CoreFeatureKind::Edge && face)
              adjusted.distance = 0;
            crossed = true;
            break;
          }
        }
        if (!crossed)
          return ContactGeometryResult::Dropped;
      }
      if (ref.kind == CoreFeatureKind::Edge && !face && otherRadius > 0) {
        const float distance = adjusted.distance;
        const float along = std::sqrt(std::max(0.0f, radii * radii - distance * distance));
        Vec3& wheelPoint = wheelFirst ? adjusted.pointA : adjusted.pointB;
        for (unsigned i = 0; i < 3; ++i) {
          normal[i] = (axis[i] * along + normal[i] * distance) / radii;
          wheelPoint[i] += direction[i] * along;
        }
      }
    }
    adjusted.normal = normal;
    if (!wheelFirst)
      for (float& value : adjusted.normal)
        value = -value;
    const auto result = convert(contact, adjusted,
                                a->state[a->current ^ 1], b->state[b->current ^ 1],
                                wheelFirst ? 0 : otherRadius,
                                wheelFirst ? otherRadius : 0, worldPoint, true);
    if (result == ContactGeometryResult::Updated) {
      renewFeatures();
      pair.contact = contact;
      pair.firstFeature = fa;
      pair.secondFeature = fb;
    }
    return result;
  }
  const auto result = updateDirectContactGeometryWithSearch(
      contact, *predicted, sa.radius, sb.radius, worldPoint, world, query);
  if (result == ContactGeometryResult::Updated && pair.contact.resting &&
      (fa != pair.firstFeature || fb != pair.secondFeature)) {
    auto* channel = runlog::traceChannel("contacttrace");
    if (channel && std::getenv("SLRR_CAR_TRACE"))
      channel->stream.store(true, std::memory_order_relaxed);
    char line[512];
    const auto index = [](const std::optional<size_t>& value) {
      return value ? static_cast<int>(*value) : -1;
    };
    std::snprintf(line, sizeof line,
                  "{\"kind\":\"dynamic_features\",\"a\":\"%p\",\"b\":\"%p\","
                  "\"time\":%.6f,\"old\":[%d,%d],\"new\":[%d,%d],"
                  "\"sep\":%.9g,\"normal_force\":%.9g,\"impulse\":%.9g}",
                  static_cast<void*>(a), static_cast<void*>(b),
                  a->state[a->current ^ 1].time,
                  index(pair.firstFeature), index(pair.secondFeature), index(fa), index(fb),
                  contact.separation, pair.contact.normalForce, pair.contact.normalImpulse);
    runlog::tracePush(channel, line);

  }
  if (result != ContactGeometryResult::Unsupported) {
    if (result == ContactGeometryResult::Updated)
      renewFeatures();
    pair.firstFeature = fa;
    pair.secondFeature = fb;
    pair.contact = contact;
  }
  return result;
}

} // namespace physics
