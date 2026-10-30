#include "road_network.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {
// Stock runs these x87 operations at 24-bit precision with an extended
// exponent. A float intermediate would overflow before the stock operation.
double x87SinglePrecision(double value) {
  if (value == 0.0) return value;
  int exponent = 0;
  const double fraction = std::frexp(value, &exponent);
  return std::ldexp(static_cast<double>(static_cast<float>(fraction)), exponent);
}

struct Reader {
  const std::vector<uint8_t>& bytes;
  size_t pos = 0;
  bool take(void* dest, size_t n) {
    if (n > bytes.size() - pos) return false;
    std::memcpy(dest, bytes.data() + pos, n); pos += n; return true;
  }
  bool u32(uint32_t& value) {
    uint8_t b[4]; if (!take(b, 4)) return false;
    value = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
    return true;
  }
  bool segment(RoadSegment& s) {
    for (auto& word : s.words) if (!u32(word)) return false;
    return take(&s.flag, 1);
  }
  bool count(uint32_t& n, size_t minimum) {
    return u32(n) && n <= (bytes.size() - pos) / minimum;
  }
  bool segments(std::vector<RoadSegment>& out) {
    uint32_t n; if (!count(n, 57)) return false;
    out.resize(n); for (auto& segment : out) if (!this->segment(segment)) return false;
    return true;
  }
};
}

std::optional<RoadInitialState> roadInitialState(const RoadNetwork& network, float time) {
  constexpr uint32_t maxPathId = 1024 * 1024 - 1;
  uint32_t maximum = 0;
  for (const auto& path : network.paths) {
    const uint32_t id = path.words[6];
    if (id > maxPathId) return std::nullopt;
    maximum = std::max(maximum, id);
  }
  RoadInitialState state;
  state.time = time;
  state.pathMask.assign(static_cast<size_t>(maximum) + 1, 1);
  state.pathMask[0] = 0;
  return state;
}

std::optional<RoadRecordIndex> resolveRoadRecordIndex(const RoadNetwork& network, uint32_t id) {
  if (id == 0) return std::nullopt;
  size_t index = static_cast<size_t>(id - 1);
  const size_t sizes[] = {network.nodes[0].size(), network.nodes[1].size(),
                          network.paths.size(), network.others.size()};
  constexpr RoadRecordKind kinds[] = {RoadRecordKind::FirstNode, RoadRecordKind::SecondNode,
                                       RoadRecordKind::Path, RoadRecordKind::Other};
  for (size_t i = 0; i < 4; ++i) {
    if (index < sizes[i]) return RoadRecordIndex{kinds[i], index};
    index -= sizes[i];
  }
  return std::nullopt;
}

std::optional<RoadRecordIndex> roadNearestCrossing(const RoadNetwork& network,
    const std::array<float, 3>& position, float distance, bool limitHeight) {
  if (!std::isfinite(distance)) return std::nullopt;
  for (float value : position) if (!std::isfinite(value)) return std::nullopt;
  std::optional<RoadRecordIndex> best;
  float bestError = 0;
  for (size_t group = 0; group < 2; ++group) {
    for (size_t i = 0; i < network.nodes[group].size(); ++i) {
      const auto& words = network.nodes[group][i].words;
      std::array<float, 3> p{};
      for (size_t j = 0; j < 3; ++j) {
        std::memcpy(&p[j], &words[j], sizeof(float));
        if (!std::isfinite(p[j])) return std::nullopt;
      }
      const double dx = x87SinglePrecision(double(p[0]) - position[0]);
      const double dy = x87SinglePrecision(double(p[1]) - position[1]);
      const double dz = x87SinglePrecision(double(p[2]) - position[2]);
      if (limitHeight && std::fabs(dy) >= 5.0f) continue;
      const double xy = x87SinglePrecision(x87SinglePrecision(dx * dx) + x87SinglePrecision(dy * dy));
      const double square = x87SinglePrecision(xy + x87SinglePrecision(dz * dz));
      const double error = std::fabs(x87SinglePrecision(x87SinglePrecision(std::sqrt(square)) - distance));
      if (!best || error < bestError) {
        best = RoadRecordIndex{group == 0 ? RoadRecordKind::FirstNode : RoadRecordKind::SecondNode, i};
        bestError = static_cast<float>(error);
      }
    }
  }
  return best;
}

