#pragma once

#include "vm.hpp"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

using NativeResult = std::variant<std::monostate, VmValue, std::string>;
using NativeHandler = NativeResult (*)(const std::vector<VmValue>&, const VmObject*, VmArena*,
                                      const std::vector<const TufaClass*>*);

struct NativeEntry {
	std::string_view className;
	std::string_view method;
	std::string_view descriptor;
	std::string_view handlerAddress;
	NativeHandler handler;
};

const std::vector<NativeEntry>& nativeRegistry();
NativeResult invokeNative(std::string_view className, std::string_view method,
                          std::string_view descriptor, const std::vector<VmValue>& args,
                          const VmObject* receiver = nullptr, VmArena* arena = nullptr,
                          const std::vector<const TufaClass*>* classes = nullptr);

// The resource state that a ResourceRef-derived arena object's `ptr` handle names, or null.
VmResourceState* nativeResourceState(const VmObject* object, VmArena* arena);
