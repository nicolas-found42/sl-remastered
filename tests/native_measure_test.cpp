#include "natives/registry.hpp"
#include <cstdint>
#include <cstring>

static uint32_t bits(float x) { uint32_t result; std::memcpy(&result, &x, sizeof result); return result; }
int main() {
	VmMeasurementState state{};
	VmArena arena; arena.measurementState = &state;
	const auto call = [&](const std::vector<VmValue>& args) {
		return invokeNative("java.lang.System", "setMeasure", "(F)V", args, nullptr, &arena);
	};
	if (!std::holds_alternative<std::monostate>(call({1000.0f})) || bits(state.value) != 0x447a0000 ||
	    bits(state.product) != 0x3e8e38e4 || bits(state.quotient) != 0x3c23d70a) return 1;
	if (!std::holds_alternative<std::monostate>(call({1600.0f})) || bits(state.value) != 0x44c80000 ||
	    bits(state.product) != 0x3ee38e39 || bits(state.quotient) != 0x3bcccccd) return 1;
	if (!std::holds_alternative<std::string>(call({0.0f})) ||
	    !std::holds_alternative<std::string>(call({int32_t{1000}})) ||
	    !std::holds_alternative<std::string>(call({}))) return 1;
	if (bits(state.value) != 0x44c80000 || bits(state.product) != 0x3ee38e39 ||
	    bits(state.quotient) != 0x3bcccccd) return 1;
	return 0;
}
