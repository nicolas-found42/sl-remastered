#pragma once
#include "natives/registry.hpp"
NativeResult sfxPlay(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                     const std::vector<const TufaClass*>* classes);
NativeResult sfxStop(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                     const std::vector<const TufaClass*>* classes);
