#include "road_network.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
int main(int argc,char**argv){if(argc!=2)return 2;std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});RoadNetwork net;std::string error;if(!readRoadNetwork(b,net,error))return 3;size_t cursor=20;for(unsigned group=0;group<2;group++){size_t links=0,lanes=0;uint32_t hash=2166136261u;auto mix=[&](uint32_t v){std::printf("%08x ",v);hash=(hash^v)*16777619u;};for(auto&n:net.nodes[group]){links+=n.links.size();lanes+=n.lanes.size();cursor+=4+n.links.size()*5+4+n.lanes.size()*69+16;for(auto&l:n.links){mix(l.target);mix(l.flag);}for(auto w:n.words)mix(w);}std::printf("group=%u records=%zu links=%zu lanes=%zu cursor=%zu hash=%08x\n",group,net.nodes[group].size(),links,lanes,cursor,hash);}}
