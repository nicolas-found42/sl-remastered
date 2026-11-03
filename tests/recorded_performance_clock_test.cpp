// Actual Port currentTime consumes clock operands projected from a retained Stock
// response tape. This is not a Stock returned-float observation or paired proof.
#include "natives/lang_system.hpp"
#include "vm.hpp"
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <variant>

int main(int argc,char** argv) {
  static_assert(std::endian::native==std::endian::little);
  if(argc<2 || argc>3 || (argc==3 && std::strcmp(argv[2],"--zero-clock"))) return 2;
  std::ifstream input(argv[1],std::ios::binary);
  char magic[8];uint32_t count=0;
  if(!input.read(magic,8) || std::memcmp(magic,"SLRCLK1\n",8) ||
     !input.read(reinterpret_cast<char*>(&count),4) || !count || count>200000) return 3;
  VmArena arena;
  double milliseconds=0;
  uint32_t callbacks=0,failures=0;
  arena.performanceMilliseconds=[&] {++callbacks;return argc==3?0.0:milliseconds;};
  for(uint32_t index=0;index<count;++index) {
    uint32_t expected=0,call=0;
    if(!input.read(reinterpret_cast<char*>(&milliseconds),8) ||
       !input.read(reinterpret_cast<char*>(&expected),4) ||
       !input.read(reinterpret_cast<char*>(&call),4)) return 4;
    const auto result=systemCurrentTime({},nullptr,&arena,nullptr);
    const auto* value=std::get_if<VmValue>(&result);
    const auto* number=value?std::get_if<float>(value):nullptr;
    if(!number || std::bit_cast<uint32_t>(*number)!=expected) {
      if(failures<4)std::printf("mismatch sample=%u captured_call=%u expected=%08x actual=%08x\n",index,call,expected,number?std::bit_cast<uint32_t>(*number):0);
      ++failures;
    }
  }
  char extra;
  if(input.get(extra) || callbacks!=count) return 5;
  arena.performanceMilliseconds={};
  if(!std::holds_alternative<std::string>(systemCurrentTime({},nullptr,&arena,nullptr))) return 6;
  arena.performanceMilliseconds=[] {return std::numeric_limits<double>::quiet_NaN();};
  if(!std::holds_alternative<std::string>(systemCurrentTime({},nullptr,&arena,nullptr))) return 7;
  std::printf("scope=owned_Port_currentTime_from_recorded_clock_projection samples=%u callbacks=%u failures=%u Stock_pair=0\n",count,callbacks,failures);
  return failures?1:0;
}
