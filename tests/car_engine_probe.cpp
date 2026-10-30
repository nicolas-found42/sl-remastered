#include "car_engine.hpp"
#include <cstdio>
#include <cstring>
int main(){
 CarConfig config;const std::vector<std::array<float,2>> custom{{0,1},{1,2},{1000,0.1f},{3000,-1},{7000,0.5f}};
 const float keys[]={-1,0,0.5f,1,199,200,201,1000,1499,1500,1501,3000,4000,4999,5000,6999,7000,10000};
 for(int id=0;id<3;id++)for(unsigned k=0;k<sizeof(keys)/sizeof(keys[0]);k++){
  auto curve=id==2?std::vector<std::array<float,2>>{}:id==1?custom:config.torqueCurve;
  float a=sampleCarTorqueCurve(curve,keys[k]),b=sampleCarTorqueCurve(config.secondaryTorqueCurve,keys[k]);
  unsigned x,y;std::memcpy(&x,&a,4);std::memcpy(&y,&b,4);std::printf("case=%d key=%u %08x %08x\n",id,k,x,y);
 }
}
