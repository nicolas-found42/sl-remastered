#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// On-disk fields of 00580910. References are the original one-based global
// indices, with zero meaning null; no road routing behavior is implied.
struct RoadSegment { std::array<uint32_t, 14> words{}; uint8_t flag = 0; };
struct RoadLink { uint32_t target = 0; uint8_t flag = 0; };
struct RoadNodeRecord {
  std::vector<RoadLink> links;
  struct Lane { std::array<uint8_t, 12> header{}; RoadSegment segment; };
  std::vector<Lane> lanes;
  std::array<uint32_t, 4> words{};
};
struct RoadPathRecord {
  std::array<uint32_t, 3> refs{};
  std::array<uint32_t, 7> words{};
  std::vector<RoadSegment> segments;
};
struct RoadOtherRecord {
  std::array<uint32_t, 2> refs{};
  uint8_t flag = 0;
  std::array<uint32_t, 12> words{};
  std::array<uint32_t, 4> extraRefs{};
  std::array<uint32_t, 3> tail{};
  std::vector<RoadSegment> segments;
};
struct RoadNetwork {
  uint32_t version = 0;
  std::array<std::vector<RoadNodeRecord>, 2> nodes;
  std::vector<RoadPathRecord> paths;
  std::vector<RoadOtherRecord> others;
};

// Partial 00580cc0 state. Per-record initialization and the traffic manager
// are not represented here. Values above 1,048,575 are refused, including
// high-bit values that stock compares as negative signed integers.
struct RoadInitialState {
  float time = 0;
  std::vector<uint8_t> pathMask;
};
std::optional<RoadInitialState> roadInitialState(const RoadNetwork& network, float time);

// 00580910 builds a one-based table in this group order. Index zero is null.
enum class RoadRecordKind { FirstNode, SecondNode, Path, Other };
struct RoadRecordIndex {
  RoadRecordKind kind;
  size_t index;
};
std::optional<RoadRecordIndex> resolveRoadRecordIndex(const RoadNetwork& network, uint32_t id);
// 00581a00: among the first two groups, choose the node closest to the
// requested distance from the query point. Ties keep the earlier node.
// A height-limited query only considers nodes less than 5 units away in y.
// Non-finite inputs or node coordinates are unsupported and return nullopt.
std::optional<RoadRecordIndex> roadNearestCrossing(const RoadNetwork& network,
    const std::array<float, 3>& position, float distance, bool limitHeight);
// 0057b9b0: maximum distance from a crossing center to its linked endpoints.
// Unsupported links or non-finite coordinates return nullopt.
std::optional<float> roadCrossingRadius(const RoadNetwork& network, RoadRecordIndex crossing);
// Partial 0057f4d0 result for a first-group crossing. Empty lanes and links
// outside roadCrossingRadius's supported subset are refused.
struct RoadCrossingInitialState {
  float radius = 0;
  uint32_t field38 = 0;
  std::vector<std::array<uint32_t, 2>> lanePairs;
  uint32_t lowerBucket = 0;
  uint32_t upperBound = 0;
};
std::optional<RoadCrossingInitialState> roadFirstCrossingInitialState(
    const RoadNetwork& network, size_t index);
// Partial 0057fae0 state. Null groups correspond to links with flag zero.
struct RoadSecondCrossingInitialState {
  // lowerBucket is the 0057f4d0 return value, which 0057fae0 discards.
  RoadCrossingInitialState crossing;
  float time = 0;
  std::vector<std::optional<uint32_t>> linkGroups;
  uint32_t groupCount = 0;
};
std::optional<RoadSecondCrossingInitialState> roadSecondCrossingInitialState(
    const RoadNetwork& network, size_t index, float time);
// Bounded topology portion of 0057b120. Scaling and road-global maximum are
// not modeled. Each index identifies an element of RoadNetwork::others.
struct RoadPathTopology {
  std::vector<size_t> others;
  int64_t adjustedCount = 0;
};
std::optional<RoadPathTopology> roadPathTopology(const RoadNetwork& network, size_t index);
// Bounded 0057b120 pass in path order. Unsupported float extremes and malformed
// topology return null; nonfinite and subnormal other values are refused even
// when unreachable. Uses the stock startup constant 1.75, assuming that setup
// has run. This does not initialize other traffic state.
struct RoadPathInitialState {
  std::vector<float> otherValues;
  std::vector<float> pathValues;
  float maximum = 0;
};
std::optional<RoadPathInitialState> roadPathInitialState(const RoadNetwork& network);
// 00581b70: test the nearest crossing's 0.7-scaled radius. Null means the
// bounded implementation cannot evaluate the supplied data or position.
std::optional<bool> roadInsideCrossing(const RoadNetwork& network,
    const std::array<float, 3>& position, bool limitHeight);
bool readRoadNetwork(const std::vector<uint8_t>& bytes, RoadNetwork& out, std::string& error);
