#include "road_route.hpp"

#include "road_alignment.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <queue>
#include <vector>

namespace {

// Flat index over every road record in the stock 00580910 group order.
size_t flatIndex(const RoadNetwork& network, RoadRecordIndex ref) {
  switch (ref.kind) {
  case RoadRecordKind::FirstNode:
    return ref.index;
  case RoadRecordKind::SecondNode:
    return network.nodes[0].size() + ref.index;
  case RoadRecordKind::Path:
    return network.nodes[0].size() + network.nodes[1].size() + ref.index;
  case RoadRecordKind::Other:
    return network.nodes[0].size() + network.nodes[1].size() +
           network.paths.size() + ref.index;
  }
  return 0;
}

size_t recordCount(const RoadNetwork& network) {
  return network.nodes[0].size() + network.nodes[1].size() +
         network.paths.size() + network.others.size();
}

// Adjacency over the whole road graph: node links, path refs and other refs
// all name one-based global record ids. Edge payloads record which "other"
// record the edge traverses, when it does.
std::vector<std::vector<std::pair<size_t, size_t>>> buildAdjacency(
    const RoadNetwork& network) {
  const size_t total = recordCount(network);
  std::vector<std::vector<std::pair<size_t, size_t>>> adjacency(total);
  auto addEdge = [&](size_t from, uint32_t target, size_t other) {
    const auto ref = resolveRoadRecordIndex(network, target);
    if (!ref) return;
    const size_t to = flatIndex(network, *ref);
    if (to < total) adjacency[from].push_back({to, other});
  };
  for (size_t group = 0; group < 2; ++group)
    for (size_t i = 0; i < network.nodes[group].size(); ++i) {
      const size_t from = group == 0 ? i : network.nodes[0].size() + i;
      for (const auto& link : network.nodes[group][i].links) {
        const auto ref = resolveRoadRecordIndex(network, link.target);
        addEdge(from, link.target,
                ref && ref->kind == RoadRecordKind::Other ? ref->index
                                                          : std::numeric_limits<size_t>::max());
      }
    }
  for (size_t i = 0; i < network.paths.size(); ++i) {
    const size_t from =
        network.nodes[0].size() + network.nodes[1].size() + i;
    for (const uint32_t ref : network.paths[i].refs)
      addEdge(from, ref, std::numeric_limits<size_t>::max());
  }
  for (size_t i = 0; i < network.others.size(); ++i) {
    const size_t from = network.nodes[0].size() + network.nodes[1].size() +
                        network.paths.size() + i;
    for (const uint32_t ref : network.others[i].refs) addEdge(from, ref, i);
  }
  return adjacency;
}

std::optional<size_t> nearestCrossingFlat(const RoadNetwork& network,
                                          const std::array<float, 3>& position) {
  const auto ref = roadNearestCrossing(network, position, 0, true); // stock filterY=1
  if (!ref || (ref->kind != RoadRecordKind::FirstNode &&
               ref->kind != RoadRecordKind::SecondNode))
    return std::nullopt;
  return flatIndex(network, *ref);
}

// UNCORROBORATED APPROXIMATION — not stock-matched: one shortest path (by
// record count) from the crossing nearest start to the crossing nearest
// finish; returns the "other" records on it. Stock FUN_005826f0's helpers
// are unread and no parity probe covers routing.
std::optional<std::vector<size_t>> routeOthers(const RoadNetwork& network,
                                               const std::array<float, 3>& start,
                                               const std::array<float, 3>& finish) {
  const auto source = nearestCrossingFlat(network, start);
  const auto target = nearestCrossingFlat(network, finish);
  if (!source || !target) return std::nullopt;
  const auto adjacency = buildAdjacency(network);
  const size_t total = recordCount(network);
  if (*source >= total || *target >= total) return std::nullopt;
  const size_t none = std::numeric_limits<size_t>::max();
  std::vector<size_t> previousEdgeOther(total, none);
  std::vector<size_t> previousNode(total, none);
  std::vector<bool> visited(total, false);
  std::queue<size_t> queue;
  queue.push(*source);
  visited[*source] = true;
  while (!queue.empty()) {
    const size_t node = queue.front();
    queue.pop();
    if (node == *target) {
      std::vector<size_t> path;
      size_t cursor = *target;
      while (cursor != *source) {
        const size_t other = previousEdgeOther[cursor];
        if (other != none) path.push_back(other);
        cursor = previousNode[cursor];
        if (cursor == none) return std::nullopt;
      }
      return path;
    }
    for (const auto& edge : adjacency[node]) {
      if (edge.first >= total || visited[edge.first]) continue;
      visited[edge.first] = true;
      previousEdgeOther[edge.first] = edge.second;
      previousNode[edge.first] = node;
      queue.push(edge.first);
    }
  }
  return std::nullopt;
}

} // namespace

std::optional<std::array<float, 3>> roadStartDirection(const RoadNetwork& network,
                                                       const std::array<float, 3>& start, const std::array<float, 3>& finish) {
  for (float value : start)
    if (!std::isfinite(value)) return std::nullopt;
  for (float value : finish)
    if (!std::isfinite(value)) return std::nullopt;
  const auto aligned = roadAlignToRoad(network, start);
  if (!aligned) return std::nullopt;
  double toward[3] = {double(finish[0]) - start[0], double(finish[1]) - start[1],
                      double(finish[2]) - start[2]};
  const double towardLength =
      std::sqrt(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
  if (!(towardLength > 0) || !std::isfinite(towardLength)) return std::nullopt;
  double dot = 0;
  for (size_t i = 0; i < 3; ++i) dot += double(aligned->direction[i]) * toward[i];
  // FUN_00483400 returns the negated road direction and null on a zero
  // vector; FUN_00583840 itself is unread, so we orient the aligned spline
  // tangent toward the finish before negating.
  const double sign = dot >= 0 ? 1.0 : -1.0;
  std::array<float, 3> direction{};
  for (size_t i = 0; i < 3; ++i)
    direction[i] = static_cast<float>(-sign * aligned->direction[i]);
  for (float value : direction)
    if (!std::isfinite(value)) return std::nullopt;
  return direction;
}

std::optional<std::vector<uint8_t>> roadHaltTrafficPath(const RoadNetwork& network,
                                                        const std::array<float, 3>& start, const std::array<float, 3>& finish) {
  const auto route = routeOthers(network, start, finish);
  if (!route) return std::nullopt;
  std::vector<uint8_t> flags(network.others.size(), 0);
  for (const size_t other : *route) {
    if (other >= flags.size()) return std::nullopt;
    flags[other] = 1;
  }
  return flags;
}
