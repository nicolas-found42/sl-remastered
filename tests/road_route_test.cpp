#include "road_route.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
void setFloat(uint32_t& word, float value) { std::memcpy(&word, &value, sizeof(float)); }

RoadSegment straightSegment(float x0, float z0, float x1, float z1) {
  RoadSegment s;
  setFloat(s.words[0], x0);
  setFloat(s.words[1], 0);
  setFloat(s.words[2], z0);
  setFloat(s.words[3], x1);
  setFloat(s.words[4], 0);
  setFloat(s.words[5], z1);
  const float dx = x1 - x0, dz = z1 - z0;
  setFloat(s.words[6], dx);
  setFloat(s.words[7], 0);
  setFloat(s.words[8], dz);
  setFloat(s.words[9], dx);
  setFloat(s.words[10], 0);
  setFloat(s.words[11], dz);
  const float length = std::sqrt(dx * dx + dz * dz);
  setFloat(s.words[12], length);
  setFloat(s.words[13], length);
  s.flag = 1;
  return s;
}
} // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream file(argv[1], std::ios::binary);
  assert(file);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  RoadNetwork city;
  std::string error;
  assert(readRoadNetwork(bytes, city, error));

  // Fixture: two crossings (one per group) joined by one "other" record.
  // Global one-based ids: nodes[0][0] = 1, nodes[1][0] = 2, others[0] = 3.
  RoadNetwork fixture;
  fixture.nodes[0].resize(1);
  fixture.nodes[1].resize(1);
  setFloat(fixture.nodes[0][0].words[0], 0);
  setFloat(fixture.nodes[0][0].words[1], 0);
  setFloat(fixture.nodes[0][0].words[2], 0);
  setFloat(fixture.nodes[1][0].words[0], 100);
  setFloat(fixture.nodes[1][0].words[1], 0);
  setFloat(fixture.nodes[1][0].words[2], 0);
  fixture.nodes[0][0].links.push_back({3, 0});
  fixture.others.resize(1);
  fixture.others[0].refs[0] = 1;
  fixture.others[0].refs[1] = 2;
  fixture.others[0].segments.push_back(straightSegment(0, 0, 100, 0));

  const auto direction = roadStartDirection(fixture, {10, 0, 0}, {90, 0, 0});
  assert(direction);
  // Negated tangent oriented toward the finish: spline runs +x, so the
  // negated stock tangent points -x.
  assert((*direction)[0] < -0.99f);
  assert(std::fabs((*direction)[1]) < 1e-4f);
  assert(std::fabs((*direction)[2]) < 1e-4f);
  assert(!roadStartDirection(fixture, {10, 0, 0}, {10, 0, 0}));

  const auto flags = roadHaltTrafficPath(fixture, {10, 0, 0}, {90, 0, 0});
  assert(flags && flags->size() == 1 && (*flags)[0] == 1);
  RoadNetwork unreachable;
  unreachable.nodes[0].resize(1);
  unreachable.nodes[1].resize(1);
  unreachable.others.resize(1);
  setFloat(unreachable.nodes[0][0].words[0], 0);
  setFloat(unreachable.nodes[0][0].words[2], 0);
  setFloat(unreachable.nodes[1][0].words[0], 500);
  setFloat(unreachable.nodes[1][0].words[2], 500);
  assert(!roadHaltTrafficPath(unreachable, {0, 0, 0}, {500, 0, 500}));

  // City network: a route between two spawn-area positions must exist and
  // mark at least one road record.
  const auto cityFlags = roadHaltTrafficPath(city, {-278.5f, 9.8f, 1033.0f},
                                             {-278.5f, 9.8f, 1000.0f});
  assert(cityFlags);
  size_t marked = 0;
  for (const auto flag : *cityFlags) marked += flag;
  assert(marked >= 1);
  const auto cityDirection = roadStartDirection(city, {-278.5f, 9.8f, 1033.0f},
                                                {-278.5f, 9.8f, 1000.0f});
  assert(cityDirection);
  double len = 0;
  for (const float v : *cityDirection) len += double(v) * v;
  assert(std::fabs(len - 1.0) < 1e-3);
  return 0;
}
