#include "road_alignment.hpp"

#include <cmath>
#include <cstring>

namespace {

float wordAsFloat(uint32_t word) {
  float value;
  std::memcpy(&value, &word, sizeof(float));
  return value;
}

// Cubic Hermite evaluation of one road segment at t in [0,1].
std::array<float, 3> hermitePoint(const RoadSegment& s, float t) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  const float h00 = 2 * t3 - 3 * t2 + 1;
  const float h10 = t3 - 2 * t2 + t;
  const float h01 = -2 * t3 + 3 * t2;
  const float h11 = t3 - t2;
  std::array<float, 3> point{};
  for (size_t i = 0; i < 3; ++i) {
    const float p0 = wordAsFloat(s.words[i]);
    const float p1 = wordAsFloat(s.words[3 + i]);
    const float m0 = wordAsFloat(s.words[6 + i]);
    const float m1 = wordAsFloat(s.words[9 + i]);
    point[i] = h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
  }
  return point;
}

// Derivative of the Hermite piece (unnormalized tangent).
std::array<float, 3> hermiteTangent(const RoadSegment& s, float t) {
  const float t2 = t * t;
  const float h00 = 6 * t2 - 6 * t;
  const float h10 = 3 * t2 - 4 * t + 1;
  const float h01 = -6 * t2 + 6 * t;
  const float h11 = 3 * t2 - 2 * t;
  std::array<float, 3> tangent{};
  for (size_t i = 0; i < 3; ++i) {
    const float p0 = wordAsFloat(s.words[i]);
    const float p1 = wordAsFloat(s.words[3 + i]);
    const float m0 = wordAsFloat(s.words[6 + i]);
    const float m1 = wordAsFloat(s.words[9 + i]);
    tangent[i] = h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
  }
  return tangent;
}

bool usable(const RoadSegment& s) {
  for (size_t i = 0; i < 12; ++i)
    if (!std::isfinite(wordAsFloat(s.words[i]))) return false;
  return true;
}

// Stock GroundMap_lookupPathNearXZ (0x00581BF0) compares in the XZ plane;
// the y component does not select the nearest road.
double distanceSqXZ(const std::array<float, 3>& a, const std::array<float, 3>& b) {
  const double dx = double(a[0]) - double(b[0]);
  const double dz = double(a[2]) - double(b[2]);
  return dx * dx + dz * dz;
}

// XZ arc length of one segment: the on-disk words[12] length when positive,
// else the chord length in XZ.
double segmentLengthXZ(const RoadSegment& s) {
  const float stored = wordAsFloat(s.words[12]);
  if (std::isfinite(stored) && stored > 0) return stored;
  const double dx = double(wordAsFloat(s.words[3])) - wordAsFloat(s.words[0]);
  const double dz = double(wordAsFloat(s.words[5])) - wordAsFloat(s.words[2]);
  return std::sqrt(dx * dx + dz * dz);
}

// Closest parameter on one segment to the query point in XZ: coarse sample
// plus golden-section refinement on the bracketing third.
float closestParameterXZ(const RoadSegment& s, const std::array<float, 3>& query,
                         double& bestDistance) {
  constexpr int samples = 33;
  int bestIndex = 0;
  double best = distanceSqXZ(hermitePoint(s, 0.0f), query);
  for (int i = 1; i < samples; ++i) {
    const float t = float(i) / float(samples - 1);
    const double d = distanceSqXZ(hermitePoint(s, t), query);
    if (d < best) {
      best = d;
      bestIndex = i;
    }
  }
  float lo = float(bestIndex > 0 ? bestIndex - 1 : 0) / float(samples - 1);
  float hi = float(bestIndex < samples - 1 ? bestIndex + 1 : samples - 1) / float(samples - 1);
  constexpr float phi = 0.6180339887f;
  float c = hi - phi * (hi - lo);
  float d = lo + phi * (hi - lo);
  double dc = distanceSqXZ(hermitePoint(s, c), query);
  double dd = distanceSqXZ(hermitePoint(s, d), query);
  for (int iter = 0; iter < 40 && hi - lo > 1e-6f; ++iter) {
    if (dc < dd) {
      hi = d;
      d = c;
      dd = dc;
      c = hi - phi * (hi - lo);
      dc = distanceSqXZ(hermitePoint(s, c), query);
    } else {
      lo = c;
      c = d;
      dc = dd;
      d = lo + phi * (hi - lo);
      dd = distanceSqXZ(hermitePoint(s, d), query);
    }
  }
  const float t = 0.5f * (lo + hi);
  bestDistance = distanceSqXZ(hermitePoint(s, t), query);
  return t;
}

// Walk every spline group (paths, other records, crossing lanes) with the
// cumulative XZ arc length at each segment start and the group's total XZ
// length, so the caller can apply the stock half-path tangent flip.
template <typename F>
void forEachSegment(const RoadNetwork& network, F&& fn) {
  const auto walk = [&](const std::vector<RoadSegment>& segments) {
    double total = 0;
    for (const auto& segment : segments)
      if (usable(segment)) total += segmentLengthXZ(segment);
    double cumulative = 0;
    for (const auto& segment : segments) {
      if (!usable(segment)) continue;
      fn(segment, cumulative, total);
      cumulative += segmentLengthXZ(segment);
    }
  };
  for (const auto& path : network.paths) walk(path.segments);
  for (const auto& other : network.others) walk(other.segments);
  for (const auto& group : network.nodes)
    for (const auto& node : group) {
      std::vector<RoadSegment> lanes;
      lanes.reserve(node.lanes.size());
      for (const auto& lane : node.lanes) lanes.push_back(lane.segment);
      walk(lanes);
    }
}

} // namespace

std::optional<RoadAlignment> roadAlignToRoad(const RoadNetwork& network,
                                             const std::array<float, 3>& position) {
  for (float value : position)
    if (!std::isfinite(value)) return std::nullopt;
  std::optional<RoadSegment> bestSegment;
  float bestT = 0;
  double bestCumulative = 0;
  double bestTotal = 0;
  double bestDistance = -1;
  forEachSegment(network, [&](const RoadSegment& segment, double cumulative,
                              double total) {
    double d = 0;
    const float t = closestParameterXZ(segment, position, d);
    if (bestDistance < 0 || d < bestDistance) {
      bestDistance = d;
      bestSegment = segment;
      bestT = t;
      bestCumulative = cumulative;
      bestTotal = total;
    }
  });
  if (!bestSegment) return std::nullopt;
  RoadAlignment result;
  result.position = hermitePoint(*bestSegment, bestT);
  const auto tangent = hermiteTangent(*bestSegment, bestT);
  double length = 0;
  for (size_t i = 0; i < 3; ++i) length += double(tangent[i]) * tangent[i];
  length = std::sqrt(length);
  if (!(length > 0) || !std::isfinite(length)) return std::nullopt;
  // Stock RouteSpline_evalTangent (0x0057EB00) negates the tangent when the
  // spline parameter is at or past half of the path's length (path+96 * 0.5).
  const double param = bestCumulative + double(bestT) * segmentLengthXZ(*bestSegment);
  const double sign = param >= bestTotal * 0.5 ? -1.0 : 1.0;
  for (size_t i = 0; i < 3; ++i)
    result.direction[i] = static_cast<float>(sign * tangent[i] / length);
  for (size_t i = 0; i < 3; ++i)
    if (!std::isfinite(result.position[i]) || !std::isfinite(result.direction[i]))
      return std::nullopt;
  return result;
}
