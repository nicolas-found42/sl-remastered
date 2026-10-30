#include "native_lang_support.hpp"
#include "natives/vector3.hpp"

#include <cmath>
#include <limits>

static float number(const NativeResult& result) {
	const auto* value = std::get_if<VmValue>(&result);
	return value && std::holds_alternative<float>(*value) ? std::get<float>(*value) : std::nanf("");
}

int main() {
	const auto vectorClass = loadStockClass("Vector3");
	const auto yprClass = loadStockClass("Ypr");
	if (!vectorClass || !yprClass) return 1;
	std::vector<const TufaClass*> classes{vectorClass.get(), yprClass.get()};
	VmArena arena;
	const auto vec = [&](float x, float y, float z) {
		const VmObject* object = instantiate(arena, *vectorClass);
		for (auto& field : object->fields) field.value = field.name == "x" ? x : field.name == "y" ? y : z;
		return object;
	};
	const auto ypr = [&](float yaw, float pitch, float roll) {
		const VmObject* object = instantiate(arena, *yprClass);
		for (auto& field : object->fields) field.value = field.name == "y" ? yaw : field.name == "p" ? pitch : roll;
		return object;
	};
	const auto get = [](const VmObject* object, const char* name) {
		for (const auto& field : object->fields) if (field.name == name) return std::get<float>(field.value);
		return std::nanf("");
	};
	CHECK(vec(1, 2, 3)->fields.size() >= 3);

	// Vector3.length, handler 004823e0.
	CHECK(number(vector3Length({}, vec(3, 4, 0), &arena, &classes)) == 5.0f);
	CHECK(number(vector3Length({}, vec(1, 2, 2), &arena, &classes)) == 3.0f);
	CHECK(number(vector3Length({}, vec(0, 0, 0), &arena, &classes)) == 0.0f);
	CHECK(std::isinf(number(vector3Length({}, vec(std::numeric_limits<float>::infinity(), 0, 0), &arena, &classes))));
	CHECK(std::isnan(number(vector3Length({}, vec(std::nanf(""), 0, 0), &arena, &classes))));
	CHECK(isError(vector3Length({}, nullptr, &arena, &classes)));
	CHECK(isError(vector3Length({1.0f}, vec(1, 2, 3), &arena, &classes)));
	VmObject missing{vectorClass.get(), {{"x", 1.0f, "F"}, {"y", 2.0f, "F"}}};
	CHECK(isError(vector3Length({}, &missing, &arena, &classes)));
	VmObject wrongType{vectorClass.get(), {{"x", 1.0f, "F"}, {"y", 2.0f, "F"}, {"z", int32_t{3}, "I"}}};
	CHECK(isError(vector3Length({}, &wrongType, &arena, &classes)));
	VmObject duplicate{vectorClass.get(), {{"x", 1.0f, "F"}, {"y", 2.0f, "F"}, {"z", 3.0f, "F"}, {"x", 1.0f, "F"}}};
	CHECK(isError(vector3Length({}, &duplicate, &arena, &classes)));

	// Vector3.distance, handler 00482550.
	CHECK(number(vector3Distance({vec(4, 6, 3)}, vec(1, 2, 3), &arena, &classes)) == 5.0f);
	CHECK(number(vector3Distance({vec(1, 2, 3)}, vec(1, 2, 3), &arena, &classes)) == 0.0f);
	CHECK(number(vector3Distance({vec(-1, -2, -2)}, vec(1, 2, 2), &arena, &classes)) == 6.0f);
	CHECK(isError(vector3Distance({static_cast<const VmObject*>(nullptr)}, vec(1, 2, 3), &arena, &classes)));
	CHECK(isError(vector3Distance({}, vec(1, 2, 3), &arena, &classes)));
	CHECK(isError(vector3Distance({int32_t{1}}, vec(1, 2, 3), &arena, &classes)));
	CHECK(isError(vector3Distance({&missing}, vec(1, 2, 3), &arena, &classes)));

	// Vector3.normalize, handler 00482450.
	const VmObject* n = vec(0, 3, 4);
	CHECK(std::holds_alternative<std::monostate>(vector3Normalize({}, n, &arena, &classes)));
	CHECK(get(n, "x") == 0.0f && get(n, "y") == 0.6f && get(n, "z") == 0.8f);
	const VmObject* zero = vec(0, 0, 0);
	CHECK(std::holds_alternative<std::monostate>(vector3Normalize({}, zero, &arena, &classes)));
	CHECK(get(zero, "x") == 0.0f && get(zero, "y") == 0.0f && get(zero, "z") == 0.0f);
	const VmObject* nan = vec(std::nanf(""), 1, 1);
	CHECK(std::holds_alternative<std::monostate>(vector3Normalize({}, nan, &arena, &classes)));
	CHECK(std::isnan(get(nan, "x")) && get(nan, "y") == 1.0f);
	const VmObject* unit = vec(2, 0, 0);
	vector3Normalize({}, unit, &arena, &classes);
	CHECK(get(unit, "x") == 1.0f);
	CHECK(isError(vector3Normalize({}, nullptr, &arena, &classes)));
	CHECK(isError(vector3Normalize({}, &wrongType, &arena, &classes)));
	CHECK(std::get<float>(wrongType.fields[0].value) == 1.0f);

	// Vector3.rotate, handler 00482110: zero angles are exact, a quarter turn is checked within a few ulps.
	const VmObject* r = vec(1, 2, 3);
	const auto turned = vector3Rotate({ypr(0, 0, 0)}, r, &arena, &classes);
	CHECK(std::get<VmValue>(turned) == VmValue{r});
	CHECK(get(r, "x") == 1.0f && get(r, "y") == 2.0f && get(r, "z") == 3.0f);
	const float quarter = 1.5707964f;
	const VmObject* q = vec(1, 2, 3);
	vector3Rotate({ypr(quarter, 0, 0)}, q, &arena, &classes);
	CHECK(std::fabs(get(q, "x") - 3.0f) < 1e-6f && std::fabs(get(q, "y") - 2.0f) < 1e-6f && std::fabs(get(q, "z") + 1.0f) < 1e-6f);
	const VmObject* p = vec(1, 2, 3);
	vector3Rotate({ypr(0, quarter, 0)}, p, &arena, &classes);
	const float s1 = 1.0f;
	CHECK(std::fabs(get(p, "y") - (-s1 * 3.0f)) < 1e-6f);
	const VmObject* lengthKept = vec(1, 2, 3);
	vector3Rotate({ypr(0.3f, -0.7f, 1.1f)}, lengthKept, &arena, &classes);
	CHECK(std::fabs(number(vector3Length({}, lengthKept, &arena, &classes)) - std::sqrt(14.0f)) < 1e-5f);
	CHECK(isError(vector3Rotate({static_cast<const VmObject*>(nullptr)}, vec(1, 2, 3), &arena, &classes)));
	CHECK(isError(vector3Rotate({vec(0, 0, 0)}, vec(1, 2, 3), &arena, &classes)));
	CHECK(isError(vector3Rotate({ypr(0, 0, 0)}, nullptr, &arena, &classes)));
	CHECK(isError(vector3Rotate({}, vec(1, 2, 3), &arena, &classes)));
	const auto checkDirection = [&](float x, float y, float z, float yaw, float pitch) {
		const VmObject* result = ypr(9, 9, 9);
		CHECK(std::holds_alternative<std::monostate>(yprFromVector3({vec(x, y, z)}, result, &arena, &classes)));
		CHECK(std::fabs(get(result, "y") - yaw) < 1e-6f);
		CHECK(std::fabs(get(result, "p") - pitch) < 1e-6f);
		CHECK(get(result, "r") == 0.0f);
	};
	checkDirection(0, 0, -1, 0, 0);
	checkDirection(1, 0, 0, -quarter, 0);
	checkDirection(-1, 0, 0, quarter, 0);
	checkDirection(0, 1, 0, 0, quarter);
	checkDirection(0, -1, 0, 0, -quarter);
	checkDirection(0, 0, 0, 0, -quarter);
	checkDirection(1, 1, -1, -0.7853982f, 0.6154797f);
	CHECK(isError(yprFromVector3({static_cast<const VmObject*>(nullptr)}, ypr(0, 0, 0), &arena, &classes)));
	CHECK(isError(yprFromVector3({vec(1, 2, 3)}, nullptr, &arena, &classes)));
	return failures ? 1 : 0;
}
