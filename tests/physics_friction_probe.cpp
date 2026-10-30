#include "physics_friction.hpp"
#include <cstdio>
#include <cstring>
using namespace physics;
static void word(float x) { unsigned u; std::memcpy(&u,&x,4); std::printf(" %08x",u); }
int main() {
 for(int k=0;k<21;k++) { BodyProto p[4]; Body b[4]; for(int i=0;i<4;i++){ b[i].proto=&p[i]; b[i].state[0].stepFlags=b[i].state[1].stepFlags=3; }
  if(k==1) p[1].flags=kProtoStatic; if(k==2) p[0].flags=kProtoStatic; if(k==4) b[0].state[0].shift[0]=0.03f;
  if(k==5) b[0].current=b[1].current=1;
  if(k>=6) { p[0].invMass=1; p[1].invMass=k==9?0.5f:1; b[0].state[1].vel[0]=-2; if(k>=7) b[0].state[1].vel[1]=1; if(k==8) p[1].flags=kProtoStatic; }
  if(k>=10) { for(auto& x:b) for(auto& st:x.state) st.invInertiaWorld=matrixIdentity(); if(k>=12) b[0].state[1].omega[2]=0.5f; if(k==13) p[1].flags=kProtoStatic; if(k>=14) for(auto& st:b[0].state) { st.rot[0]=0; st.rot[1]=-1; st.rot[4]=1; st.rot[5]=0; st.invInertiaWorld[0]=0.5f; st.invInertiaWorld[5]=2; st.invInertiaWorld[10]=0.25f; } if(k==15) b[0].state[1].omega[0]=0.5f; }
  DirectContact c{&b[0],&b[1],{1,0,0},(k==3||k>=6)?0.03f:0}; if(k>=7) c.friction=0.5f; if(k>=10) c.pointA[1]=1; if(k>=11) c.pointB[2]=0.5f; if(k==15) { c.pointA[0]=0.5f; c.pointA[2]=0.5f; } if(k>=16) {
   b[2].flags=kBodyChild; b[2].attachment=BodyAttachment{&b[0],RelativeBodyPose({0.5f,0.25f,0},{0,0,0,1})}; c.a=&b[2];
   if(k==17 || k==20) p[2].flags=kProtoStatic;
   if(k==18) { b[3].flags=kBodyChild; b[3].attachment=BodyAttachment{&b[2],RelativeBodyPose({0,0,0.5f},{0,0,0,1})}; c.a=&b[3]; }
   if(k==19) b[2].attachment->relative.setOrientation({0,0,1,0});
   if(k==20) { b[2].attachment->parent=&b[1]; c.a=&b[0]; c.b=&b[2]; p[1].flags=kProtoStatic; }
  }
  DirectContactWorld w;
  w.step=0.01f;
  int result=correctDirectContact(c,w); std::printf("case=%d result=%d world=%u",k,result,w.corrections);
  for(auto& x:b) {for(float v:x.state[x.current].shift) word(v); std::printf(" %u",x.state[x.current^1].stepFlags);} for(auto& x:b) for(int j=0;j<3;j++) {word(x.state[0].vel[j]); word(x.state[0].force[j]); word(x.state[0].omega[j]); word(x.state[0].angMom[j]); word(x.state[0].torque[j]);} word(c.normalImpulse); word(c.frictionForce); std::puts("");
 }
}
