#include "road_network.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
int main(int argc,char**argv){if(argc!=2)return 2;std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});RoadNetwork n;std::string error;if(!readRoadNetwork(b,n,error))return 3;float q[8][4]={{0,0,0,0},{0,1000,0,1},{20,3,-400,0},{10,-2,20,1}};for(int i=4;i<8;i++)for(int j=0;j<3;j++)std::memcpy(&q[i][j],&n.nodes[0][0].words[j],4);auto radius=roadCrossingRadius(n,{RoadRecordKind::FirstNode,0});if(!radius)return 4;q[5][0]+=*radius*0.69f;q[6][0]+=*radius*0.7f;q[7][0]+=*radius*0.71f;for(int i=0;i<8;i++){auto result=roadInsideCrossing(n,{q[i][0],q[i][1],q[i][2]},q[i][3]!=0);if(!result)return 5;std::printf("case=%d inside=%u\n",i,unsigned(*result));}}
