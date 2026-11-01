#include "natives/registry.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>

static uint32_t bits(float x) { uint32_t result; std::memcpy(&result, &x, sizeof result); return result; }
int main() {
	VmMeasurementState state{};
	if (state.value != 1610.0f || state.product != static_cast<float>(1610.0 * static_cast<double>(0x1.234568p-12f)) ||
	    state.quotient != static_cast<float>(10.0 / 1610.0)) return 1;
	VmArena arena; arena.measurementState = &state;
	const auto call = [&](const std::vector<VmValue>& args) {
		return invokeNative("java.lang.System", "setMeasure", "(F)V", args, nullptr, &arena);
	};
	if (!std::holds_alternative<std::monostate>(call({1000.0f})) || bits(state.value) != 0x447a0000 ||
	    bits(state.product) != 0x3e8e38e4 || bits(state.quotient) != 0x3c23d70a) return 1;
	if (!std::holds_alternative<std::monostate>(call({1600.0f})) || bits(state.value) != 0x44c80000 ||
	    bits(state.product) != 0x3ee38e39 || bits(state.quotient) != 0x3bcccccd) return 1;
	if (!std::holds_alternative<std::monostate>(call({1610.0f})) || state.value != 1610.0f ||
	    state.product != static_cast<float>(1610.0 * static_cast<double>(0x1.234568p-12f))) return 1;
	if (!std::holds_alternative<std::monostate>(call({0.0f})) || !std::isinf(state.quotient)) return 1;
	if (!std::holds_alternative<std::monostate>(call({1600.0f}))) return 1;
	if (
	    !std::holds_alternative<std::string>(call({int32_t{1000}})) ||
	    !std::holds_alternative<std::string>(call({}))) return 1;
	if (bits(state.value) != 0x44c80000 || bits(state.product) != 0x3ee38e39 ||
	    bits(state.quotient) != 0x3bcccccd) return 1;
	return 0;
}
