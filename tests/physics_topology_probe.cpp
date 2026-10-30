#include "physics_polyhedron.hpp"
#include <cstdio>
#include <cstring>
using namespace physics;
static void words(const Vec3& v){for(float x:v){unsigned u;std::memcpy(&u,&x,4);std::printf(" %08x",u);}}
static void run(int id){
 CollisionCore c; std::string error;
 if(id==0)c=boxCore({1,2,3});
 if(id==1)meshCore({{0,0,0},{1,0,0},{0,1,0},{0,0,1}},{{0,2,1},{0,1,3},{0,3,2},{1,2,3}},c,error);
 if(id==2)c=pointCore();
 if(id==3)c=segmentCore({1,2,3},{4,6,8});
 std::printf("case=%d vertices=%zu features=%zu\n",id,c.vertices.size(),c.features.size());
 for(size_t i=0;i<c.vertices.size();i++){std::printf("v %zu",i);words(c.vertices[i].position);std::puts("");}
 for(size_t i=0;i<c.features.size();i++){
  auto &f=c.features[i]; int kind=(int)f.ref.kind+1; size_t anchor=0; Vec3 n={0,0,0};
  if(kind==1)anchor=f.ref.index;
  if(kind==2){anchor=c.edges[f.ref.index].vertices[0];n=c.edges[f.ref.index].direction;}
  if(kind==3){anchor=c.faces[f.ref.index].vertices[0];n=c.faces[f.ref.index].normal;}
  std::printf("f %zu %d %zu",i,kind,anchor);words(n);std::printf(" %zu\n",f.adjacent.size());
  for(size_t j=0;j<f.adjacent.size();j++){auto &a=f.adjacent[j];size_t neighbor=0;for(;neighbor<c.features.size();neighbor++)if(c.features[neighbor].ref.kind==a.neighbor.kind&&c.features[neighbor].ref.index==a.neighbor.index)break;std::printf("a %zu %zu %zu %zu",i,j,neighbor,a.anchorVertex);words(a.normal);std::puts("");}
 }
}
int main(){for(int i=0;i<4;i++)run(i);}