std::optional<float> roadCrossingRadius(const RoadNetwork& network, RoadRecordIndex crossing) {
  size_t group;
  if (crossing.kind == RoadRecordKind::FirstNode) group = 0;
  else if (crossing.kind == RoadRecordKind::SecondNode) group = 1;
  else return std::nullopt;
  if (crossing.index >= network.nodes[group].size()) return std::nullopt;
  const auto& node = network.nodes[group][crossing.index];
  float center[3];
  for (size_t i = 0; i < 3; ++i) {
    std::memcpy(&center[i], &node.words[i], sizeof(float));
    if (!std::isfinite(center[i])) return std::nullopt;
  }
  float maximum = 0;
  for (const auto& link : node.links) {
    const auto ref = resolveRoadRecordIndex(network, link.target);
    if (!ref || ref->kind != RoadRecordKind::Other || link.flag > 1) return std::nullopt;
    const auto& endpoint = network.others[ref->index].words;
    double delta[3];
    for (size_t i = 0; i < 3; ++i) {
      float coordinate;
      std::memcpy(&coordinate, &endpoint[i + link.flag * 3], sizeof(float));
      if (!std::isfinite(coordinate)) return std::nullopt;
      delta[i] = x87SinglePrecision(double(coordinate) - center[i]);
    }
    const double xy = x87SinglePrecision(x87SinglePrecision(delta[0] * delta[0]) +
                                          x87SinglePrecision(delta[1] * delta[1]));
    const double square = x87SinglePrecision(xy + x87SinglePrecision(delta[2] * delta[2]));
    const double distance = x87SinglePrecision(std::sqrt(square));
    if (distance > maximum) maximum = static_cast<float>(distance);
  }
  return maximum;
}

static std::optional<RoadCrossingInitialState> crossingInitialState(
    const RoadNetwork& network, RoadRecordIndex crossing) {
  const size_t group = crossing.kind == RoadRecordKind::FirstNode ? 0 : 1;
  if (crossing.index >= network.nodes[group].size()) return std::nullopt;
  const auto& lanes = network.nodes[group][crossing.index].lanes;
  if (lanes.empty()) return std::nullopt;
  const auto radius = roadCrossingRadius(network, crossing);
  if (!radius || !std::isfinite(*radius)) return std::nullopt;
  const auto laneValue = [](const RoadNodeRecord::Lane& lane) {
    return uint32_t(lane.header[2]) | (uint32_t(lane.header[3]) << 8);
  };
  uint32_t minimum = laneValue(lanes.front());
  uint32_t maximum = 0;
  for (const auto& lane : lanes) {
    const uint32_t value = laneValue(lane);
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
  }
  RoadCrossingInitialState state;
  state.radius = *radius;
  state.lanePairs.resize(lanes.size());
  state.lowerBucket = minimum / 10;
  state.upperBound = maximum - maximum % 10;
  if (state.upperBound == minimum - minimum % 10) state.upperBound += 10;
  return state;
}

std::optional<RoadCrossingInitialState> roadFirstCrossingInitialState(
    const RoadNetwork& network, size_t index) {
  return crossingInitialState(network, {RoadRecordKind::FirstNode, index});
}

