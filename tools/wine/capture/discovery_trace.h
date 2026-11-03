#ifndef SLRR_DISCOVERY_TRACE_H
#define SLRR_DISCOVERY_TRACE_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Diagnostic only. Install at the proxy's startup boundary, outside loader
 * lock; arm once before the ordinary scene action. Hooks never write files. */
enum discovery_trace_hook {
  DISCOVERY_WORLD,
  DISCOVERY_QUEUE,
  DISCOVERY_REGISTER,
  DISCOVERY_FACTORY,
  DISCOVERY_CANDIDATES,
  DISCOVERY_ADMIT,
  DISCOVERY_INITIALIZE,
  DISCOVERY_FINALIZE,
  DISCOVERY_HOOK_COUNT
};
struct discovery_trace_status {
  DWORD version, installed, armed, world_calls, reserved, pending;
  DWORD dropped, invalid_reads, traversal_limits, stack_limits, window_complete;
};
int discovery_trace_install(HMODULE module, const char* image_path);
int discovery_trace_arm(const char* nonce);
int discovery_trace_flush(const char* path);
void discovery_trace_status(struct discovery_trace_status* status);
#endif
