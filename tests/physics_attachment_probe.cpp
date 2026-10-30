#include "physics_compound.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
int main() {
 using namespace physics;
 const Quat quats[]={{0,0,0,1},{0,0,0.70710677f,0.70710677f},{0.1f,0.2f,0.3f,0.4f},{0.11f,-0.25f,0.31f,0.87f}};
 for(int id=0;id<4;id++) {
  Body parent,child;
  parent.state[0].pos={2,3,4};child.state[0].pos={4,5,6};
  parent.state[0].quat=quats[id];child.state[0].quat=quats[3-id];
  parent.state[0].rot=matrixFromQuat(quats[id]);child.state[0].rot=matrixFromQuat(quats[3-id]);
  if(!attachBody(child,parent))return 1;
  const auto& r=child.attachment->relative;
  std::printf("case=%d",id);
  auto word=[](float f){uint32_t x;std::memcpy(&x,&f,4);std::printf(" %08x",x);};
  for(float f:r.position)word(f);for(float f:r.orientation())word(f);
  for(size_t i=0;i<12;i++)if(i!=3&&i!=7&&i!=11)word(r.rotation()[i]);
  std::puts("");
 }
}
