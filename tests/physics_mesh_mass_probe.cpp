#include "physics_mesh_mass.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <string>
static void words(const float *v,int n){for(int i=0;i<n;i++){uint32_t x;std::memcpy(&x,v+i,4);std::printf(" %08x",x);}}
int main(int argc,char**argv){
 for(int id=0;id<2;id++){
 physics::BodyProto body;body.invMass=1;
 std::vector<physics::MeshTriangle> faces;
 if(!id){body.shape.vertices={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};faces={{{0,2,1},{0,0,-1}},{{0,1,3},{0,-1,0}},{{0,3,2},{-1,0,0}},{{1,2,3},{1,1,1}}};}
 else {body.shape.vertices={{2,3,4},{4,3,4},{4,7,4},{2,7,4},{2,3,10},{4,3,10},{4,7,10},{2,7,10}};faces={{{0,2,1},{0,0,-1}},{{0,3,2},{0,0,-1}},{{4,5,6},{0,0,1}},{{4,6,7},{0,0,1}},{{0,1,5},{0,-1,0}},{{0,5,4},{0,-1,0}},{{3,7,6},{0,1,0}},{{3,6,2},{0,1,0}},{{0,4,7},{-1,0,0}},{{0,7,3},{-1,0,0}},{{1,2,6},{1,0,0}},{{1,6,5},{1,0,0}}};}
 physics::Vec3 center;std::string error;if(!physics::applyMeshMassProperties(body,faces,center,error)){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
 std::printf("case=%d volume",id);words(&body.volume,1);std::printf(" center");words(center.data(),3);std::printf(" inverse");words(body.invInertia.data(),12);std::puts("");
 }
 if(argc==2){std::ifstream in(argv[1],std::ios::binary);auto read=[&](auto& x){return bool(in.read(reinterpret_cast<char*>(&x),4));};uint32_t count;if(!read(count))return 2;
 for(uint32_t id=0;id<count;++id){uint32_t nv,nf;physics::BodyProto body;if(!read(nv)||!read(nf)||!read(body.invMass)||nv>1000000||nf>1000000)return 2;body.shape.vertices.resize(nv);std::vector<physics::MeshTriangle> faces(nf);for(auto& v:body.shape.vertices)for(auto& x:v)if(!read(x))return 2;for(auto& f:faces){for(auto& x:f.indices)if(!read(x))return 2;for(auto& x:f.normal)if(!read(x))return 2;}
 physics::Vec3 center;std::string error;if(!physics::applyMeshMassProperties(body,faces,center,error)){std::fprintf(stderr,"case=%u %s\n",id+2,error.c_str());return 1;}std::printf("case=%u volume",id+2);words(&body.volume,1);std::printf(" center");words(center.data(),3);std::printf(" inverse");words(body.invInertia.data(),12);std::puts("");}if(in.peek()!=EOF)return 2;}
}
