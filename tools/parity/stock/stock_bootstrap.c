/* FUN_005514c0 / verified Stock PE32 import layout. No file I/O, allocation,
 * clock call, module loading or worker creation while the loader lock is held.
 * The host verifies the exact private file first; full in-process file SHA is
 * confirmed outside attach before any tape can be flushed. */
#define WIN32_LEAN_AND_MEAN
#include "control_tape.h"
#include <mmsystem.h>
#include <stdint.h>

static struct control_tape_early_plan plan = {
#ifdef SLRR_STOCK_NATIVE_CLOCK_OBSERVER
  .version=2,
#else
  .version=1,
#endif
  .kind_mask=31, .image_size=0x3c0000, .preferred_base=0x400000,
  .entry_rva=0x1514c0, .iat_page_rva=0x1e7000,
  .slot_rva={0x1e7048,0x1e7050,0x1e7058,0x1e725c,0x1e72cc},
  .prologue_rva=0x1514c0, .prologue_size=16,
  .prologue={0xa1,0x00,0x70,0x60,0x00,0x85,0xc0,0x75,0x4d,0xa1,0x1c,0x72,0x60,0x00,0x85,0xc0},
  .expected_file_sha256={0x7d,0x77,0x46,0x6a,0x8c,0x1a,0x34,0xd1,0x6a,0x59,0xd9,0xcc,0xf2,0xe8,0x05,0x96,0x64,0xdf,0xf1,0xbf,0xb4,0x21,0x33,0x80,0x74,0xa4,0x61,0xa9,0x32,0xe8,0xed,0xde},
#ifdef SLRR_STOCK_RNG_OBSERVER
  .rng_observer=1,
#endif
#ifdef SLRR_STOCK_NATIVE_CLOCK_OBSERVER
  .native_clock_observer=1,
#endif
  .metadata_count=8,
  .metadata={{"engine_frequency",0x36a280,8}, {"engine_qpc_epoch",0x36a288,8},
             {"engine_last_counter",0x36a290,8}, {"stopwatch_frequency",0x36a298,8},
             {"stopwatch_slots_0_to_14",0x36a3a0,120}, {"input_frequency",0x36f9a8,8},
             {"input_previous_counter",0x377440,8}, {"frame_delta_float_bits",0x23c534,4}}
};

__attribute__((noinline,target("no-sse,no-sse2")))
int control_tape_bootstrap_stock(void) {
  DWORD error;
  int result=0;
  __asm__ volatile("movl %%fs:0x34,%0" : "=r"(error));
  HMODULE image=GetModuleHandleA(NULL);
  /* The prologue contains absolute operands. Relocated instances are outside
   * this qualified descriptor and must refuse before a single IAT write. */
  if((uintptr_t)image!=plan.preferred_base) goto done;
  plan.resolved_import[0]=(void*)QueryPerformanceCounter;
  plan.resolved_import[1]=(void*)QueryPerformanceFrequency;
  plan.resolved_import[2]=(void*)GetTickCount;
  plan.resolved_import[3]=(void*)GetAsyncKeyState;
  plan.resolved_import[4]=(void*)timeGetTime;
  result=control_tape_early_bootstrap(image,&plan);
done:
  __asm__ volatile("movl %0,%%fs:0x34" :: "r"(error) : "memory");
  return result;
}
