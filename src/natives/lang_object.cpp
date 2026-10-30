#include "natives/lang_object.hpp"

#include "natives/lang_string.hpp"

#include <cstdio>

NativeResult objectToString(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes) {
	if (!args.empty() || !receiver || !arena || !classes) return std::string{"invalid Object.toString call"};
	size_t position = 0;
	for (size_t i = 0; i < arena->objects.size(); ++i) if (arena->objects[i].get() == receiver) position = i + 1;
	if (!position) return std::string{"invalid Object.toString receiver"};
	if (!receiver->runtimeClass) return std::string{"Object.toString: array receivers are unsupported"};
	char hex[16];
	std::snprintf(hex, sizeof hex, "%zx", position);
	return internStringResult("L" + receiver->runtimeClass->name + ";<" + hex + ">", arena, classes);
}
