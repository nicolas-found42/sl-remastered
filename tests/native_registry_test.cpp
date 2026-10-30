#include "natives/registry.hpp"

#include <cstdio>
#include <variant>

int main() {
	const auto result = invokeNative("java.lang.System", "buildNumber", "()I", {});
	const auto* value = std::get_if<VmValue>(&result);
	if (!value || !std::holds_alternative<int32_t>(*value) || std::get<int32_t>(*value) != 606) {
		std::puts("buildNumber did not return 606");
		return 1;
	}
	if (!std::holds_alternative<std::string>(invokeNative("java.lang.System", "buildNumber", "()V", {})) ||
	    !std::holds_alternative<std::string>(invokeNative("java.lang.System", "buildNumber", "()I", {int32_t{1}}))) {
		std::puts("invalid native call was accepted");
		return 1;
	}
	VmObject object;
	const auto debug = [&](const std::vector<VmValue>& args) {
		return invokeNative("java.sound.Sound", "enableDebugDump", "(Ljava.util.resource.ResourceRef;)I", args);
	};
	for (const VmObject* ref : {static_cast<const VmObject*>(&object), static_cast<const VmObject*>(nullptr)}) {
		const auto response = debug({ref});
		const auto* resultValue = std::get_if<VmValue>(&response);
		if (!resultValue || !std::holds_alternative<int32_t>(*resultValue) || std::get<int32_t>(*resultValue) != 0) {
			std::puts("enableDebugDump did not return zero");
			return 1;
		}
	}
	if (!std::holds_alternative<std::string>(debug({})) ||
		!std::holds_alternative<std::string>(debug({int32_t{0}})) ||
		!std::holds_alternative<std::string>(debug({0.0f})) ||
		!std::holds_alternative<std::string>(debug({&object, &object}))) {
		std::puts("invalid enableDebugDump call was accepted");
		return 1;
	}
	TufaClass cls;
	cls.name = "java.util.resource.ResourceRef";
	cls.hasClassHeader = true;
	std::vector<const TufaClass*> classes{&cls};
	VmArena arena;
	auto first = std::make_unique<VmObject>();
	first->runtimeClass = &cls;
	first->fields.push_back({"ptr", int32_t{0}, "I"});
	const auto* firstPtr = first.get();
	arena.objects.push_back(std::move(first));
	const auto call = [&](const VmObject* receiver) {
		return invokeNative("java.util.resource.ResourceRef", "newNative", "()V", {}, receiver, &arena, &classes);
	};
	if (!std::holds_alternative<std::monostate>(call(firstPtr)) || arena.resourceStates.size() != 1 ||
	    std::get<int32_t>(firstPtr->fields[0].value) != 1 || arena.resourceStates[0].owner != firstPtr) return 1;
	for (uint32_t word : arena.resourceStates[0].words) if (word != 0) return 1;
	if (!std::holds_alternative<std::monostate>(call(firstPtr)) || arena.resourceStates.size() != 1) return 1;
	auto second = std::make_unique<VmObject>();
	second->runtimeClass = &cls;
	second->fields.push_back({"ptr", int32_t{0}, "I"});
	const auto* secondPtr = second.get();
	arena.objects.push_back(std::move(second));
	if (!std::holds_alternative<std::monostate>(call(secondPtr)) || arena.resourceStates.size() != 2 ||
	    std::get<int32_t>(secondPtr->fields[0].value) != 2) return 1;
	secondPtr->fields[0].value = int32_t{1};
	if (!std::holds_alternative<std::string>(call(secondPtr))) return 1;
	secondPtr->fields[0].value = int32_t{99};
	if (!std::holds_alternative<std::string>(call(secondPtr))) return 1;
	if (!std::holds_alternative<std::string>(call(nullptr))) return 1;
	VmObject unowned{&cls, {{"ptr", int32_t{0}, "I"}}};
	if (!std::holds_alternative<std::string>(call(&unowned))) return 1;
	secondPtr->fields[0].value = int32_t{2};
	arena.resourceStates[0].attachment = std::make_shared<VmResourceAttachment>();
	arena.resourceStates[0].attachment->type = 1;
	const auto duplicate = [&](const VmObject* source) {
		return invokeNative("java.util.resource.ResourceRef", "duplicate", "(Ljava.util.resource.ResourceRef;)V",
		                    {source}, secondPtr, &arena, &classes);
	};
	if (!std::holds_alternative<std::monostate>(duplicate(firstPtr)) ||
	    arena.resourceStates[1].attachment != arena.resourceStates[0].attachment) return 1;
	if (!std::holds_alternative<std::monostate>(duplicate(nullptr)) ||
	    arena.resourceStates[1].attachment != arena.resourceStates[0].attachment) return 1;
	arena.resourceStates[0].attachment->type = 7;
	if (!std::holds_alternative<std::string>(duplicate(firstPtr))) return 1;
	secondPtr->fields[0].value = int32_t{99};
	if (!std::holds_alternative<std::string>(duplicate(firstPtr))) return 1;
	std::puts("[{\"class\":\"java.lang.System\",\"method\":\"buildNumber\",\"descriptor\":\"()I\",\"handler\":\"0047c0b0\"}]");
	return 0;
}
