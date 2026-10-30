#include "part_config.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <euler_cases.h>
int main() {

 for (unsigned i=0;i<sizeof cases/sizeof cases[0];++i) { PartSlotDirective d{}; for(int j=0;j<3;++j) d.orientation[j]=cases[i][j]; PartSlotState s{}; if(!makePartSlotState(d,s)) return 1; std::printf("case=%u",i); for(float v:s.rotation) { uint32_t u; std::memcpy(&u,&v,4); std::printf(" %08x",u); } std::puts(""); }
}
