#include "physics_contact_geometry.hpp"
#include <cstdio>
#include <cstring>
using namespace physics;
static void word(float v){unsigned x;std::memcpy(&x,&v,4);std::printf(" %08x",x);}
int main(){
 for(int k=0;k<21;k++){
  BodyProto p[2];Body b[2];for(int i=0;i<2;i++){b[i].proto=&p[i];b[i].state[1].stepFlags=3;}
  State &a=b[0].state[1],&z=b[1].state[1];a.pos[0]=k==4?0.53f:1.03f;
  if(k==1||k==3){a.rot[0]=0;a.rot[1]=-1;a.rot[4]=1;a.rot[5]=0;}
  if(k>=2){a.pos[0]=0;a.pos[1]=k==4?0.53f:1.03f;}if(k==5)a.pos[1]=0.03f;
  float r0=(k==4||k==5)?0:0.5f,r1=k==5?0:0.5f;
  ContactGeometryWorld world;
  if(k>=6){ b[0].flags=b[1].flags=kBodyActive; b[0].state[0].pos[0]=1.2f; a.pos={k==6?1.01f:k==7?0.9f:k==8?0.5f:1.0f,0,0}; world.stepReady=5; if(k==10)b[0].state[0].pos[0]=0.8f; if(k==11){p[1].flags=kProtoStatic;b[1].flags=0;} if(k==13){a.quat={0,0,0.6f,0.8f};a.rot=matrixFromQuat(a.quat);} if(k==12)for(auto& st:b[0].state){st.quat={0,0,1,0};st.rot=matrixFromQuat(st.quat);} }
  if(k>=14 && k<=16){auto& st=b[k==15?1:0].state[1]; st.quat={k==14?0.6f:0,k==15?0.6f:0,k==16?-0.6f:0,k==16?-0.8f:0.8f}; st.rot=matrixFromQuat(st.quat);}
  if(k==17){p[1].flags=kProtoStatic;b[1].flags=0;b[1].state[0].pos[0]=b[1].state[1].pos[0]=-0.25f;b[0].state[0].pos[0]=0.95f;a.pos[0]=0.65f;}
  CorePair pair=sphereCoreClosest(a,z);DirectContact c{&b[0],&b[1]};Vec3 point{};
  if(k>=18) { b[0].state[0].pos={0.2f,k==19?0.2f:0,k==19?0.2f:0};a.pos={0.2f,0.2f,0.2f}; pair=CorePair{-1.2f,{0,0,0},{-1,0.2f,0.2f},{-1,0,0}}; point[0]=7; if(k==19) c.separation=2; if(k==20){a.quat={0,0,1,0};a.rot=matrixFromQuat(a.quat);} }
  auto result=updateDirectContactGeometryWithSearch(c,pair,r0,r1,point,world,[k](const State& x,const State& y)->std::optional<CorePair>{if(k==19)return CorePair{-1.2f,{0,0,0},{-1,0.2f,0.2f},{-1,0,0}};return sphereCoreClosest(x,y);});if(result!=ContactGeometryResult::Updated)return 1;
  std::printf("%d",k);for(float v:c.pointA)word(v);for(float v:c.pointB)word(v);for(float v:c.normal)word(v);for(float v:point)word(v);word(c.separation);std::printf(" %u %u %u %u\n",b[0].state[1].stepFlags,b[1].state[1].stepFlags,world.queries,world.stepReady);
 }
 for(int k=0;k<8;k++) { Body body; body.flags=kBodyActive; auto& predicted=body.state[1]; predicted.quat={0,0,0,0}; predicted.quat[k/4?0:2]=k/4?0.6f:-0.6f; predicted.quat[3]=k/4?0.8f:-0.8f; predicted.rot=matrixFromQuat(predicted.quat);
  float t=k%4==0?0.5f:k%4==1?0.75f:k%4==2?0.625f:0.5625f; State sampled=interpolateContactPose(body,t);
  std::printf("sample %d",k);for(float v:sampled.quat)word(v);for(int i=0;i<3;i++)for(int j=0;j<3;j++)word(sampled.rot[i*4+j]);std::puts("");
 }

}
