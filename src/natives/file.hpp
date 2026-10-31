#pragma once

#include "natives/registry.hpp"
NativeResult fileExists(const std::vector<VmValue> &args,
                        const VmObject *receiver, VmArena *arena,
                        const std::vector<const TufaClass *> *classes);

NativeResult fileOpenRead(const std::vector<VmValue> &args,
                          const VmObject *receiver, VmArena *arena,
                          const std::vector<const TufaClass *> *classes);
NativeResult fileReadInt(const std::vector<VmValue> &args,
                         const VmObject *receiver, VmArena *arena,
                         const std::vector<const TufaClass *> *classes);
NativeResult fileReadFloat(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           const std::vector<const TufaClass *> *classes);
NativeResult fileClose(const std::vector<VmValue> &args,
                       const VmObject *receiver, VmArena *arena,
                       const std::vector<const TufaClass *> *classes);

NativeResult findFileFirst(const std::vector<VmValue> &, const VmObject *, VmArena *, const std::vector<const TufaClass *> *);
NativeResult findFileNext(const std::vector<VmValue> &, const VmObject *, VmArena *, const std::vector<const TufaClass *> *);
NativeResult findFileClose(const std::vector<VmValue> &, const VmObject *, VmArena *, const std::vector<const TufaClass *> *);

#define SLRR_FILE_NATIVE(name) NativeResult name(const std::vector<VmValue> &, const VmObject *, VmArena *, const std::vector<const TufaClass *> *)
SLRR_FILE_NATIVE(fileWriteInt);
SLRR_FILE_NATIVE(fileWriteFloat);
SLRR_FILE_NATIVE(fileWriteString);
SLRR_FILE_NATIVE(fileWriteResource);
SLRR_FILE_NATIVE(fileReadString);
SLRR_FILE_NATIVE(fileReadResId);
SLRR_FILE_NATIVE(fileDelete);
SLRR_FILE_NATIVE(fileCopy);
SLRR_FILE_NATIVE(fileMove);
#undef SLRR_FILE_NATIVE
