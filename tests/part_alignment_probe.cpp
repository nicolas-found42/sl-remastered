#include "part_alignment.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
int main() {
  for (int id = 0; id < 3; ++id) {
    physics::State state;
    state.pos = {3,-4,7};
    PartSlotState target{{2,3,-1},{1,0,0,0,1,0,0,0,1},1};
    PartSlotState part{{-2,1,4},{1,0,0,0,1,0,0,0,1},2};
    if (id == 1) {
      state.rot = {0,-1,0,0,1,0,0,0,0,0,1,0};
      target.rotation = {1,0,0,0,0,-1,0,1,0};
      part.rotation = {0,-1,0,1,0,0,0,0,1};
    }
    if (id == 2) {
      state.pos = {1.25f,-3.75f,2.125f};
      state.rot = {0.6f,-0.8f,0,0,0.8f,0.6f,0,0,0,0,1,0};
      target.position = {0.3125f,-1.375f,2.625f};
      target.rotation = {1,0,0,0,0.6f,-0.8f,0,0.8f,0.6f};
      part.position = {-0.875f,1.625f,0.4375f};
      part.rotation = {0.8f,0,-0.6f,0,1,0,0.6f,0,0.8f};
    }
    auto pose = alignPartSlots(state, target, part);
    std::printf("case=%d", id);
    auto print = [](float v) { uint32_t u; std::memcpy(&u, &v, 4); std::printf(" %08x", u); };
    for (auto v : pose.position) print(v);
    for (int i=0; i<12; ++i) if (i%4 != 3) print(pose.rotation[i]);
    std::puts("");
  }
}
