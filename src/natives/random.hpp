#pragma once

#include "natives/registry.hpp"

NativeResult mathRandom(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                        const std::vector<const TufaClass*>* classes);
NativeResult mathRandomize(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);
