#pragma once

#include "vm.hpp"

struct WorldNode;

// Both the deferred frame request and Chassis.forceUpdate enter the same native
// refresh. The caller/arena and world-owned node must outlive this synchronous
// call. Script requests raised during the callback remain pending for next frame.
bool refreshCarVariables(WorldNode& node, VmArena& arena,
                         const VmScriptCall& callScript, std::string& error);

// 004942b0: call after the Car's script addpart callback. Only the installed
// slot-401 engine (or the Car fallback when that slot is empty) binds its Dyno.
bool bindAddedCarEngine(WorldNode& car, WorldNode& added, VmArena& arena,
                        std::string& error);
