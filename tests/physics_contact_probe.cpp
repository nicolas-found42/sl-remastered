#include "physics_solver.hpp"
#include <cstdio>
#include <cstring>
using namespace physics;
static void word(float v) { unsigned u; std::memcpy(&u, &v, 4); std::printf(" %08x", u); }
int main() {
  for (int k=0;k<5;k++) {
    BodyProto proto; proto.invMass=1;
    BodyProto wall; wall.flags=kProtoStatic;
    Body bodies[4];
    for (int i=0;i<4;i++) {
      bodies[i].proto=i==3?&wall:&proto;
      for (auto& state : bodies[i].state) if (i!=3) state.invInertiaWorld=matrixIdentity();
    }
    for (auto& state : bodies[0].state) { state.vel[0]=-2; state.omega[2]=1; }
    for (int i=0;i<2;i++) {
      RelativeBodyPose pose; pose.position={0,1,0};
      if (i==0 && k==3) pose.setOrientation({0,0,0.70710677f,0.70710677f});
      bodies[i+1].attachment=BodyAttachment{&bodies[i],pose}; bodies[i+1].flags=kBodyChild;
    }
    const int selected=k==0?0:k==2?2:1;
    const Vec3 normal=k==4?Vec3{0.6f,0.8f,0}:Vec3{1,0,0};
    std::vector<Contact> contacts{{&bodies[selected],&bodies[3],normal,0,{0,1,0},{}}};
    if (solveContacts(contacts)!=ContactSolveResult::Solved) return 1;
    std::printf("case=%d",k); word(contacts[0].target); word(contacts[0].impulse);
    for (float v : bodies[selected].state[0].vel) word(v);
    for (float v : bodies[selected].state[0].omega) word(v);
    std::puts("");
  }
}
