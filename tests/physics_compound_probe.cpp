#include "physics_compound_mass.hpp"
#include "part_physics.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
using namespace physics;
static void words(const float *p,int n) { for(int i=0;i<n;i++){uint32_t x;std::memcpy(&x,p+i,4);std::printf(" %08x",x);} }
int main() {
 for(int id=0;id<2;id++) {
  BodyProto a,b; a.invMass=.5f; b.invMass=1; a.inertia=matrixIdentity(); b.inertia=matrixIdentity();
  auto rot=matrixIdentity(); if(id){rot[0]=0;rot[1]=-1;rot[4]=1;rot[5]=0;b.inertia[0]=2;b.inertia[5]=3;b.inertia[10]=4;}
  auto result=aggregateCompoundMass({{a,{0,0,0},matrixIdentity()},{b,{3,0,0},rot}});
  auto center=result.center; Vec3 child0{}, child1{3,0,0}; for(int i=0;i<3;i++){child0[i]-=center[i];child1[i]-=center[i];}
  CarConfig config; config.rootBody=0; config.bodies.resize(2); std::vector<CarGeometryPrototype> geometry(2); std::string error;
  geometry[0].bodyIndex=0; *geometry[0].body=a; geometry[1].bodyIndex=1; *geometry[1].body=b;
  for(auto& g: geometry) { g.body->shape.type=ShapeType::Sphere; g.body->shape.radius=1; g.body->shape.boundRadius=1; }
  geometry[1].transform.position={3,0,0};
  if(id) geometry[1].transform.rotation={0,-1,0,1,0,0,0,0,1};
  auto part=buildPartPhysics(config,geometry,"0,0,0,0,0,0,0",error); if(!part){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
  std::printf("part=%d mass",id+2); words(&part->rootPrototype.invMass,1);std::printf(" inertia");words(part->rootPrototype.inertia.data(),12);std::printf(" center");words(part->rootPrototype.shape.centerOffset,3);std::printf(" root");words(part->root->state[0].pos.data(),3);std::printf(" child0");words(part->children[0].body->attachment->relative.position.data(),3);std::printf(" child1");words(part->children[1].body->attachment->relative.position.data(),3);std::printf(" bound");words(&part->rootPrototype.shape.boundRadius,1);std::puts("");
  std::printf("case=%d mass",id); words(&result.prototype.invMass,1);std::printf(" inertia");words(result.prototype.inertia.data(),12);std::printf(" center");words(center.data(),3);std::printf(" root");words(center.data(),3);std::printf(" child0");words(child0.data(),3);std::printf(" child1");words(child1.data(),3);std::puts("");
 }
}
