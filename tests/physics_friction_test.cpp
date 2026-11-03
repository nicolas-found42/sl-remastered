#include "physics_friction.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <bit>
#include <cstring>
#include "physics_contact_stock_fixture.hpp"
using namespace physics;
static void testStockContactGrouping() {
  const auto& in = stock_contact_fixture::input;
  BodyProto proto[3];
  Body body[3];
  for (unsigned i = 0; i < 3; ++i) {
    proto[i].flags = in.proto[i];
    proto[i].invMass = std::bit_cast<float>(in.mass[i]);
    body[i].proto = &proto[i];
    body[i].flags = in.flags[i];
    body[i].current = in.current[i];
    static_assert(sizeof body[i].state == sizeof in.states[i]);
    std::memcpy(body[i].state, in.states[i], sizeof body[i].state);
    if (in.parent[i] >= 0) body[i].attachment = BodyAttachment{&body[in.parent[i]], RelativeBodyPose({std::bit_cast<float>(in.position[i][0]), std::bit_cast<float>(in.position[i][1]), std::bit_cast<float>(in.position[i][2])}, {0, 0, 0, 1})};
  }
  DirectContact c;
  c.a = &body[in.a];
  c.b = &body[in.b];
  c.resting = in.resting != 0;
  for (unsigned i = 0; i < 3; ++i) {
    c.pointA[i] = std::bit_cast<float>(in.contact[i]);
    c.pointB[i] = std::bit_cast<float>(in.contact[i + 3]);
    c.normal[i] = std::bit_cast<float>(in.contact[i + 6]);
  }
  c.separation = std::bit_cast<float>(in.contact[9]);
  c.friction = std::bit_cast<float>(in.contact[10]);
  c.bounce = std::bit_cast<float>(in.contact[11]);
  c.normalImpulse = std::bit_cast<float>(in.contact[12]);
  c.normalForce = std::bit_cast<float>(in.contact[13]);
  c.frictionForce = std::bit_cast<float>(in.contact[14]);
  DirectContactWorld world{in.corrections, std::bit_cast<float>(in.dt)};
  assert(correctDirectContact(c, world) == static_cast<int>(in.expected_result));
  assert(world.corrections == in.expected_corrections);
  for (unsigned bi = 0; bi < 3; ++bi)
    for (unsigned i = 0; i < 106; ++i) {
      uint32_t bits;
      std::memcpy(&bits, reinterpret_cast<const unsigned char*>(body[bi].state) + i * 4, 4);
      assert(bits == in.expected_states[bi][i]);
    }
  const float values[] = {c.pointA[0], c.pointA[1], c.pointA[2], c.pointB[0], c.pointB[1], c.pointB[2], c.normal[0], c.normal[1], c.normal[2], c.separation, c.friction, c.bounce, c.normalImpulse, c.normalForce, c.frictionForce};
  for (unsigned i = 0; i < 15; ++i) assert(std::bit_cast<uint32_t>(values[i]) == in.expected_contact[i]);
}
int main() {
 testStockContactGrouping();
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
 // Stock batch normal rows: signed/unbounded diagonal solutions, no
 // projected friction, no immediate predicted-velocity or pose mutation.
 BodyProto dynamicProto, staticProto;
 dynamicProto.invMass=1; staticProto.invMass=0; staticProto.flags=kProtoStatic;
 Body dynamicBody, staticBody;
 dynamicBody.proto=&dynamicProto; staticBody.proto=&staticProto;
 dynamicBody.state[0].invInertiaWorld=matrixIdentity();
 dynamicBody.state[1].stepFlags=1; staticBody.state[1].stepFlags=1;
 dynamicBody.state[1].vel={0,-1,0};
 DirectContact resting{&dynamicBody,&staticBody,{0,1,0},0.02f};
 resting.resting=true; resting.constraintRow=true; resting.friction=10;
 std::vector<DirectContact> rows{resting,resting};
 DirectContactWorld batchWorld{0,0.1f};
 const State predicted=dynamicBody.state[1];
 assert(solveRestingContacts(rows,batchWorld)==1);
 // RHS=.02-(1+0)*((-1+.2)+.2)=.62; each row /1 /dt.
 assert(std::abs(dynamicBody.state[0].force[1]-12.4f)<0.00001f);
 assert(dynamicBody.state[1].vel==predicted.vel);
 assert(dynamicBody.state[1].stepFlags==predicted.stepFlags);
 assert(dynamicBody.state[0].vel==Vec3{} && dynamicBody.state[0].shift==Vec3{});
 assert(rows[0].processed && rows[1].processed);
 assert(rows[0].frictionForce==0 && rows[0].normalImpulse==0);
 // Invalid prediction activates the positional RHS, not velocity projection.
 dynamicBody.state[0].force={}; dynamicBody.state[1].vel={};
 dynamicBody.state[1].stepFlags=0;
 rows={resting};
 assert(solveRestingContacts(rows,batchWorld)==1);
 assert(std::abs(dynamicBody.state[0].force[1]-1.0f)<0.00001f);
 // No nonnegative bound in 004927f0: signed resting response is retained.
 dynamicBody.state[0].force={}; dynamicBody.state[1].stepFlags=1;
 resting.separation=-0.01f; rows={resting};
 assert(solveRestingContacts(rows,batchWorld)==1);
 assert(std::abs(dynamicBody.state[0].force[1]+0.1f)<0.00001f);
 // A malformed later row must reject the whole batch before force apply.
 const State savedBatch=dynamicBody.state[0];
 rows={resting,resting}; rows[1].normal[0]=std::numeric_limits<float>::quiet_NaN();
 assert(solveRestingContacts(rows,batchWorld)==-1);
 assert(dynamicBody.state[0].force==savedBatch.force);
 assert(!rows[0].processed);
 // Ordinary contacts stay in world+0x118, not the generic constraint list.
 rows={resting}; rows[0].constraintRow=false;
 assert(solveRestingContacts(rows,batchWorld)==0);
 assert(dynamicBody.state[0].force==savedBatch.force);
 // 00496fe0 calls 00496050 for type7 too: force-stage normal and
 // Coulomb-limited tangent forces, never normal impact momentum.
 Body directBody; directBody.proto=&dynamicProto;
 directBody.state[0].invInertiaWorld=matrixIdentity();
 directBody.state[1].vel={1,-1,0};
 directBody.state[1].stepFlags=0; staticBody.state[1].stepFlags=0;
 DirectContact direct{&directBody,&staticBody,{0,1,0},0.02f};
 direct.resting=true; direct.friction=0.5f;
 assert(correctDirectContact(direct,batchWorld)==1); // positional shift overrides sliding status
 assert(std::abs(directBody.state[0].force[1]-9.0f)<0.00001f);
 assert(std::abs(directBody.state[0].force[0]+4.5f)<0.00001f);
 assert(directBody.state[0].vel==Vec3{} && direct.normalImpulse==0);
 assert(std::abs(direct.normalForce-9.0f)<0.00001f);
}
