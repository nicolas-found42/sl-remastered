#pragma once
#include "natives/registry.hpp"
NativeResult textGetWidthPixels(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult textSetDefaultColor(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult textGetDefaultColor(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult textCreate(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult textUpdate(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult textChangeAlign(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult textChangeColor(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult textSetPos(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
