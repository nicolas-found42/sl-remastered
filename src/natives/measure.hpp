#pragma once
#include "natives/registry.hpp"
NativeResult setMeasure(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                        const std::vector<const TufaClass*>* classes);
