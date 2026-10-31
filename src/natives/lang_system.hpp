#pragma once

#include "natives/registry.hpp"

NativeResult systemSetLdPriority(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>* classes);

NativeResult systemArraycopy(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>* classes);
NativeResult systemExitString(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes);
NativeResult systemLog(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                       const std::vector<const TufaClass*>* classes);

NativeResult systemOpenLib(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes);

NativeResult systemCurrentTime(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult systemTimeWarp(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);

NativeResult systemSyncGameTime(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);

NativeResult systemIsLoadingReset(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
NativeResult systemIsLoading(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
