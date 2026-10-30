#include "road_other_initial.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
static uint32_t bits(float f) { uint32_t v; std::memcpy(&v, &f, 4); return v; }
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream file(argv[1], std::ios::binary);
  if (!file) return 3;
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  RoadNetwork network; std::string error;
  if (!readRoadNetwork(bytes, network, error)) return 4;
  for (size_t i = 0; i < network.others.size(); ++i) {
    const auto state = roadOtherInitialState(network.others[i], 12.5f);
    if (!state) return 5;
    std::printf("other %zu %08x %08x %u %08x\n", i, bits(state->time), bits(state->curveLength), unsigned(state->fieldC4), bits(state->fieldC8));
  }
}
