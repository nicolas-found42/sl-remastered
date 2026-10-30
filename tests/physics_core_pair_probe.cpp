#include "physics_core_pair.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace physics;
int main(int argc, char** argv) {
  const auto p=pointCore(), s=segmentCore({-2,0,0},{2,0,0});
  for(int k=0;k<12;k++) {
    State a,b; a.pos={k%3==1?5.f:1.f,k%3==2?0.f:3.f,0};
    if(k%6>=3) {b.rot[0]=0;b.rot[1]=-1;b.rot[4]=1;b.rot[5]=0;}
    auto r=k<6 ? *closestPointSegmentCores(p,a,s,b) : *closestPointSegmentCores(s,b,p,a);
    std::printf("%d",k);
    const float values[]={r.distance,r.pointA[0],r.pointA[1],r.pointA[2],r.pointB[0],r.pointB[1],r.pointB[2],r.normal[0],r.normal[1],r.normal[2]};
    for(float v:values) { uint32_t bits; std::memcpy(&bits,&v,4); std::printf(" %08x",bits); } std::puts("");
  }
  for(int k=0;k<96;k++) {
    const Vec3 cases[]={{3,0,0},{3,3,0},{3,3,3},{0,0,0},{0.2f,0.2f,0.2f},{-1,-1,-1}};
    State a,b; a.pos=cases[k%6]; if((k/6)%2) {b.rot[0]=0;b.rot[1]=-1;b.rot[4]=1;b.rot[5]=0;}
    CollisionCore c; std::string error;
    if(k%24<12) c=boxCore({1,2,3}); else meshCore({{0,0,0},{1,0,0},{0,1,0},{0,0,1}},{{0,2,1},{0,1,3},{0,3,2},{1,2,3}},c,error);
    std::optional<std::size_t> cache; if(k>=48) cache=c.features.size()-1;
    auto r=closestPointPolyhedron(a,c,b,cache,k%48<24);
    if(!r) {std::printf("walk %d unsupported\n",k);continue;}
    std::printf("walk %d %zu",k,*cache);
    const float values[]={r->distance,r->pointA[0],r->pointA[1],r->pointA[2],r->pointB[0],r->pointB[1],r->pointB[2],r->normal[0],r->normal[1],r->normal[2]};
    for(float v:values){uint32_t bits;std::memcpy(&bits,&v,4);std::printf(" %08x",bits);} std::puts("");
  }
  for(int k=0;k<6;k++) {
    const Vec3 cases[]={{3,4,0},{0,0,0},{0.13f,-1.21f,2.78f}};
    State a,b;a.pos=cases[k%3];
    auto r=k<3 ? sphereCoreClosest(a,b) : sphereCoreClosest(b,a);
    std::printf("points %d",k);
    const float values[]={r.distance,r.pointA[0],r.pointA[1],r.pointA[2],r.pointB[0],r.pointB[1],r.pointB[2],r.normal[0],r.normal[1],r.normal[2]};
    for(float f:values){uint32_t x;std::memcpy(&x,&f,4);std::printf(" %08x",x);}std::puts("");
  }
  for(int k=0;k<48;k++) {
    const Vec3 cases[]={{0,0,1},{3,0,1},{5,3,1},{0,3,1},{1,1,0},{3,3,2}};
    State a,b;b.pos=cases[k%6];b.rot[0]=0;b.rot[1]=-1;b.rot[4]=1;b.rot[5]=0;
    std::optional<std::size_t> fa,fb;if(k>=24){fa=2;fb=2;}
    auto r=k%24<12?closestSegmentCores(s,a,s,b,fa,fb):closestSegmentCores(s,b,s,a,fb,fa);
    if(!r){std::printf("segments %d unsupported\n",k);continue;}
    std::printf("segments %d %zu %zu",k,*fa,*fb);
    const float values[]={r->distance,r->pointA[0],r->pointA[1],r->pointA[2],r->pointB[0],r->pointB[1],r->pointB[2],r->normal[0],r->normal[1],r->normal[2]};
    for(float v:values){uint32_t x;std::memcpy(&x,&v,4);std::printf(" %08x",x);}std::puts("");
  }
  for(int k=0;k<128;k++) {
    const Vec3 cases[]={{0.3f,0.2f,5},{0.3f,0.2f,-5},{0.3f,4,0.2f},{0.3f,-4,0.2f},{3,0.2f,0.2f},{-3,0.2f,0.2f},{0.3f,0.2f,0.2f},{0.3f,0.2f,3}};
    State a,b;a.pos=cases[k%8];if((k/8)%2){b.rot[0]=0;b.rot[1]=-1;b.rot[4]=1;b.rot[5]=0;}
    auto seg=segmentCore({-0.1f,0,0},{0.1f,0,0});CollisionCore box;std::string error;
    if(k%64<32)box=boxCore({1,2,3});else meshCore({{0,0,0},{1,0,0},{0,1,0},{0,0,1}},{{0,2,1},{0,1,3},{0,3,2},{1,2,3}},box,error);
    std::optional<std::size_t> fa,fb;if(k>=64){fa=2;fb=box.features.size()-1;}
    auto r=closestSegmentPolyhedron(seg,a,box,b,fa,fb,k%32<16);
    if(!r){std::printf("segmentbox %d unsupported\n",k);continue;}
    std::printf("segmentbox %d %zu %zu",k,*fa,*fb);
    const float values[]={r->distance,r->pointA[0],r->pointA[1],r->pointA[2],r->pointB[0],r->pointB[1],r->pointB[2],r->normal[0],r->normal[1],r->normal[2]};
    for(float v:values){uint32_t x;std::memcpy(&x,&v,4);std::printf(" %08x",x);}std::puts("");
  }
  for(int k=0;k<8;k++) {
    State a,b;a.pos={k%2?1.5f:1.f,0,0};auto box=boxCore({1,2,3});std::optional<std::size_t> fa=2,fb=24;
    auto r=closestSegmentPolyhedron(s,a,box,b,fa,fb,k<4);
    if(!r){std::printf("cross %d unsupported\n",k);continue;}
    std::printf("cross %d %zu %zu",k,*fa,*fb);
    const float values[]={r->distance,r->pointA[0],r->pointA[1],r->pointA[2],r->pointB[0],r->pointB[1],r->pointB[2],r->normal[0],r->normal[1],r->normal[2]};
    for(float v:values){uint32_t x;std::memcpy(&x,&v,4);std::printf(" %08x",x);}std::puts("");
  }
  for(int k=0;k<16;k++) {
    const Vec3 cases[]={{0,1,0},{1,2,0},{3,1,0},{5,1,0},{0,0,0},{-1,1,0},{-3,1,0},{-5,1,0}};
    State a,b;b.pos=cases[k%8];std::optional<std::size_t> fa=2,fb=2;
    auto r=k<8?closestSegmentCores(s,a,s,b,fa,fb):closestSegmentCores(s,b,s,a,fb,fa);
    if(!r){std::printf("parallel %d unsupported\n",k);continue;}
    std::printf("parallel %d %zu %zu",k,*fa,*fb);
    const float values[]={r->distance,r->pointA[0],r->pointA[1],r->pointA[2],r->pointB[0],r->pointB[1],r->pointB[2],r->normal[0],r->normal[1],r->normal[2]};
    for(float v:values){uint32_t x;std::memcpy(&x,&v,4);std::printf(" %08x",x);}std::puts("");
  }
  for(int k=0;k<108;k++) {
    State a,b;int v=k%27;for(int n=0;n<3;n++){a.pos[n]=float((v%3-1)*3);v/=3;}
    if((k/27)%2){b.rot[0]=0;b.rot[1]=-1;b.rot[4]=1;b.rot[5]=0;}
    auto box=boxCore({1,2,3});std::optional<std::size_t> fa,fb;if(k>=54){fa=2;fb=24;}
    auto r=closestSegmentPolyhedron(s,a,box,b,fa,fb,k%54<27);
    if(!r){std::printf("grid %d unsupported\n",k);continue;}
    std::printf("grid %d %zu %zu",k,*fa,*fb);
    const float values[]={r->distance,r->pointA[0],r->pointA[1],r->pointA[2],r->pointB[0],r->pointB[1],r->pointB[2],r->normal[0],r->normal[1],r->normal[2]};
    for(float x:values){uint32_t bits;std::memcpy(&bits,&x,4);std::printf(" %08x",bits);}std::puts("");
  }
  for(int k=0;k<12;k++) {
    State a,b;b.pos={float(4+k%3),5,7};auto ca=boxCore({1,2,3}),cb=boxCore({1,2,3});std::optional<std::size_t> fa,fb;
    for(std::size_t i=0;i<ca.features.size();++i)if(ca.features[i].ref.kind==CoreFeatureKind::Vertex&&ca.features[i].ref.index==7)fa=i;
    for(std::size_t i=0;i<cb.features.size();++i)if(cb.features[i].ref.kind==CoreFeatureKind::Vertex&&cb.features[i].ref.index==0)fb=i;
    auto r=k<6?closestPolyhedronCores(ca,a,cb,b,fa,fb):closestPolyhedronCores(cb,b,ca,a,fb,fa);
    if(!r){std::printf("poly %d unsupported\n",k);continue;}
    std::printf("poly %d %zu %zu",k,*fa,*fb);
    const float values[]={r->distance,r->pointA[0],r->pointA[1],r->pointA[2],r->pointB[0],r->pointB[1],r->pointB[2],r->normal[0],r->normal[1],r->normal[2]};
    for(float x:values){uint32_t bits;std::memcpy(&bits,&x,4);std::printf(" %08x",bits);}std::puts("");
  }
  for(int k=0;k<96;k++) {
    const Vec3 cases[]={{4,5,7},{3,0,0},{0,4,0},{0,0,5},{0,0,0},{1,1,1},{2,2,2},{0.5f,0.5f,0.5f}};
    State a,b;b.pos=cases[k%8];if((k/8)%2){b.rot[0]=0;b.rot[1]=-1;b.rot[4]=1;b.rot[5]=0;}
    CollisionCore c[2];std::string error;
    for(int j=0;j<2;j++)if(k/32==2||(j==1&&k/32==1))meshCore({{0,0,0},{1,0,0},{0,1,0},{0,0,1}},{{0,2,1},{0,1,3},{0,3,2},{1,2,3}},c[j],error);else c[j]=boxCore({1,2,3});
    std::optional<std::size_t> fa,fb;if((k/16)%2){fa=c[0].features.size()-1;fb=c[1].features.size()-1;}
    auto r=(k/8)%2==0?closestPolyhedronCores(c[0],a,c[1],b,fa,fb):closestPolyhedronCores(c[1],b,c[0],a,fb,fa);
    if(!r){std::printf("polymix %d unsupported\n",k);continue;}
    std::printf("polymix %d %zu %zu",k,*fa,*fb);
    const float values[]={r->distance,r->pointA[0],r->pointA[1],r->pointA[2],r->pointB[0],r->pointB[1],r->pointB[2],r->normal[0],r->normal[1],r->normal[2]};
    for(float x:values){uint32_t bits;std::memcpy(&bits,&x,4);std::printf(" %08x",bits);}std::puts("");
  }
  if(argc==2) {
    std::ifstream in(argv[1],std::ios::binary);uint32_t count;in.read(reinterpret_cast<char*>(&count),4);if(!in||!count)return 1;
    for(uint32_t id=0;id<count;id++){
      uint32_t nv,nf;float mass;in.read(reinterpret_cast<char*>(&nv),4);in.read(reinterpret_cast<char*>(&nf),4);in.read(reinterpret_cast<char*>(&mass),4);
      if(!in||nv>10000||nf>10000)return 1;
      std::vector<Vec3> vertices(nv);for(auto& p:vertices)in.read(reinterpret_cast<char*>(p.data()),12);
      std::vector<std::vector<std::size_t>> faces(nf,std::vector<std::size_t>(3));
      for(auto& f:faces){for(auto& i:f){uint32_t x;in.read(reinterpret_cast<char*>(&x),4);i=x;}in.ignore(12);}
      if(!in)return 1;CollisionCore core;std::string error;if(!meshCore(vertices,faces,core,error)){std::printf("car %u geometry-failure\n",id);continue;}
      for(int k=0;k<16;k++) {
        State a,b;b.pos={k%4==0?6.f:k%4==1?0.f:k%4==2?1.f:3.f,k%4==2?1.f:0.f,k%4==3?2.f:0.f};
        if((k/4)%2){b.rot[0]=0;b.rot[1]=-1;b.rot[4]=1;b.rot[5]=0;}
        auto box=boxCore({1,2,3});std::optional<std::size_t> fa,fb;if(k>=8){fa=core.features.size()-1;fb=box.features.size()-1;}
        auto r=(k/4)%2==0?closestPolyhedronCores(core,a,box,b,fa,fb):closestPolyhedronCores(box,b,core,a,fb,fa);
        if(!r){std::printf("car %u %d unsupported\n",id,k);continue;}
        std::printf("car %u %d %zu %zu",id,k,*fa,*fb);const float values[]={r->distance,r->pointA[0],r->pointA[1],r->pointA[2],r->pointB[0],r->pointB[1],r->pointB[2],r->normal[0],r->normal[1],r->normal[2]};
        for(float x:values){uint32_t bits;std::memcpy(&bits,&x,4);std::printf(" %08x",bits);}std::puts("");
      }
    }if(in.peek()!=EOF)return 1;
  }
}
