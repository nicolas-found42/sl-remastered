#include "road_network.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
int main(int argc,char**argv){if(argc!=2)return 2;std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});RoadNetwork n;std::string error;if(!readRoadNetwork(b,n,error))return 3;uint32_t id=1;for(int group=0;group<2;group++)for(size_t i=0;i<n.nodes[group].size();i++){auto r=roadCrossingRadius(n,{group?RoadRecordKind::SecondNode:RoadRecordKind::FirstNode,i});if(!r)return 4;uint32_t bits;std::memcpy(&bits,&*r,4);std::printf("%u %08x\n",id++,bits);}}
