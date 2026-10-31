#pragma once

#include "natives/registry.hpp"

#include <cstdint>
#include <string>
#include <string_view>

// Stock "%d" style integer formatter at 00554170, including its INT_MIN quirk.
std::string stockFormatInt(int32_t value, int32_t width, char pad);
// Stock float formatter reached through 00551220 -> 00553d70 for
// Float.toString. Returns false and fills error when the format would read
// stack bytes the host cannot model.

// Stock default float formatting for VM string concatenation: FUN_00417da0
// converts a float operand to String with "%.3f" (0x60ba9c) via
// FUN_00551220 -> FUN_00553d70 case 0x66. The int operand uses "%d" (0x60ba84).
bool stockFormatFloatConcat(float value, std::string &out, std::string &error);
bool stockFormatFloat(std::string_view format, float value, std::string &out,
                      std::string &error);

NativeResult integerToString(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *arena,
                             const std::vector<const TufaClass *> *classes);
NativeResult floatToString(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           const std::vector<const TufaClass *> *classes);
NativeResult stringIntValue(const std::vector<VmValue> &args,
                            const VmObject *receiver, VmArena *arena,
                            const std::vector<const TufaClass *> *classes);
NativeResult stringFloatValue(const std::vector<VmValue> &args,
                              const VmObject *receiver, VmArena *arena,
                              const std::vector<const TufaClass *> *classes);
NativeResult stringLength(const std::vector<VmValue> &args,
                          const VmObject *receiver, VmArena *arena,
                          const std::vector<const TufaClass *> *classes);
NativeResult stringAppend(const std::vector<VmValue> &args,
                          const VmObject *receiver, VmArena *arena,
                          const std::vector<const TufaClass *> *classes);
NativeResult stringToken(const std::vector<VmValue> &args,
                         const VmObject *receiver, VmArena *arena,
                         const std::vector<const TufaClass *> *classes);

// The String object for these bytes, as 004174a0 gives it: equal bytes return
// the same object.
NativeResult internStringResult(const std::string &bytes, VmArena *arena,
                                const std::vector<const TufaClass *> *classes);
