#include "physics_compound_step.hpp"
#include <cstdio>
#include <cstring>
using namespace physics;
int main() {
 for(int nested=0;nested<2;++nested) {
  BodyProto p[3]; Body b[3];
  for(int i=0;i<3;++i) {p[i].invMass=1;p[i].flags=kProtoSimple;b[i].proto=&p[i];b[i].flags=i?5:0x109;}
  b[0].state[0].vel={2,1,0}; b[0].state[0].omega[0]=0.25f;b[0].state[1].omega[0]=0.25f;
  b[1].attachment=BodyAttachment{&b[0],{{2,1,0},{0,0,nested?0.70710677f:0,nested?0.70710677f:1}}};
  if(nested) {b[1].flags|=kBodyHasChildren;b[2].attachment=BodyAttachment{&b[1],{{1,3,0},{0,0,0,1}}};}
  if(stepCompound(nested?std::vector<Body*>{&b[0],&b[1],&b[2]}:std::vector<Body*>{&b[0],&b[1]},0.25f)!=CompoundStepResult::Stepped)return 2;
  for(int i=0;i<(nested?3:2);++i) {std::printf("case=%d body=%d current=%u state",nested,i,b[i].current);uint32_t words[53];std::memcpy(words,&b[i].state[b[i].current],sizeof words);for(auto v:words)std::printf(" %08x",v);std::puts("");}
 }
 BodyProto p[3]; Body b[3];
 for(int i=0;i<3;++i){p[i].invMass=1;p[i].flags=kProtoSimple;b[i].proto=&p[i];b[i].flags=i?5:9;}
 p[0].invInertia=matrixIdentity();b[0].state[0].invInertiaWorld=matrixIdentity();b[0].state[1].invInertiaWorld=matrixIdentity();b[0].state[0].angMom[0]=0.25f;p[0].linearDrag=-0.005f;b[0].state[0].vel={2,1,0};b[0].state[0].omega[0]=0.25f;b[0].state[1].omega[0]=0.25f;
 b[1].flags|=kBodyHasChildren;b[1].attachment=BodyAttachment{&b[0],{{2,1,0},{0,0,0.70710677f,0.70710677f}}};
 b[2].attachment=BodyAttachment{&b[1],{{1,3,0},{0,0,0,1}}};
 WorldParams world;
 for(int n=1;n<=100;n++){
  for(auto& v:b)if(!accumulateWorldForces(world,v))return 3;
  std::printf("force step=%d",n);for(float f:b[0].state[b[0].current].force){uint32_t v;std::memcpy(&v,&f,4);std::printf(" %08x",v);}std::puts("");
  if(stepCompound({&b[0],&b[1],&b[2]},0.01f)!=CompoundStepResult::Stepped){std::fprintf(stderr,"step %d rejected or would sleep\n",n);return 4;}
  for(int i=0;i<3;i++){std::printf("trajectory step=%d body=%d current=%u",n,i,b[i].current);uint32_t words[53];std::memcpy(words,&b[i].state[b[i].current],sizeof words);for(auto v:words)std::printf(" %08x",v);std::puts("");}
 }
 Body x[3];
 for(int i=0;i<3;i++){x[i].flags=i?5:9;x[i].current=i&1;for(int j=0;j<2;j++){uint32_t words[53];for(int k=0;k<53;k++)words[k]=0x3f000000+i*0x1000+j*0x100+k;words[15]=3;std::memcpy(&x[i].state[j],words,sizeof words);}for(int k=0;k<3;k++){uint32_t w=0x3f100000+i*0x100+k;std::memcpy(&x[i].activity[k],&w,4);}}
 x[1].flags|=8;x[1].attachment=BodyAttachment{&x[0],{}};x[2].attachment=BodyAttachment{&x[1],{}};
 for(int phase=0;phase<5;phase++){
  if((phase==1||phase==2) && !sleepDetachedCompound({&x[0],&x[1],&x[2]}))return 5;
  if((phase==3||phase==4) && !wakeDetachedCompound({&x[0],&x[1],&x[2]}))return 6;
  for(int i=0;i<3;i++){std::printf("sleep phase=%d body=%d flags=%08x current=%u",phase,i,x[i].flags,x[i].current);uint32_t a[3];std::memcpy(a,x[i].activity.data(),sizeof a);for(auto w:a)std::printf(" %08x",w);uint32_t v[106];std::memcpy(v,x[i].state,sizeof v);for(auto w:v)std::printf(" %08x",w);std::puts("");}
 }

 Body asleep[3];BodyProto asleepProto[3];
 for(int i=0;i<3;i++){asleepProto[i].invMass=1;asleepProto[i].flags=kProtoSimple;asleep[i].proto=&asleepProto[i];asleep[i].flags=i?5:0x109;}
 asleep[1].flags|=8;asleep[1].attachment=BodyAttachment{&asleep[0],{}};asleep[2].attachment=BodyAttachment{&asleep[1],{}};
 if(stepCompound({&asleep[0],&asleep[1],&asleep[2]},0.25f)!=CompoundStepResult::Slept)return 7;
 for(int i=0;i<3;i++){std::printf("sleepstep body=%d flags=%08x current=%u",i,asleep[i].flags,asleep[i].current);uint32_t a[3];std::memcpy(a,asleep[i].activity.data(),sizeof a);for(auto w:a)std::printf(" %08x",w);uint32_t v[106];std::memcpy(v,asleep[i].state,sizeof v);for(auto w:v)std::printf(" %08x",w);std::puts("");}

}
