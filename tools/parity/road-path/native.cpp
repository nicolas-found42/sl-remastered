#include "road_network.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

static uint32_t bits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream file(argv[1], std::ios::binary);
  if (!file) return 3;
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  RoadNetwork network;
  std::string error;
  if (!readRoadNetwork(bytes, network, error)) return 4;
  const auto state = roadPathInitialState(network);
  if (!state) return 5;
  const size_t firstOther = 1 + network.nodes[0].size() + network.nodes[1].size() + network.paths.size();
  for (size_t i = 0; i < state->otherValues.size(); ++i)
    std::printf("other %zu %08x\n", firstOther + i, bits(state->otherValues[i]));
  for (size_t i = 0; i < state->pathValues.size(); ++i)
    std::printf("path %zu %08x\n", i, bits(state->pathValues[i]));
  std::printf("maximum %08x\n", bits(state->maximum));
}
