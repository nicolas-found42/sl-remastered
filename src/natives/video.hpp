#pragma once
#include "natives/registry.hpp"
NativeResult gfxOpenVideo(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult gfxCloseVideo(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult gfxIsPlayingVideo(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
