#include "dds.hpp"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
int main(int argc,char**argv){
 if(argc!=2)return 2;FILE* file=std::fopen(argv[1],"rb");if(!file)return 3;
 uint32_t count;if(std::fread(&count,4,1,file)!=1)return 4;
 for(uint32_t i=0;i<count;i++){
  uint32_t size;if(std::fread(&size,4,1,file)!=1)return 5;std::vector<uint8_t> data(size);
  if(std::fread(data.data(),1,size,file)!=size)return 6;
  DdsImage image;std::string error;bool ok=ddsParseCompressed(data.data(),data.size(),image,error);
  std::printf("case=%u accepted=%d",i,ok);
  uint32_t format=image.format==DdsImage::Format::Dxt1?0x31545844u:image.format==DdsImage::Format::Dxt3?0x33545844u:0x35545844u;
  if(ok)for(const auto& level:image.levels)std::printf(" %u,%u,%u,%zu,%u,%zu",format,level.width,level.height,level.offset,((level.width+3)/4)*(image.format==DdsImage::Format::Dxt1?8:16),level.size);
  std::puts("");
 }std::fclose(file);return 0;
}
