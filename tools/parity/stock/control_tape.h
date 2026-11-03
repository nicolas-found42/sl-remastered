#pragma once
#include <windows.h>
/* Record only. Call outside loader lock. No thread or file I/O inside hooks.
 * Return 1 installed, 0 refused. Installed hooks live until process exit. */
int control_tape_install(HMODULE image, const char* image_path);
/* LOCAL EXPERIMENT ONLY. Trusted compile-time descriptor, host hash checked
 * before launch. Ordinary imports fixed; no main-image caller can run until
 * this attach callback returns. All selected IAT words share one 4KiB page.
 * 1 installed, 0 refused before writes, -1 fatal restore failure (abort attach).
 * Only memory/atomic operations, bounded guards/copies and saved LastError.
 * Does not hash, load a module, do file I/O, start a worker or call an API clock.
 */
struct control_tape_early_plan {
  DWORD version, kind_mask, image_size, preferred_base, entry_rva, iat_page_rva;
  DWORD slot_rva[5]; void* resolved_import[5];
  DWORD prologue_rva, prologue_size; BYTE prologue[16];
  BYTE expected_file_sha256[32];
  DWORD metadata_count;
  struct { const char* name; DWORD rva, size; } metadata[8];
  /* Optional exact Stock CRT text mutations, declared in format 5. Zero keeps
   * the format-4 IAT-only installation. Requires CONTROL_TAPE_RNG at build. */
  DWORD rng_observer;
  /* Version 2 only; version 1 descriptors end before this field. Optional
   * return-only currentTime observer; format 9. Mutually exclusive
   * with RNG/input until their combined producer is independently qualified. */
  DWORD native_clock_observer;
};
int control_tape_early_bootstrap(HMODULE image, const struct control_tape_early_plan* plan);
/* Exact Stock descriptor; only called by the optional imported proxy attach. */
int control_tape_bootstrap_stock(void);
/* Must run OUTSIDE attach. Until exact full-file SHA succeeds, flush/stop
 * refuses to create any apparently complete tape. Failure stays fatal-to-proof. */
int control_tape_confirm_file(const char* image_path);
void control_tape_early_status(DWORD out[8]);
/* One external consumer only; call periodically and immediately before shutdown. */
int control_tape_flush(const char* path);
/* Stop accepting observations, wait boundedly for API returns, flush. A stop
 * failure or any dropped/pending observation invalidates a complete tape. */
int control_tape_stop(const char* path);
/* Opt-in main-image application boundary, immediately before the verified CRT
 * ExitProcess call after its callbacks. Admission ends atomically at close;
 * new roots during drain and DLL detach are excluded. This is not the last
 * physical consumer on every other thread. Outside loader lock. Original
 * exit/code always forwarded, including failed drain.
 * Both artifacts must be absent at qualification start. */
int control_tape_shutdown_arm(HMODULE image,const char* tape,const char* report);
#ifdef CONTROL_TAPE_TEST
void* control_tape_test_hook(unsigned kind, void* original);
void control_tape_test_alignment(DWORD out[6]);
/* Explicit helper-only synthetic image for exercising production guards. */
void control_tape_test_guard_image(void* image, DWORD size);
void control_tape_test_writer(DWORD delay,int fail_once);
#ifdef CONTROL_TAPE_NATIVE_CLOCK
void* control_tape_test_native_clock_hook(void* resume,DWORD base);
#endif
#ifdef CONTROL_TAPE_RNG
/* Owned instruction/ABI qualification only; never used by a Stock producer. */
void* control_tape_test_rng_hook(unsigned kind);
#endif
#endif
