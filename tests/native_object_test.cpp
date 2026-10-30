// Object.toString()Ljava.lang.String; (handler 0047c870). Stock prints the receiver's class descriptor and then a hex
// word that is the caller's saved ESI, so only the shape "L<class>;<hex>" is checked, not the number.
#include "class_environment.hpp"
#include "natives/registry.hpp"

#include <cstdio>
#include <memory>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
	ClassEnvironment environment("ref/raw");
	std::string error;
	const TufaClass* stringClass = environment.findOrLoad("java.lang.String", error);
	CHECK(stringClass != nullptr);
	if (!stringClass) return 1;
	TufaClass thing;
	thing.name = "test.Thing";
	thing.hasClassHeader = true;
	std::vector<const TufaClass*> classes{stringClass, &thing};

	VmArena arena;
	const auto make = [&](const TufaClass* cls, const char* arrayDescriptor = "") {
		auto object = std::make_unique<VmObject>();
		object->runtimeClass = cls;
		object->arrayDescriptor = arrayDescriptor;
		arena.objects.push_back(std::move(object));
		return arena.objects.back().get();
	};
	const auto toString = [&](const VmObject* receiver, std::vector<VmValue> args = {}) {
		return invokeNative("java.lang.Object", "toString", "()Ljava.lang.String;", args, receiver, &arena, &classes);
	};
	const auto text = [&](const NativeResult& result) -> std::string {
		const auto* value = std::get_if<VmValue>(&result);
		const auto* object = value ? std::get_if<const VmObject*>(value) : nullptr;
		if (!object || !*object) return "<none>";
		for (const auto& entry : arena.strings) if (entry.second == *object) return entry.first;
		return "<not interned>";
	};

	const VmObject* first = make(&thing);
	const VmObject* second = make(&thing);
	const std::string a = text(toString(first)), b = text(toString(second));
	CHECK(a.rfind("Ltest.Thing;<", 0) == 0 && a.back() == '>' && a.size() > 14);
	CHECK(b.rfind("Ltest.Thing;<", 0) == 0 && a != b);
	CHECK(text(toString(first)) == a);

	const auto* resultValue = std::get_if<VmValue>(&static_cast<const NativeResult&>(toString(first)));
	CHECK(resultValue && std::get<const VmObject*>(*resultValue)->runtimeClass == stringClass);

	const auto isError = [](const NativeResult& result) { return std::holds_alternative<std::string>(result); };
	CHECK(isError(toString(first, {VmValue{int32_t{1}}})));
	CHECK(isError(toString(nullptr)));
	VmObject outsider;
	outsider.runtimeClass = &thing;
	CHECK(isError(toString(&outsider)));
	CHECK(isError(toString(make(nullptr, "[Ljava.lang.String;"))));
	CHECK(isError(invokeNative("java.lang.Object", "toString", "()Ljava.lang.String;", {}, first, nullptr, &classes)));

	if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
	std::puts("Object.toString native tests passed");
	return 0;
}
