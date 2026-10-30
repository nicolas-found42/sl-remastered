#include "physics_friction.hpp"
#include <cassert>
using namespace physics;
int main() {
 BodyProto p, wall; wall.flags=kProtoStatic; Body a,b; a.proto=&p; b.proto=&p;
 DirectContact c{&a,&b,{1,0,0},0}; DirectContactWorld w;
 a.state[1].stepFlags=3; b.state[1].stepFlags=3;
 assert(correctDirectContact(c,w)==1); assert(w.corrections==1);
 assert(a.state[0].shift[0]==0.0105f && b.state[0].shift[0]==-0.0105f);
 assert(a.state[1].stepFlags==2 && b.state[1].stepFlags==2);
 assert(correctDirectContact(c,w)==0 && w.corrections==1);
 a.state[1].vel[0]=-1; auto before=a.state[0];
 assert(correctDirectContact(c,w)==-1); assert(a.state[0].shift==before.shift && w.corrections==1);
 p.invMass=1; a.state[1].vel={-2,1,0}; c.separation=0.03f; c.friction=0.5f;
 w.step=0.01f;
 assert(correctDirectContact(c,w)==2); assert(a.state[0].vel[0]==0.9f);
 assert(b.state[0].vel[0]==-0.9f); assert(a.state[0].force[1]==-45.0f);
 assert(b.state[0].force[1]==45.0f); assert(c.normalImpulse==0.9f);
 assert(c.frictionForce==45.0f); assert(w.corrections==2);
 c.pointA[0]=1; before=a.state[0]; auto beforeImpulse=c.normalImpulse;
 w.step=0;
 assert(correctDirectContact(c,w)==-1); assert(a.state[0].vel==before.vel);
 assert(c.normalImpulse==beforeImpulse && w.corrections==2);
 Body x,y; x.proto=&p; y.proto=&p; x.state[0].invInertiaWorld=matrixIdentity();
 y.state[0].invInertiaWorld=matrixIdentity(); x.state[1].vel={-2,1,0};
 DirectContact off{&x,&y,{1,0,0},0.03f}; off.pointA[1]=1; off.friction=0.5f;
 DirectContactWorld movingWorld; movingWorld.step=0.01f;
 assert(correctDirectContact(off,movingWorld)==2);
 assert(x.state[0].angMom[2]==-off.normalImpulse);
 assert(x.state[0].omega[2]==x.state[0].angMom[2]);
 assert(x.state[0].force[1]==-off.frictionForce);
 assert(movingWorld.corrections==1);
 Body child, nested; child.proto=&wall; child.flags=kBodyChild;
 child.attachment=BodyAttachment{&x,RelativeBodyPose({0.5f,0,0},{0,0,0,1})};
 nested.proto=&p; nested.flags=kBodyChild;
 nested.attachment=BodyAttachment{&child,RelativeBodyPose({0,0.5f,0},{0,0,0,1})};
 off.a=&nested; off.normalImpulse=0; off.frictionForce=0;
 assert(correctDirectContact(off,movingWorld)==2);
 assert(off.normalImpulse>0 && movingWorld.corrections==2);
 assert(child.state[0].vel==Vec3{} && nested.state[0].vel==Vec3{});
 child.attachment->parent=&nested;
 auto unchanged=x.state[0];
 assert(correctDirectContact(off,movingWorld)==-1);
 assert(x.state[0].vel==unchanged.vel && movingWorld.corrections==2);
}
