#include "natives/math.hpp"
#include <cfenv>
#include <cmath>

NativeResult mathSqrt(const std::vector<VmValue>& args, const VmObject*, VmArena*,
                      const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid Math.sqrt call"};
	const float value = std::get<float>(args[0]);
	if (!std::isfinite(value) || value < 0.0f || std::fegetround() != FE_TONEAREST ||
	    (value != 0.0f && !std::isnormal(value))) return std::string{"unsupported Math.sqrt input"};
	return VmValue{std::sqrt(value)};
}
