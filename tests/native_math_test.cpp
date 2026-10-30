#include "natives/registry.hpp"
#include <cmath>
#include <limits>
#include <vector>

int main() {
	const auto call = [](const std::vector<VmValue>& args) { return invokeNative("java.lang.Math", "sqrt", "(F)F", args); };
	for (const auto pair : std::vector<std::pair<float, float>>{{0.0f, 0.0f}, {1.0f, 1.0f}, {4.0f, 2.0f}, {9.0f, 3.0f}, {0.25f, 0.5f}}) {
		const auto result = call({pair.first});
		const auto* value = std::get_if<VmValue>(&result);
		if (!value || !std::holds_alternative<float>(*value) || std::get<float>(*value) != pair.second) return 1;
	}
	const auto negativeZero = call({-0.0f});
	const auto* z = std::get_if<VmValue>(&negativeZero);
	if (!z || !std::holds_alternative<float>(*z) || !std::signbit(std::get<float>(*z))) return 1;
	for (float value : {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::denorm_min()})
		if (!std::holds_alternative<std::string>(call({value}))) return 1;
	if (!std::holds_alternative<std::string>(call({})) || !std::holds_alternative<std::string>(call({int32_t{1}}))) return 1;
	return 0;
}
