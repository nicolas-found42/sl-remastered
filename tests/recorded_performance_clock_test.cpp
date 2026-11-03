// Existing operand-projection mode and explicit stored-Native-result mode remain
// separate. Neither qualifies game injection, ABI, scheduling or a paired proof.
#include "natives/lang_system.hpp"
#include "vm.hpp"
#include <bit>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace {
enum StoredColumn {
  NativeOrdinal,
  SharedPhase,
  CaptureLane,
  MixedLaneRead,
  Caller,
  Callee,
  ValueBits,
  ColumnCount
};

int storedNativeInput(std::ifstream& input, uint32_t count, bool zeroClock) {
  std::array<unsigned char, 32> tapeSha{};
  if (!input.read(reinterpret_cast<char*>(tapeSha.data()), tapeSha.size())) return 4;
  // This header names the externally bound fixture source; it does not
  // authenticate the tape or qualify the caller/callee or lane identities.
  // Ordinal, global phase, capture lane, mixed API/Native lane read, caller,
  // callee, exact stored binary32 bits. Keep original coordinates throughout.
  std::vector<std::array<uint32_t, ColumnCount>> rows(count);
  std::map<uint32_t, uint32_t> lastLaneRead;
  for (uint32_t index = 0; index < count; ++index) {
    auto& row = rows[index];
    if (!input.read(reinterpret_cast<char*>(row.data()), ColumnCount * sizeof(uint32_t))) return 4;
    if (index && (row[NativeOrdinal] <= rows[index-1][NativeOrdinal] || row[SharedPhase] <= rows[index-1][SharedPhase])) return 4;
    const auto previous = lastLaneRead.find(row[CaptureLane]);
    if (previous != lastLaneRead.end() && row[MixedLaneRead] <= previous->second) return 4;
    lastLaneRead[row[CaptureLane]] = row[MixedLaneRead];
  }
  char extra;
  if (input.get(extra) || !input.eof()) return 5;
  VmArena arena;
  uint32_t consumed = 0, liveReads = 0, failures = 0;
  arena.performanceMilliseconds = [&] { ++liveReads; return 123456.0; };
  arena.storedCurrentTimeBits = [&]() -> VmCurrentTimeBitsResult {
    if (consumed == count) return std::monostate{};
    const auto bits = rows[consumed++][ValueBits];
    return zeroClock ? uint32_t{0} : bits;
  };
  for (uint32_t index = 0; index < count; ++index) {
    const auto& row = rows[index];
    const auto result = systemCurrentTime({}, nullptr, &arena, nullptr);
    const auto* value = std::get_if<VmValue>(&result);
    const auto* number = value ? std::get_if<float>(value) : nullptr;
    if (!number || std::bit_cast<uint32_t>(*number) != row[ValueBits]) {
      if (failures < 4)
        std::printf("mismatch sample=%u Native_ordinal=%u shared_phase=%u capture_lane=%u mixed_lane_read=%u caller=%08x callee=%08x expected=%08x actual=%08x\n",
                    index, row[NativeOrdinal], row[SharedPhase], row[CaptureLane], row[MixedLaneRead], row[Caller], row[Callee], row[ValueBits], number ? std::bit_cast<uint32_t>(*number) : 0);
      ++failures;
    }
  }
  if (consumed != count || liveReads != 0) return 5;
  if (!std::holds_alternative<std::string>(systemCurrentTime({}, nullptr, &arena, nullptr))) return 6;
  if (liveReads != 0) return 6;
  std::printf("scope=owned_Port_currentTime_from_actual_stored_returns native_input_only=1 samples=%u consumed=%u failures=%u raw_tape_sha256=",
              count, consumed, failures);
  for (const auto byte : tapeSha) std::printf("%02x", static_cast<unsigned>(byte));
  std::printf(" first_Native_ordinal=%u last_Native_ordinal=%u game_ABI_scheduler_pair_qualified=0\n", rows.front()[NativeOrdinal], rows.back()[NativeOrdinal]);
  return failures ? 1 : 0;
}
} // namespace

int main(int argc,char** argv) {
  static_assert(std::endian::native==std::endian::little);
  if(argc<2 || argc>3 || (argc==3 && std::strcmp(argv[2],"--zero-clock"))) return 2;
  std::ifstream input(argv[1],std::ios::binary);
  char magic[8];uint32_t count=0;
  if(!input.read(magic,8) ||
     (std::memcmp(magic,"SLRCLK1\n",8) && std::memcmp(magic,"SLRNAT1\n",8)) ||
     !input.read(reinterpret_cast<char*>(&count),4) || !count || count>200000) return 3;
  if (!std::memcmp(magic, "SLRNAT1\n", 8)) return storedNativeInput(input, count, argc == 3);
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
