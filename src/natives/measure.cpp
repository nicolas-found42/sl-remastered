#include "natives/measure.hpp"

NativeResult setMeasure(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                        const std::vector<const TufaClass*>*) {
	if (!arena || !arena->measurementState || args.size() != 1 || !std::holds_alternative<float>(args[0]))
		return std::string{"invalid setMeasure call"};
	const float value = std::get<float>(args[0]);
	// These two values are used by Config.loadConfig; general x87 parity is unverified.
	if (value != 1000.0f && value != 1600.0f) return std::string{"unsupported setMeasure value"};
	auto& state = *arena->measurementState;
	state.value = value;
	state.product = static_cast<float>(static_cast<double>(value) * static_cast<double>(0x1.234568p-12f));
	state.quotient = static_cast<float>(10.0 / static_cast<double>(value));
	return std::monostate{};
}