std::optional<RoadSecondCrossingInitialState> roadSecondCrossingInitialState(
    const RoadNetwork& network, size_t index, float time) {
  auto crossing = crossingInitialState(network, {RoadRecordKind::SecondNode, index});
  if (!crossing) return std::nullopt;
  const auto& links = network.nodes[1][index].links;
  if (links.empty()) return std::nullopt;
  const auto otherFor = [&](const RoadLink& link) -> const RoadOtherRecord* {
    const auto ref = resolveRoadRecordIndex(network, link.target);
    return ref && ref->kind == RoadRecordKind::Other ? &network.others[ref->index] : nullptr;
  };
  const auto* first = otherFor(links.front());
  if (!first) return std::nullopt;
  uint32_t previous = first->refs[0], group = 0;
  RoadSecondCrossingInitialState state;
  state.crossing = std::move(*crossing);
  state.crossing.upperBound = 0;
  state.time = time;
  state.linkGroups.reserve(links.size());
  for (const auto& link : links) {
    const auto* other = otherFor(link);
    if (!other) return std::nullopt;
    if (link.flag == 0) state.linkGroups.emplace_back(std::nullopt);
    else {
      if (other->refs[0] != previous) ++group;
      state.linkGroups.emplace_back(group);
      previous = other->refs[0];
    }
  }
  state.groupCount = group + 1;
  return state;
}

std::optional<RoadPathTopology> roadPathTopology(const RoadNetwork& network, size_t index) {
  if (index >= network.paths.size()) return std::nullopt;
  RoadPathTopology result;
  std::vector<bool> seen(network.others.size(), false);
  uint32_t id = network.paths[index].refs[0];
  while (id != 0) {
    const auto ref = resolveRoadRecordIndex(network, id);
    if (!ref || ref->kind != RoadRecordKind::Other || seen[ref->index]) return std::nullopt;
    seen[ref->index] = true;
    result.others.push_back(ref->index);
    const auto& other = network.others[ref->index];
    ++result.adjustedCount;
    if (other.extraRefs[0]) --result.adjustedCount;
    if (other.extraRefs[1]) --result.adjustedCount;
    id = other.refs[1];
  }
  return result;
}

std::optional<RoadPathInitialState> roadPathInitialState(const RoadNetwork& network) {
  auto supported = [](float value) {
    return std::isfinite(value) && (value == 0 || std::fpclassify(value) == FP_NORMAL);
  };
  auto multiply = [&](float a, float b, float& result) {
    const double exact = static_cast<double>(a) * b;
    if (!std::isfinite(exact) || (exact != 0 && std::fabs(exact) < std::numeric_limits<float>::min()) ||
        std::fabs(exact) > std::numeric_limits<float>::max()) return false;
    result = static_cast<float>(exact);
    return supported(result);
  };
  RoadPathInitialState state;
  state.otherValues.reserve(network.others.size());
  for (const auto& other : network.others) {
    float value;
    std::memcpy(&value, &other.tail[0], sizeof(value));
    if (!supported(value)) return std::nullopt;
    state.otherValues.push_back(value);
  }
  state.pathValues.reserve(network.paths.size());
  for (size_t i = 0; i < network.paths.size(); ++i) {
    const auto topology = roadPathTopology(network, i);
    if (!topology || topology->adjustedCount < std::numeric_limits<int32_t>::min() ||
        topology->adjustedCount > std::numeric_limits<int32_t>::max()) return std::nullopt;
    for (uint8_t flag = 0; flag < 2; ++flag) {
      float factor = 1.0f;
      for (size_t j : topology->others) {
        const auto& other = network.others[j];
        if (other.flag == flag && !other.extraRefs[0] && !other.extraRefs[1] &&
            !multiply(factor, 1.1f, factor)) return std::nullopt;
      }
      for (size_t j : topology->others) {
        const auto& other = network.others[j];
        if (other.flag == flag && !other.extraRefs[0] && !other.extraRefs[1]) {
          if (!multiply(factor, 0x1.d1745cp-1f, factor) ||
              !multiply(factor, state.otherValues[j], state.otherValues[j])) return std::nullopt;
        }
      }
    }
    float pathValue;
    if (!multiply(static_cast<float>(topology->adjustedCount), 1.75f, pathValue)) return std::nullopt;
    state.pathValues.push_back(pathValue);
    if (state.maximum < pathValue) state.maximum = pathValue;
  }
  return state;
}

