#pragma once

#include "natives/registry.hpp"

NativeResult vector3Length(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);
NativeResult vector3Distance(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>* classes);
NativeResult vector3Normalize(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult vector3Rotate(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);
NativeResult yprFromVector3(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
