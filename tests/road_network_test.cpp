#include "road_network.hpp"
#include "road_other_initial.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream file(argv[1], std::ios::binary);
  assert(file);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  RoadNetwork network; std::string error;
  assert(readRoadNetwork(bytes, network, error));
  assert(network.version == 1);
  assert(network.nodes[0].size() == 53 && network.nodes[1].size() == 218);
  assert(network.paths.size() == 484 && network.others.size() == 1572);
  const auto freshOther = roadOtherInitialState(network.others[0], 12.5f);
  assert(freshOther && freshOther->time == 12.5f && freshOther->fieldC4 == 0 && freshOther->fieldC8 == 0);
  const auto cityPath = roadPathTopology(network, 0);
  assert(cityPath && (cityPath->others == std::vector<size_t>{0, 1}) && cityPath->adjustedCount == 2);
  const auto cityPaths = roadPathInitialState(network);
  assert(cityPaths && cityPaths->pathValues.size() == 484 && cityPaths->pathValues[0] == 3.5f &&
         cityPaths->pathValues[1] == 1.75f && cityPaths->maximum == 10.5f);
  RoadNetwork pathFixture;
  pathFixture.paths.resize(1);
  pathFixture.others.resize(2);
  auto topology = roadPathTopology(pathFixture, 0);
  assert(topology && topology->others.empty() && topology->adjustedCount == 0);
  pathFixture.paths[0].refs[0] = 2;
  pathFixture.others[0].refs[1] = 3;
  pathFixture.others[0].extraRefs[0] = 1;
  pathFixture.others[1].extraRefs[1] = 1;
  topology = roadPathTopology(pathFixture, 0);
  assert(topology && (topology->others == std::vector<size_t>{0, 1}) && topology->adjustedCount == 0);
  auto pathState = roadPathInitialState(pathFixture);
  assert(pathState && pathState->pathValues[0] == 0 && pathState->maximum == 0);
  pathFixture.others[1].refs[1] = 2;
  assert(!roadPathTopology(pathFixture, 0));
  pathFixture.others[1].refs[1] = 1;
  assert(!roadPathTopology(pathFixture, 0));
  pathFixture.others[1].refs[1] = 99;
  assert(!roadPathTopology(pathFixture, 0));
  assert(!roadPathInitialState(pathFixture));
  assert(!roadPathTopology(pathFixture, 1));
  pathFixture.others[1].refs[1] = 0;
  pathFixture.others[0].extraRefs[0] = 0;
  pathFixture.others[1].extraRefs[1] = 0;
  const float one = 1.0f;
  std::memcpy(&pathFixture.others[0].tail[0], &one, sizeof(one));
  std::memcpy(&pathFixture.others[1].tail[0], &one, sizeof(one));
  pathState = roadPathInitialState(pathFixture);
  assert(pathState && pathState->pathValues[0] == 3.5f && pathState->maximum == 3.5f &&
         pathState->otherValues[0] > pathState->otherValues[1]);
  pathFixture.others[0].tail[0] = 1;
  assert(!roadPathInitialState(pathFixture));
  pathFixture.others[0].tail[0] = 0x3f800000;
  pathFixture.paths.push_back(RoadPathRecord{});
  pathFixture.paths[0].refs[0] = 3;
  pathFixture.others[0].refs[1] = 4;
  const auto once = roadPathInitialState(pathFixture);
  pathFixture.paths[1].refs[0] = 3;
  const auto twice = roadPathInitialState(pathFixture);
  assert(once && twice && twice->pathValues.size() == 2 &&
         twice->otherValues[0] > once->otherValues[0] &&
         twice->pathValues[0] == twice->pathValues[1]);
  RoadNetwork unreachable;
  unreachable.others.resize(1);
  unreachable.others[0].tail[0] = 0x7fc00000;
  assert(!roadPathInitialState(unreachable));
  const auto cityState = roadInitialState(network, 12.5f);
  assert(cityState && cityState->time == 12.5f && cityState->pathMask == std::vector<uint8_t>{0});
  RoadNetwork maskFixture;
  auto mask = roadInitialState(maskFixture, 3.0f);
  assert(mask && mask->time == 3.0f && mask->pathMask == std::vector<uint8_t>{0});
  maskFixture.paths.resize(3);
  maskFixture.paths[1].words[6] = 1;
  maskFixture.paths[2].words[6] = 3;
  mask = roadInitialState(maskFixture, -1.0f);
  assert(mask && mask->time == -1.0f && (mask->pathMask == std::vector<uint8_t>{0, 1, 1, 1}));
  maskFixture.paths[2].words[6] = UINT32_MAX;
  assert(!roadInitialState(maskFixture, 0));
  maskFixture.paths[2].words[6] = 0x80000000u;
  assert(!roadInitialState(maskFixture, 0));
  maskFixture.paths[2].words[6] = 1048575;
  mask = roadInitialState(maskFixture, 0);
  assert(mask && mask->pathMask.size() == 1048576 && mask->pathMask.front() == 0 && mask->pathMask.back() == 1);
  maskFixture.paths[2].words[6] = 1048576;
  assert(!roadInitialState(maskFixture, 0));
  const auto radius0 = roadCrossingRadius(network, {RoadRecordKind::FirstNode, 0});
  const auto radius1 = roadCrossingRadius(network, {RoadRecordKind::FirstNode, 1});
  assert(radius0 && radius1);
  uint32_t radiusBits0, radiusBits1;
  std::memcpy(&radiusBits0, &*radius0, 4); std::memcpy(&radiusBits1, &*radius1, 4);
  assert(radiusBits0 == 0x41084252 && radiusBits1 == 0x410aea7a);
  const auto crossing = roadFirstCrossingInitialState(network, 0);
  assert(crossing && crossing->radius == *radius0 && crossing->field38 == 0 &&
         crossing->lanePairs.size() == 3 && crossing->lowerBucket == 1 && crossing->upperBound == 20);
  for (const auto& pair : crossing->lanePairs) assert((pair == std::array<uint32_t, 2>{0, 0}));
  RoadNetwork crossingFixture;
  crossingFixture.nodes[0].resize(1);
  assert(!roadFirstCrossingInitialState(crossingFixture, 0));
  const auto second = roadSecondCrossingInitialState(network, 0, 7.5f);
  assert(second && second->time == 7.5f && second->crossing.lanePairs.size() == 10 &&
         second->crossing.lowerBucket == 1 && second->crossing.upperBound == 0 && second->groupCount == 3);
  const std::vector<std::optional<uint32_t>> cityGroups{
      std::nullopt, 0, std::nullopt, std::nullopt, 1, 1, std::nullopt, std::nullopt, 2, 2};
  assert(second && second->linkGroups == cityGroups);
  RoadNetwork secondFixture;
  secondFixture.nodes[1].resize(1);
  secondFixture.nodes[1][0].lanes.resize(1);
  assert(!roadSecondCrossingInitialState(secondFixture, 0, 0));
  secondFixture.others.resize(2);
  secondFixture.others[0].refs[0] = 1;
  secondFixture.others[1].refs[0] = 0;
  secondFixture.nodes[1][0].links = {{2, 0}, {3, 1}, {2, 1}, {2, 0}};
  const auto syntheticSecond = roadSecondCrossingInitialState(secondFixture, 0, -2);
  const std::vector<std::optional<uint32_t>> expectedGroups{std::nullopt, 1, 2, std::nullopt};
  assert(syntheticSecond && syntheticSecond->linkGroups == expectedGroups &&
         syntheticSecond->groupCount == 3 && syntheticSecond->time == -2);
  secondFixture.others[1].refs[0] = 1;
  const auto equalSecond = roadSecondCrossingInitialState(secondFixture, 0, 0);
  const std::vector<std::optional<uint32_t>> equalGroups{std::nullopt, 0, 0, std::nullopt};
  assert(equalSecond && equalSecond->linkGroups == equalGroups && equalSecond->groupCount == 1);
  secondFixture.nodes[1][0].links[1].target = 99;
  assert(!roadSecondCrossingInitialState(secondFixture, 0, 0));
  crossingFixture.nodes[0][0].lanes.resize(2);
  crossingFixture.nodes[0][0].lanes[0].header[2] = 3;
  crossingFixture.nodes[0][0].lanes[1].header[2] = 23;
  auto initialCrossing = roadFirstCrossingInitialState(crossingFixture, 0);
  assert(initialCrossing && initialCrossing->radius == 0 && initialCrossing->lowerBucket == 0 && initialCrossing->upperBound == 20);
  crossingFixture.nodes[0][0].lanes[0].header[2] = 0xff;
  crossingFixture.nodes[0][0].lanes[0].header[3] = 0xff;
  crossingFixture.nodes[0][0].lanes[1].header[2] = 0xff;
  crossingFixture.nodes[0][0].lanes[1].header[3] = 0xff;
  initialCrossing = roadFirstCrossingInitialState(crossingFixture, 0);
  assert(initialCrossing && initialCrossing->lowerBucket == 6553 && initialCrossing->upperBound == 65540);
  assert(!roadFirstCrossingInitialState(crossingFixture, 1));
  crossingFixture.nodes[0][0].links.push_back({2, 0});
  crossingFixture.others.resize(1);
  const float extreme = std::numeric_limits<float>::max();
  const float negativeExtreme = -extreme;
  std::memcpy(&crossingFixture.nodes[0][0].words[0], &negativeExtreme, sizeof(float));
  std::memcpy(&crossingFixture.others[0].words[0], &extreme, sizeof(float));
  assert(!roadFirstCrossingInitialState(crossingFixture, 0));
  assert(!roadCrossingRadius(network, {RoadRecordKind::Path, 0}));
  std::array<float, 3> center{};
  for (size_t i = 0; i < 3; ++i) std::memcpy(&center[i], &network.nodes[0][0].words[i], 4);
  auto inside = roadInsideCrossing(network, center, false);
  assert(inside && *inside);
  center[0] += *radius0 * 0.71f;
  auto outside = roadInsideCrossing(network, center, false);
  assert(outside && !*outside);
  assert(!roadInsideCrossing(network, {std::numeric_limits<float>::quiet_NaN(),0,0}, false));
  auto check = [&](uint32_t id, RoadRecordKind kind, size_t index) {
    auto ref = resolveRoadRecordIndex(network, id);
    assert(ref && ref->kind == kind && ref->index == index);
  };
  assert(!resolveRoadRecordIndex(network, 0));
  check(1, RoadRecordKind::FirstNode, 0);
  check(53, RoadRecordKind::FirstNode, 52);
  check(54, RoadRecordKind::SecondNode, 0);
  check(271, RoadRecordKind::SecondNode, 217);
  check(272, RoadRecordKind::Path, 0);
  check(755, RoadRecordKind::Path, 483);
  check(756, RoadRecordKind::Other, 0);
  check(2327, RoadRecordKind::Other, 1571);
  assert(!resolveRoadRecordIndex(network, 2328));
  RoadNetwork sparse;
  sparse.paths.resize(1);
  auto sparseRef = resolveRoadRecordIndex(sparse, 1);
  assert(sparseRef && sparseRef->kind == RoadRecordKind::Path && sparseRef->index == 0);
  assert(!resolveRoadRecordIndex(sparse, 2));
  const struct { std::array<float, 3> pos; float distance; bool height; int index; } queries[] = {
    {{0,0,0},0,false,259}, {{0,0,0},100,false,170}, {{20,3,-400},20,false,169},
    {{0,1000,0},0,true,-1}, {{0,0,0},0,true,259}, {{10,-2,20},-10,true,259}};
  for (const auto& q : queries) {
    const auto found = roadNearestCrossing(network, q.pos, q.distance, q.height);
    if (q.index < 0) { assert(!found); continue; }
    assert(found);
    const size_t flat = found->index + (found->kind == RoadRecordKind::SecondNode ? network.nodes[0].size() : 0);
    assert(flat == static_cast<size_t>(q.index));
  }
  RoadNetwork tie;
  tie.nodes[0].resize(1); tie.nodes[1].resize(1);
  auto emptyRadius = roadCrossingRadius(tie, {RoadRecordKind::FirstNode, 0});
  assert(emptyRadius && *emptyRadius == 0);
  tie.nodes[0][0].links.push_back({99999, 0});
  assert(!roadCrossingRadius(tie, {RoadRecordKind::FirstNode, 0}));
  tie.nodes[0][0].links.clear();
  auto put = [](RoadNodeRecord& n, size_t axis, float value) { std::memcpy(&n.words[axis], &value, sizeof value); };
  put(tie.nodes[0][0], 0, 10); put(tie.nodes[1][0], 0, -10);
  auto first = roadNearestCrossing(tie, {0,0,0}, 10, false);
  assert(first && first->kind == RoadRecordKind::FirstNode);
  put(tie.nodes[0][0], 1, 5); put(tie.nodes[1][0], 1, 5);
  assert(!roadNearestCrossing(tie, {0,0,0}, 0, true));
  tie.nodes[1].clear(); tie.nodes[0].resize(2);
  const float cases[][8] = {{3e20f,0,0,2e20f,0,0,0,0}, {1e20f,1e20f,1e20f,1.5e20f,0,0,0,0},
    {16777216.f,1,1,16777216.f,0,0,0,0}, {1,5,0,2,5,0,0,1},
    {10,0,0,-10,0,0,10,0}, {3.40282346e38f,0,0,2e38f,0,0,0,0}};
  const int expected[] = {1, 1, 0, -1, 0, 1};
  for (size_t i = 0; i < 6; ++i) {
    for (size_t axis = 0; axis < 3; ++axis) {
      put(tie.nodes[0][0], axis, cases[i][axis]); put(tie.nodes[0][1], axis, cases[i][axis + 3]);
    }
    const auto found = roadNearestCrossing(tie, {0,0,0}, cases[i][6], cases[i][7] != 0);
    if (expected[i] < 0) assert(!found);
    else assert(found && found->kind == RoadRecordKind::FirstNode && found->index == size_t(expected[i]));
  }
  assert(network.nodes[0][0].links.size() == 4);
  assert(network.nodes[0][0].links[0].target == 760 && network.nodes[0][0].links[0].flag == 0);
  assert(network.nodes[0][0].links[1].target == 761 && network.nodes[0][0].links[1].flag == 1);
  bytes.pop_back();
  assert(!readRoadNetwork(bytes, network, error));
  assert(network.others.size() == 1572);
}