std::optional<bool> roadInsideCrossing(const RoadNetwork& network,
    const std::array<float, 3>& position, bool limitHeight) {
  for (float value : position) if (!std::isfinite(value)) return std::nullopt;
  // A null nearest result also represents non-finite node data, which must not
  // be mistaken for an empty or height-filtered graph.
  for (const auto& group : network.nodes) for (const auto& node : group)
    for (size_t i = 0; i < 3; ++i) {
      float value; std::memcpy(&value, &node.words[i], sizeof(float));
      if (!std::isfinite(value)) return std::nullopt;
    }
  const auto nearest = roadNearestCrossing(network, position, 0, limitHeight);
  if (!nearest) return false;
  const auto radius = roadCrossingRadius(network, *nearest);
  if (!radius || !std::isfinite(*radius)) return std::nullopt;
  const auto& node = network.nodes[nearest->kind == RoadRecordKind::FirstNode ? 0 : 1][nearest->index];
  double delta[3];
  for (size_t i = 0; i < 3; ++i) {
    float value; std::memcpy(&value, &node.words[i], sizeof(float));
    delta[i] = x87SinglePrecision(double(value) - position[i]);
  }
  const double xy = x87SinglePrecision(x87SinglePrecision(delta[0] * delta[0]) +
                                        x87SinglePrecision(delta[1] * delta[1]));
  const double square = x87SinglePrecision(xy + x87SinglePrecision(delta[2] * delta[2]));
  const double scaled = x87SinglePrecision(double(*radius) * 0.7f);
  return square < x87SinglePrecision(scaled * scaled);
}

bool readRoadNetwork(const std::vector<uint8_t>& bytes, RoadNetwork& out, std::string& error) {
  Reader r{bytes}; RoadNetwork result; std::array<uint32_t, 4> counts{};
  auto fail = [&] { error = "truncated or unsupported road network"; return false; };
  if (!r.u32(result.version)) return fail();
  for (auto& n : counts) if (!r.u32(n)) return fail();
  uint64_t total = 0; for (auto n : counts) total += n;
  if (total > bytes.size() / 4 || total > std::numeric_limits<uint32_t>::max()) return fail();
  auto reference = [&](uint32_t& id) { return r.u32(id) && id <= total; };
  for (size_t group = 0; group < 2; ++group) {
    result.nodes[group].resize(counts[group]);
    for (auto& node : result.nodes[group]) {
      uint32_t n; if (!r.count(n, 5)) return fail(); node.links.resize(n);
      for (auto& link : node.links) if (!reference(link.target) || !r.take(&link.flag, 1)) return fail();
      if (!r.count(n, 69)) return fail(); node.lanes.resize(n);
      for (auto& lane : node.lanes) if (!r.take(lane.header.data(), 12) || !r.segment(lane.segment)) return fail();
      for (auto& word : node.words) if (!r.u32(word)) return fail();
    }
  }
  result.paths.resize(counts[2]);
  for (auto& path : result.paths) {
    for (auto& ref : path.refs) if (!reference(ref)) return fail();
    for (auto& word : path.words) if (!r.u32(word)) return fail();
    if (!r.segments(path.segments) || path.segments.empty()) return fail();
  }
  result.others.resize(counts[3]);
  for (auto& other : result.others) {
    for (auto& ref : other.refs) if (!reference(ref)) return fail();
    if (!r.take(&other.flag, 1)) return fail();
    for (auto& word : other.words) if (!r.u32(word)) return fail();
    for (auto& ref : other.extraRefs) if (!reference(ref)) return fail();
    for (auto& word : other.tail) if (!r.u32(word)) return fail();
    if (!r.segments(other.segments) || other.segments.empty()) return fail();
  }
  if (r.pos != bytes.size()) return fail();
  out = std::move(result); error.clear(); return true;
}
