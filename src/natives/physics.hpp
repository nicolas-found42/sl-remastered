#pragma once
#include "natives/registry.hpp"
NativeResult physicsCreateBox(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult physicsSetMatrix(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult physicsGetPos(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult physicsGetOri(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
