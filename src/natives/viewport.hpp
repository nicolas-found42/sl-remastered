#pragma once

#include "natives/registry.hpp"

NativeResult cameraCreate(const std::vector<VmValue> &, const VmObject *,
                          VmArena *, const std::vector<const TufaClass *> *);

NativeResult viewportCreate(const std::vector<VmValue> &, const VmObject *,
                            VmArena *, const std::vector<const TufaClass *> *);
NativeResult viewportActivate(const std::vector<VmValue> &, const VmObject *,
                              VmArena *,
                              const std::vector<const TufaClass *> *);
NativeResult viewportDeactivate(const std::vector<VmValue> &, const VmObject *,
                                VmArena *,
                                const std::vector<const TufaClass *> *);
NativeResult viewportDestroy(const std::vector<VmValue> &, const VmObject *,
                             VmArena *, const std::vector<const TufaClass *> *);
NativeResult viewportResize(const std::vector<VmValue> &, const VmObject *,
                            VmArena *, const std::vector<const TufaClass *> *);
NativeResult viewportGetLeft(const std::vector<VmValue> &, const VmObject *,
                             VmArena *, const std::vector<const TufaClass *> *);
NativeResult viewportGetTop(const std::vector<VmValue> &, const VmObject *,
                            VmArena *, const std::vector<const TufaClass *> *);
NativeResult viewportGetWidth(const std::vector<VmValue> &, const VmObject *,
                              VmArena *,
                              const std::vector<const TufaClass *> *);
NativeResult viewportGetHeight(const std::vector<VmValue> &, const VmObject *,
                               VmArena *,
                               const std::vector<const TufaClass *> *);
NativeResult viewportGetAspect(const std::vector<VmValue> &, const VmObject *,
                               VmArena *,
                               const std::vector<const TufaClass *> *);

NativeResult cameraDestroy(const std::vector<VmValue> &, const VmObject *, VmArena *, const std::vector<const TufaClass *> *);
