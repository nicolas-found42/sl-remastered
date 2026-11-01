#include "physics_core_pair.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
using namespace physics;
static uint32_t bits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, sizeof result);
  return result;
}
int main() {
  const auto p = pointCore();
  const auto s = segmentCore({-2,0,0}, {2,0,0});
  State a, b;
  a.pos = {1,3,0};
  auto r = closestPointSegmentCores(p,a,s,b);
  assert(r && std::fabs(r->distance - 3) < 1e-6f && r->pointB[0] == 1 && r->normal[1] == 1);
  a.pos = {5,3,0};
  r = closestPointSegmentCores(p,a,s,b);
  assert(r && r->pointB[0] == 2 && std::fabs(r->distance - std::sqrt(18.f)) < 1e-6f);
  auto reversed = closestPointSegmentCores(s,b,p,a);
  assert(reversed && reversed->pointA == r->pointB && reversed->normal[0] == -r->normal[0]);
  a.pos = {0,0,0};
  r = closestPointSegmentCores(p,a,s,b);
  assert(r && r->distance == 0 && r->normal == Vec3({0,0,0}));
  State measured, origin;
  measured.pos = {0.13f + 0.125f, -1.21f, 2.78f};
  auto stockPointPair = closestPointSegmentCores(p,measured,p,origin);
  assert(stockPointPair && bits(stockPointPair->distance) == 0x4042ba41u);
  assert(bits(stockPointPair->normal[0]) == 0x3daba444u);
  assert(bits(stockPointPair->normal[1]) == 0xbecb9d38u);
  assert(bits(stockPointPair->normal[2]) == 0x3f69e761u);
  assert(!closestPointSegmentCores(s,a,s,b));
  assert(!closestPointSegmentCores(p,a,boxCore({1,1,1}),b));
  const auto box = boxCore({1,2,3});
  std::optional<std::size_t> cache;
  a.pos = {3,3,0};
  auto w = closestPointPolyhedron(a,box,b,cache);
  assert(w && cache && w->pointB == Vec3({1,2,0}));
  assert(std::fabs(w->distance - std::sqrt(5.f)) < 1e-6f);
  auto previous = *cache;
  a.pos = {0,0,0};
  w = closestPointPolyhedron(a,box,b,cache);
  assert(w && w->distance == -1 && w->pointB == Vec3({1,0,0}));
  assert(*cache != previous);
  cache = box.features.size();
  assert(!closestPointPolyhedron(a,box,b,cache) && *cache == box.features.size());
  assert(!closestPointPolyhedron(a,p,b,cache));
  std::optional<std::size_t> ca, cb;
  State c, d;
  d.pos = {0, 0, 1};
  d.rot[0] = 0;
  d.rot[1] = -1;
  d.rot[4] = 1;
  d.rot[5] = 0;
  auto ss = closestSegmentCores(s,c,s,d,ca,cb);
  assert(ss && ss->distance == 1 && *ca == 2 && *cb == 2);
  auto shortSegment = segmentCore({-0.5f, 0, 0}, {0.5f, 0, 0});
  c.pos = {0, 0, 5};
  d.pos = {0, 0, 0};
  ca.reset();
  cb.reset();
  auto sp = closestSegmentPolyhedron(shortSegment, c, box, d, ca, cb);
  assert(sp && sp->distance == 2 && ca && cb);
  ca = shortSegment.features.size();
  cb.reset();
  assert(!closestSegmentPolyhedron(shortSegment,c,box,d,ca,cb));
  assert(*ca == shortSegment.features.size() && !cb);
  State e, f;
  f.pos = {1, 2, 0};
  ca = 2;
  cb = 2;
  auto parallel = closestSegmentCores(s, e, s, f, ca, cb);
  assert(parallel && std::fabs(parallel->distance - std::sqrt(13.f)) < 1e-6f);
  e.pos = {1, 0, 0};
  ca = 2;
  cb = 24;
  f.pos = {0, 0, 0};
  ca = 2;
  cb = 24;
  auto crossing = closestSegmentPolyhedron(s, e, box, f, ca, cb);
  assert(crossing && crossing->distance == -2 && crossing->pointB == Vec3({1,0,0}));
  State g, h;
  h.pos = {4, 5, 7};
  ca.reset();
  cb.reset();
  auto polyPair = closestPolyhedronCores(box, g, box, h, ca, cb);
  assert(polyPair && polyPair->distance > 0 && ca && cb);
  auto savedA = *ca, savedB = *cb;
  polyPair = closestPolyhedronCores(box, g, box, h, ca, cb);
  assert(polyPair && *ca == savedA && *cb == savedB);
  h.pos = {0, 0, 0};
  ca.reset();
  cb.reset();
  polyPair = closestPolyhedronCores(box, g, box, h, ca, cb);
  assert(polyPair && polyPair->distance <= 0);
  // 004ae9c0 loop cap: when the feature walk does not converge within
  // features+2*features iterations, stock logs "Voronoi error (infinite
  // loop)" and returns distance 1e6 (005f14dc), which callers treat as "too
  // far". Two arbitrarily rotated/separated boxes hit the cap; the port
  // counts it in the diagnostics delta as voronoi_loop_cap.
  State capA, capB;
  capA.pos = {1.85859036f, 1.35967708f, 0.894741297f};
  capB.pos = {-1.79804814f, 0.569901228f, 1.22493935f};
  capB.rot = {0.449589521f, 0.890719771f, -0.0669896826f,
              -0.171649873f, -0.0598761812f, 0.983336747f,
              0.871866405f, -0.430599123f, 0.125972226f, 0, 0, 0};
  const auto capCoreA = boxCore({2.00359249f, 1.91857362f, 2.44845653f});
  const auto capCoreB = boxCore({2.73298216f, 2.62931037f, 1.71355224f});
  corePairDiagnosticsDelta(); // clear the reported baseline
  ca.reset();
  cb.reset();
  const auto capped =
      closestPolyhedronCores(capCoreA, capA, capCoreB, capB, ca, cb);
  assert(capped && capped->distance == 1000000.0f && ca && cb);
  assert(corePairDiagnosticsDelta().find("voronoi_loop_cap") !=
         std::string::npos);
  return 0;
}
