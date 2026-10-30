#include "physics_contact_geometry.hpp"
#include <cassert>
using namespace physics;
int main() {
 BodyProto p,wall; p.invMass=1; wall.flags=kProtoStatic; Body a,b; a.proto=&p;b.proto=&wall;
 a.state[1].pos={1.03f,0,0}; a.state[1].vel={-2,0,0};
 DirectContact c{&a,&b}; Vec3 point{};
 auto pair=sphereCoreClosest(a.state[1],b.state[1]);
 assert(updateDirectContactGeometry(c,pair,a.state[1],b.state[1],0.5f,0.5f,point)==ContactGeometryResult::Updated);
 assert(c.pointA[0]==-0.5f && c.pointB[0]==0.5f && c.normal[0]==1);
 DirectContactWorld world; world.step=0.01f;
 assert(correctDirectContact(c,world)==1 && a.state[0].vel[0]>0);
 auto before=c; auto beforePoint=point;
 pair.distance=1.01f;
 assert(updateDirectContactGeometry(c,pair,a.state[1],b.state[1],0.5f,0.5f,point)==ContactGeometryResult::Unsupported);
 assert(c.pointA==before.pointA && c.separation==before.separation && point==beforePoint);
 pair.distance=1.1f;
 assert(updateDirectContactGeometry(c,pair,a.state[1],b.state[1],0.5f,0.5f,point)==ContactGeometryResult::Dropped);
 assert(c.pointA==before.pointA && point==beforePoint);
 a.flags=kBodyActive; b.flags=0; a.state[0].pos={1.2f,0,0}; a.state[1].pos={0.9f,0,0};
 a.state[1].stepFlags=3; ContactGeometryWorld geometryWorld; geometryWorld.stepReady=5;
 auto query=[](const State& x,const State& y)->std::optional<CorePair>{return sphereCoreClosest(x,y);};
 pair=query(a.state[1],b.state[1]).value();
 assert(updateDirectContactGeometryWithSearch(c,pair,0.5f,0.5f,point,geometryWorld,query)==ContactGeometryResult::Updated);
 assert(geometryWorld.queries==12 && geometryWorld.stepReady==0 && a.state[1].stepFlags==2);
 before=c; beforePoint=point; auto saved=geometryWorld; a.state[0].quat={0,0,2,0}; a.state[0].rot=matrixFromQuat(a.state[0].quat);
 assert(updateDirectContactGeometryWithSearch(c,pair,0.5f,0.5f,point,geometryWorld,query)==ContactGeometryResult::Unsupported);
 assert(c.pointA==before.pointA && point==beforePoint && geometryWorld.queries==saved.queries);
}
