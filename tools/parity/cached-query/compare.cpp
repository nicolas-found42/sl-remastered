#include "physics_cached.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
using namespace physics;
int main(int argc,char**argv) {
 if(argc!=2)return 2; std::ifstream in(argv[1]); if(!in)return 2; std::string line; int compared=0,mismatch=0,rejected=0;
 while(std::getline(in,line)) {std::istringstream s(line);std::string tag; int k,idx,kind,after; s>>tag;
 if(tag!="facecase")continue; s>>k>>idx>>kind>>after; unsigned stock[10];for(auto& v:stock)s>>std::hex>>v; if(!s)return 2;
 if(kind!=3)continue;
 CollisionCore core;std::string error;
 if(k%24<12)core=boxCore({1,2,3});else if(!meshCore({{0,0,0},{1,0,0},{0,1,0},{0,0,1}},{{0,2,1},{0,1,3},{0,3,2},{1,2,3}},core,error))return 3;
 State point,poly;float cases[][3]={{3,0,0},{3,3,0},{3,3,3},{0,0,0},{0.2f,0.2f,0.2f},{-1,-1,-1}};
 for(int i=0;i<3;i++)point.pos[i]=cases[k%6][i];
 if((k/6)%2){poly.rot[0]=0.6f;poly.rot[1]=-0.8f;poly.rot[4]=0.8f;poly.rot[5]=0.6f;}
 bool first=k%48<24;if(first)point.pos[0]+=0.125f;else poly.pos[0]+=0.125f;
 auto result=cachedPointFace(point,core,poly,idx,first);if(!result){rejected++;if(stock[0]!=0 || after!=idx){mismatch++;printf("rejection mismatch %d distance %08x feature %d -> %d\n",k,stock[0],idx,after);}continue;}
 float values[]={result->distance,result->pointA[0],result->pointA[1],result->pointA[2],result->pointB[0],result->pointB[1],result->pointB[2],result->normal[0],result->normal[1],result->normal[2]};
 bool differs=false;for(int j=0;j<10;j++){unsigned u;memcpy(&u,&values[j],4);if(u!=stock[j])differs=true;}compared++;if(differs){mismatch++;printf("mismatch %d",k);for(auto v:values){unsigned u;memcpy(&u,&v,4);printf(" %08x",u);}printf("\n");}
 }
 printf("compared %d mismatches %d rejected %d\n",compared,mismatch,rejected);return mismatch || compared==0 || rejected==0 ? 1 : 0;
}
