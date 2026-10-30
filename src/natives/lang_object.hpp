#pragma once

#include "natives/registry.hpp"

// Handler at 0047c870, java.lang.Object.toString()Ljava.lang.String;. Stock formats "%s<%0x>" with the receiver's
// class type descriptor ("L<class name>;") and then reads a second variadic slot that the call never sets: it is the
// caller's saved ESI, an unspecified register value. The port prints the object's position in the arena instead, so
// the hex part is a stand-in and the row stays unread. Arrays are unsupported.
NativeResult objectToString(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes);
