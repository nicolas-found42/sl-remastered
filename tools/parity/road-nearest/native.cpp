#include "road_network.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
int main(int argc,char**argv){
 if(argc!=3)return 2; RoadNetwork n; std::string error;
 const float city[][5]={{0,0,0,0,0},{0,0,0,100,0},{20,3,-400,20,0},{0,1000,0,0,1},{0,0,0,0,1},{10,-2,20,-10,1}};
 const float edge[][8]={{3e20f,0,0,2e20f,0,0,0,0},{1e20f,1e20f,1e20f,1.5e20f,0,0,0,0},{16777216.f,1,1,16777216.f,0,0,0,0},{1,5,0,2,5,0,0,1},{10,0,0,-10,0,0,10,0},{3.40282346e38f,0,0,2e38f,0,0,0,0}};
 bool isEdge=std::string(argv[1])=="edge";
 if(!isEdge){std::ifstream f(argv[2],std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});if(!readRoadNetwork(b,n,error))return 3;}else n.nodes[0].resize(2);
 for(int i=0;i<6;i++){
  std::array<float,3> p{};float distance;bool height;
  if(isEdge){for(int a=0;a<3;a++){memcpy(&n.nodes[0][0].words[a],&edge[i][a],4);memcpy(&n.nodes[0][1].words[a],&edge[i][a+3],4);}distance=edge[i][6];height=edge[i][7]!=0;}
  else{p={city[i][0],city[i][1],city[i][2]};distance=city[i][3];height=city[i][4]!=0;}
  auto found=roadNearestCrossing(n,p,distance,height);int result=-1;
  if(found)result=int(found->index+(found->kind==RoadRecordKind::SecondNode?n.nodes[0].size():0));
  std::printf("case=%d index=%d\n",i,result);
 }
}
