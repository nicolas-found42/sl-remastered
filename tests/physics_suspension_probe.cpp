#include "physics_suspension.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
static void word(float f){uint32_t n;std::memcpy(&n,&f,4);std::printf(" %08x",n);}
int main(){using namespace physics; for(int k=0;k<10;k++){
 BodyProto pa{},pb{}; Body a{},b{},root{},mid{};a.proto=&pa;b.proto=&pb; b.state[0].vel[1]=k==1?0.1f:k==4?-0.1f:0;
 root.proto=&pa; mid.proto=&pa;
 if(k>=5 && k<8){a.flags=kBodyChild; a.attachment=BodyAttachment{k==6?&mid:&root, {{1,0,0},{0,0,0,1}}}; root.state[0].vel[1]=0.1f;root.state[0].omega[2]=0.2f;
 if(k==6){mid.flags=kBodyChild;mid.attachment=BodyAttachment{&root,{{1,0,0},{0,0,0,1}}};}
 if(k==7)a.attachment->relative.setOrientation({0,0,0.70710677f,0.70710677f});}
 SuspensionUpdate u{};u.settings=configureSuspension(1000,200,0.9f,0.9f,0,140,0);u.wheelBody=&a;u.contactBody=&b;u.distanceSquared=k==2?1:k==8?0.01f:0.25f;u.length=0.6f;u.normal={0,k==3?-1.0f:1.0f,0};
 if(k==9){u.forcePointScale=2;u.forcePointThreshold=0.5f;u.forcePointOffset=1;}
 if(k==8){pa.invMass=1;pb.invMass=1;u.settings.minLength=0.2f;u.timeStep=0.1f;b.state[0].vel[1]=0.1f;}
 auto r=updateSuspension(u);std::printf("case=%d %d",k,r==SuspensionUpdateResult::Active);
 for(float f:{u.distanceSquared,u.length,u.force,u.projectedVelocity,u.dampingWork})word(f);
 for(auto v:{u.wheelPoint,u.normalForce,u.normal,(k>=5 && k<8?root:a).state[0].force,(k>=5 && k<8?root:a).state[0].torque})for(float f:v)word(f);std::puts("");
}}
