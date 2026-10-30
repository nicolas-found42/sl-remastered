#pragma once
#include "natives/registry.hpp"
NativeResult soundSetVolume(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult soundGetVolume(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult soundChangeMusicSet(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>* classes);
NativeResult soundNextTrack(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult soundPrevTrack(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
NativeResult soundSetMode(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                          const std::vector<const TufaClass*>* classes);
NativeResult soundGetMode(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                          const std::vector<const TufaClass*>* classes);
