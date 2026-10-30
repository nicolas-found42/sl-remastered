#include "road_other_initial.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  std::ifstream file(argv[1], std::ios::binary);
  if (!file) return 3;
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  RoadNetwork network;
  std::string error;
  if (!readRoadNetwork(bytes, network, error)) return 4;
  for (size_t i = 0; i < network.others.size(); ++i) {
    const auto state = roadOtherInitialState(network.others[i], 0);
    if (!state) return 5;
    uint32_t bits;
    std::memcpy(&bits, &state->curveLength, sizeof(bits));
    std::printf("other %zu %08x\n", i, bits);
  }
}
