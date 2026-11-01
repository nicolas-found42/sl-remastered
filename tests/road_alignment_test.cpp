#include "road_alignment.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
void setFloat(uint32_t& word, float value) { std::memcpy(&word, &value, sizeof(float)); }

// One straight road segment from (0,0,0) to (10,0,0) with chord tangents.
RoadSegment straightSegment() {
  RoadSegment s;
  setFloat(s.words[0], 0);
  setFloat(s.words[1], 0);
  setFloat(s.words[2], 0);
  setFloat(s.words[3], 10);
  setFloat(s.words[4], 0);
  setFloat(s.words[5], 0);
  setFloat(s.words[6], 10);
  setFloat(s.words[7], 0);
  setFloat(s.words[8], 0);
  setFloat(s.words[9], 10);
  setFloat(s.words[10], 0);
  setFloat(s.words[11], 0);
  setFloat(s.words[12], 10);
  setFloat(s.words[13], 10);
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

  // Fixture: a single "other" road record with one straight segment.
  RoadNetwork fixture;
  fixture.others.resize(1);
  fixture.others[0].segments.push_back(straightSegment());
  const auto onRoad = roadAlignToRoad(fixture, {6.0f, 3.0f, 0.0f});
  assert(onRoad);
  assert(std::fabs(onRoad->position[0] - 6.0f) < 0.05f);
  assert(std::fabs(onRoad->position[1]) < 0.05f);
  assert(std::fabs(onRoad->position[2]) < 0.05f);
  // x=6 is past half of the 10-unit spline: stock negates the tangent at or
  // past half path length, so the direction points back along -x.
  assert(std::fabs(onRoad->direction[0] + 1.0f) < 1e-4f);
  assert(std::fabs(onRoad->direction[1]) < 1e-4f);
  assert(std::fabs(onRoad->direction[2]) < 1e-4f);
  const auto firstHalf = roadAlignToRoad(fixture, {2.0f, 0.0f, 0.0f});
  assert(firstHalf && std::fabs(firstHalf->direction[0] - 1.0f) < 1e-4f);
  const auto end = roadAlignToRoad(fixture, {25.0f, 0.0f, 0.0f});
  assert(end && std::fabs(end->position[0] - 10.0f) < 0.05f);
  assert(!roadAlignToRoad(fixture, {std::nanf(""), 0, 0}));
  RoadNetwork empty;
  assert(!roadAlignToRoad(empty, {0, 0, 0}));

  // City network: every node position snaps back near itself.
  for (size_t group = 0; group < 2; ++group)
    for (const auto& node : city.nodes[group]) {
      std::array<float, 3> p{};
      for (size_t i = 0; i < 3; ++i) {
        float v;
        std::memcpy(&v, &node.words[i], sizeof(float));
        p[i] = v;
      }
      const auto aligned = roadAlignToRoad(city, p);
      assert(aligned);
      double d = 0;
      for (size_t i = 0; i < 3; ++i) {
        const double delta = aligned->position[i] - p[i];
        d += delta * delta;
      }
      assert(std::sqrt(d) < 25.0); // crossings sit at the ends of road splines
      double len = 0;
      for (size_t i = 0; i < 3; ++i) len += aligned->direction[i] * aligned->direction[i];
      assert(std::fabs(len - 1.0) < 1e-3);
    }
  return 0;
}
