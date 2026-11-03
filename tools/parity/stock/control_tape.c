/* Private experiment: transparent record-only Win32 consumers in the verified
 * Stock PE32 main-image IAT. No fixed clock, RNG, input override or scheduler.
 * GCC i686 stubs preserve the original callee's complete integer/flags/FX state.
 * A return-address shim observes the output after stdcall argument cleanup.
 * Exceptions/unwind through the API are unsupported: do not promote as-is.
 */
#define WIN32_LEAN_AND_MEAN
#include "control_tape.h"
#include <wincrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef CONTROL_TAPE_TEST
#include "control_writer_test.inc"
#else
#define writer_flush fflush
#endif

#define CAPACITY 65536u
#define THREADS 128u
#define DEPTH 16u
enum { QPC, QPF, TICK, ASYNC, TIME, KINDS };
static const char* names[KINDS+2] = {"QueryPerformanceCounter", "QueryPerformanceFrequency", "GetTickCount", "GetAsyncKeyState", "timeGetTime", "CRT_srand_after_original_TLS_getter", "CRT_rand_after_original_TLS_getter"};
struct event {
  volatile LONG state; /* 0 free, 1 entered, 2 completed (release), 3 flushing */
  DWORD sequence, returned, native_thread, lane, lane_read, kind, caller, arg;
  DWORD entry_phase, return_phase;
  DWORD result, error_before, error_after, output[2], output_valid, gap;
  DWORD rng_tls, rng_before, rng_after, rng_flags[2];
  WORD cw_before, cw_after;
  DWORD mxcsr_before, mxcsr_after;
};
struct frame { DWORD caller; struct event* event; };
struct lane {
  volatile LONG id;
  DWORD reads, depth;
  DWORD rng_tls;
  struct frame frame[DEPTH];
};
static struct event events[CAPACITY];
static struct lane lanes[THREADS];
static volatile LONG next_sequence, next_return, drops, active, accepting, gap_count;
/* One CAS gate reserves the whole admitted observation, not just its entry.
 * Closing excludes new roots while a recorded parent may finish nested calls.
 * Numeric TID reuse remains unqualified; this preserves existing lane semantics.
 */
#define OBSERVATION_CLOSED 0x80000000u
static volatile LONG observation_gate, observation_refused;
static int observation_enter(void) {
  DWORD id=GetCurrentThreadId(),n=(DWORD)InterlockedCompareExchange(&observation_gate,0,0);
  unsigned i;int nested=0;
  for(i=0;i<THREADS;++i) if((DWORD)lanes[i].id==id && lanes[i].depth) {nested=1;break;}
  for(i=0;i<64;++i) {
    if((n&OBSERVATION_CLOSED) && (!nested || n==OBSERVATION_CLOSED)) return 0;
    if((n&~OBSERVATION_CLOSED)==OBSERVATION_CLOSED-1) break;
    DWORD old=(DWORD)InterlockedCompareExchange(&observation_gate,(LONG)(n+1),(LONG)n);
    if(old==n) {InterlockedIncrement(&active);return 1;} n=old;
  }
  InterlockedExchange(&observation_refused,1);return 0;
}
static void observation_leave(void) {
  InterlockedDecrement(&active);InterlockedDecrement(&observation_gate);
}
/* One atomic observation order across both hook phases. This is NOT an OS
 * scheduler trace: instructions before/after each increment remain unobserved. */
static volatile LONG next_phase;
static DWORD image_base, image_size;
static int installed;
static int early_install, late_identity_verified, installation_flushed;
static int rng_installed;
static DWORD initial_count = 8;
static BYTE early_digest[32];
#ifdef CONTROL_TAPE_OWNED_EARLY
#include "owned_plan.h"
#endif
struct initial_global { const char* name; DWORD rva, size, valid, words[30]; };
static struct initial_global initial[] = {
  {"engine_frequency",0x36a280,8,0,{0}},
  {"engine_qpc_epoch",0x36a288,8,0,{0}},
  {"engine_last_counter",0x36a290,8,0,{0}},
  {"stopwatch_frequency",0x36a298,8,0,{0}},
  {"stopwatch_slots_0_to_14",0x36a3a0,120,0,{0}},
  {"input_frequency",0x36f9a8,8,0,{0}},
  {"input_previous_counter",0x377440,8,0,{0}},
  {"frame_delta_float_bits",0x23c534,4,0,{0}}
};
static int initial_captured, initial_flushed;
static void* real_qpc __attribute__((used));
static void* real_qpf __attribute__((used));
static void* real_tick __attribute__((used));
static void* real_async __attribute__((used));
static void* real_time __attribute__((used));
static void return_stub(void);

/* Read/write TEB LastError without introducing another API consumer. */
#ifdef CONTROL_TAPE_TEST
static int test_bypass_guards;
static volatile LONG align_counts[6] __attribute__((used));
/* These wrappers measure C-entry ESP before any compiler prologue, then exercise
 * an aligned SSE spill at ESP-28. Bad-layout controls count the defect safely. */
#define ALIGN_CHECK(OFFSET, BAD, RESIDUE, IMPL) \
  __asm__ volatile("movl %esp,%eax\n\tandl $15,%eax\n\tmovl %eax,_align_counts+" #RESIDUE "\n\tlock incl _align_counts+" #OFFSET "\n\tcmpl $12,%eax\n\tjne 1f\n\tsubl $28,%esp\n\tmovaps %xmm7,(%esp)\n\tmovaps (%esp),%xmm7\n\taddl $28,%esp\n\tjmp _" #IMPL "\n\t1: lock incl _align_counts+" #BAD "\n\tjmp _" #IMPL "\n\t");
__attribute__((naked, used)) static void tape_enter(void) { ALIGN_CHECK(0,4,16,tape_enter_impl); }
__attribute__((naked, used)) static void tape_return(void) { ALIGN_CHECK(8,12,20,tape_return_impl); }
#else
#define tape_enter_impl tape_enter
#define tape_return_impl tape_return
#endif

#ifdef CONTROL_TAPE_TEST
/* Make the helper sensitive to missing FX restoration in either stub. */
static DWORD dirty_mxcsr __attribute__((used)) = 0x3f80;
static void dirty_fx(void) {
  __asm__ volatile("fninit\n\tfldpi\n\tldmxcsr _dirty_mxcsr\n\tpxor %%xmm7,%%xmm7" ::: "memory", "xmm7");
}
#else
#define dirty_fx() ((void)0)
#endif
static DWORD error_get(void) { DWORD v; __asm__ volatile("movl %%fs:0x34,%0" : "=r"(v)); return v; }
static void error_set(DWORD v) { __asm__ volatile("movl %0,%%fs:0x34" :: "r"(v) : "memory"); }

static int writable(DWORD address, DWORD size) {
  MEMORY_BASIC_INFORMATION info;
  DWORD end;
  if (!size || address > 0xffffffffu - size) return 0;
  end = address + size;
  if (VirtualQuery((void*)(uintptr_t)address, &info, sizeof info) != sizeof info || info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD)) return 0;
  if (!(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return 0;
  return (uintptr_t)info.BaseAddress <= address && (uint64_t)(uintptr_t)info.BaseAddress + info.RegionSize >= end;
}
/* Narrow allowlist from actual Stock call sites + saved all.c/disassembly.
 * Unknown callers/pointer shapes emit gaps without rewriting the return slot.
 * This is a guard for these observed calls, NOT generic SEH transparency. */
static DWORD guard_call(DWORD kind, DWORD caller, DWORD arg, const DWORD* stack) {
  DWORD rva, expected = 0;
  if (caller < image_base || caller - image_base >= image_size) return 1;
  rva = caller - image_base;
#ifdef CONTROL_TAPE_OWNED_EARLY
  if (early_install) {
    if (kind == QPF && rva == OWN_RETURN_QPF && arg == image_base + OWN_FREQUENCY)
      return writable(arg,8) ? 0 : 3;
    if (kind == QPC && rva == OWN_RETURN_QPC && arg == image_base + OWN_COUNTER)
      return writable(arg,8) ? 0 : 3;
    return 1; /* Explicit owned callsites only; never a general bypass. */
  }
#endif
  if (kind == QPF) {
    if (rva == 0x15169b || rva == 0x1516cb) expected = 0x36a280;
    else if (rva == 0x15231b) expected = 0x36a298;
    else if (rva == 0x156531 || rva == 0x156bcf) expected = 0x36f9a8;
    else return 1;
  } else if (kind == QPC) {
    if (rva == 0x1516a6) expected = 0x36a288;
    else if (rva == 0x1516d6) expected = 0x36a290;
    else if (rva == 0x15653c) expected = 0x377440;
    else if (rva == 0x15232d) {
      /* Literal timer indices in saved all.c; unfamiliar indices remain gaps. */
      static const DWORD slots[] = {0,1,2,3,4,7,8,11,14};
      unsigned i;
      for (i = 0; i < sizeof slots / sizeof slots[0]; ++i)
        if (arg == image_base + 0x36a3a0 + slots[i] * 8) expected = 0x36a3a0 + slots[i] * 8;
      if (!expected) return 2;
    } else if (rva == 0x15233e || rva == 0x156bda) {
      DWORD limit, base;
      __asm__ volatile("movl %%fs:8,%0\n\tmovl %%fs:4,%1" : "=r"(limit), "=r"(base));
      /* Disassembly: stopwatch local at entry ESP+8; input local at ESP+12. */
      if (arg != (DWORD)(uintptr_t)(stack + (rva == 0x15233e ? 2 : 3)) || arg < limit || arg > base || base - arg < 8) return 2;
      return writable(arg, 8) ? 0 : 3;
    } else return 1;
  } else if (kind == TICK) {
    /* FUN_00554ea0 initializes both worker ticks at return 00554eb5.
     * FUN_00550be0 selects playlist index from GetTickCount via the tail-call
     * thunk 0055c440, preserving return 00550c46 (all.c252016,257094). */
    return rva == 0x154eb5 || rva == 0x150c46 || rva == 0x155278 || rva == 0x1552bc || rva == 0x1d4da1 ? 0 : 1;
  } else if (kind == ASYNC) {
    return rva == 0x15c592 || rva == 0x15c5f4 ? 0 : 1;
  } else if (kind == TIME) {
    return rva == 0x1d150d || rva == 0x1d17c1 || rva == 0x1d17d9 ? 0 : 1;
  } else return 1;
  if (arg != image_base + expected) return 2;
  return writable(arg, 8) ? 0 : 3;
}
static struct lane* get_lane(void) {
  DWORD id = GetCurrentThreadId();
  unsigned i;
  for (i = 0; i < THREADS; ++i) {
    LONG old = InterlockedCompareExchange(&lanes[i].id, (LONG)id, 0);
    if (!old || (DWORD)old == id) return &lanes[i];
  }
  return NULL;
}
static void drop(void) {
  LONG n;
  do { n = InterlockedCompareExchange(&drops, 0, 0); if (n == 0x7fffffffL) return; }
  while (InterlockedCompareExchange(&drops, n + 1, n) != n);
}
#ifdef CONTROL_TAPE_INPUT
#include "input_observer.inc"
#endif
/* Called only with all original CPU state saved. Only completion is published. */
__attribute__((used, noinline)) static void tape_enter_impl(DWORD* regs, const BYTE* fx, DWORD kind) {
  DWORD error = error_get();
  DWORD* stack = (DWORD*)(uintptr_t)(regs[3] + 4);
  struct lane* t;
  struct event* e;
  LONG seq;
  if (!observation_enter()) { dirty_fx(); error_set(error); return; }
  t=get_lane();
  if (!t || t->depth == DEPTH) { drop(); goto done; }
  seq = InterlockedIncrement(&next_sequence);
  /* Sequence exhaustion invalidates this recording, never wrap to valid data. */
  if (seq <= 0) { drop(); goto done; }
  e = &events[((DWORD)seq - 1u) % CAPACITY];
  if (InterlockedCompareExchange(&e->state, 1, 0) != 0) { drop(); goto done; }
  e->entry_phase = (DWORD)InterlockedIncrement(&next_phase);
  e->return_phase = 0;
  if ((LONG)e->entry_phase <= 0) drop();
  e->sequence = (DWORD)seq;
  e->native_thread = (DWORD)t->id;
  e->lane = (DWORD)(t - lanes);
  e->lane_read = ++t->reads;
  e->kind = kind;
  e->caller = stack[0];
  e->arg = (kind == QPC || kind == QPF || kind == ASYNC) ? stack[1] : 0;
  e->error_before = error;
  e->output_valid = 0;
  e->output[0] = e->output[1] = 0;
  e->result = e->returned = e->error_after = 0;
  e->cw_after = 0;
  e->mxcsr_after = 0;
  e->gap = guard_call(kind, e->caller, e->arg, stack);
#ifdef CONTROL_TAPE_TEST
  if (test_bypass_guards) e->gap = 0;
#endif
  memcpy(&e->cw_before, fx, 2);
  memcpy(&e->mxcsr_before, fx + 24, 4);
  if (e->gap) {
    InterlockedIncrement(&gap_count);
    InterlockedExchange(&e->state, 2);
    goto done;
  }
  t->frame[t->depth].caller = stack[0];
  t->frame[t->depth++].event = e;
  stack[0] = (DWORD)(uintptr_t)return_stub;
  dirty_fx();
  error_set(error);
  return;
done:
  observation_leave();
  dirty_fx();
  error_set(error);
}
__attribute__((used, noinline)) static DWORD tape_return_impl(DWORD* regs, const BYTE* fx) {
  DWORD error = error_get(), caller;
  struct lane* t = get_lane();
  struct event* e;
  if (!t || !t->depth) TerminateProcess(GetCurrentProcess(), 0xe7a90001u);
  --t->depth;
  caller = t->frame[t->depth].caller;
  e = t->frame[t->depth].event;
  e->return_phase = (DWORD)InterlockedIncrement(&next_phase);
  if ((LONG)e->return_phase <= 0) drop();
  e->result = regs[7];
  e->error_after = error;
  if ((e->kind == QPC || e->kind == QPF) && e->result && e->arg) {
    /* These API contracts write exactly one LARGE_INTEGER on success. */
    memcpy(e->output, (void*)(uintptr_t)e->arg, 8);
    e->output_valid = 1;
  }
  memcpy(&e->cw_after, fx, 2);
  memcpy(&e->mxcsr_after, fx + 24, 4);
#ifdef CONTROL_TAPE_INPUT
  if(e->kind>=INPUT_STATE) input_complete(e);
#endif
  e->returned = (DWORD)InterlockedIncrement(&next_return);
  InterlockedExchange(&e->state, 2);
  observation_leave();
  dirty_fx();
  error_set(error);
  return caller;
}

#define ENTRY_STUB(NAME, KIND, REAL) \
__attribute__((naked)) static void NAME(void) { \
  __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tmovl %esp,%eax\n\tsubl $4,%esp\n\tpushl $" #KIND "\n\tpushl %eax\n\tpushl %ebx\n\tcall _tape_enter\n\taddl $16,%esp\n\tfxrstor (%esp)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tjmp *_" #REAL "\n\t"); \
}
ENTRY_STUB(qpc_stub, 0, real_qpc)
ENTRY_STUB(qpf_stub, 1, real_qpf)
ENTRY_STUB(tick_stub, 2, real_tick)
ENTRY_STUB(async_stub, 3, real_async)
ENTRY_STUB(time_stub, 4, real_time)
__attribute__((naked)) static void return_stub(void) {
  __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tmovl %esp,%eax\n\tsubl $8,%esp\n\tpushl %eax\n\tpushl %ebx\n\tcall _tape_return\n\taddl $16,%esp\n\tfxrstor (%esp)\n\tmovl %eax,-4(%ebx)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tpushl -40(%esp)\n\tret\n\t");
}

#ifdef CONTROL_TAPE_RNG
#include "rng_observer.inc"
#endif
#ifdef CONTROL_TAPE_NATIVE_CLOCK
#include "native_clock_observer.inc"
#endif

static const BYTE stock_expected[32] = {0x7d,0x77,0x46,0x6a,0x8c,0x1a,0x34,0xd1,0x6a,0x59,0xd9,0xcc,0xf2,0xe8,0x05,0x96,0x64,0xdf,0xf1,0xbf,0xb4,0x21,0x33,0x80,0x74,0xa4,0x61,0xa9,0x32,0xe8,0xed,0xde};
static int verify_file(const char* path,const BYTE expected[32]) {
  HCRYPTPROV provider = 0;
  HCRYPTHASH hash = 0;
  HANDLE f = INVALID_HANDLE_VALUE;
  BYTE buf[16384], digest[32];
  DWORD got=0, n = sizeof digest;
  int ok = 0, stage=1;
  f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) goto done;
  stage=2; if(!CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) goto done;
  stage=3; if(!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) goto done;
  stage=4;
  do { if (!ReadFile(f, buf, sizeof buf, &got, NULL) || (got && !CryptHashData(hash, buf, got, 0))) goto done; } while (got);
  stage=5;
  if (CryptGetHashParam(hash, HP_HASHVAL, digest, &n, 0) && n == sizeof digest) {stage=6; if(!memcmp(digest, expected, sizeof digest)) {ok=1;stage=7;} }
done:
  /* File/provider diagnostics run only in this outside-attach identity phase. */
  fprintf(stderr,"late_identity path=%s stage=%d error=%lu ok=%d digest0=%02x expected0=%02x\n",path,stage,GetLastError(),ok,stage>=6?digest[0]:0,expected[0]);
  if (hash) CryptDestroyHash(hash);
  if (provider) CryptReleaseContext(provider, 0);
  if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
  return ok;
}
static int early_span(DWORD rva,DWORD size,DWORD limit) {
  return rva < limit && size <= limit-rva;
}
/* The MinGW always-inline intrinsic inherits the translation unit's SSE target.
 * Keep its integer XCHG in a separate non-inline function so the bootstrap's
 * no-SSE target does not acquire compiler-generated XMM array-copy code. */
__attribute__((noinline)) static LONG early_exchange(volatile LONG* word,LONG value) {
  return InterlockedExchange(word,value);
}
__attribute__((noinline,target("no-sse,no-sse2")))
int control_tape_early_bootstrap(HMODULE image,const struct control_tape_early_plan* plan) {
  DWORD error=error_get(), old=0, ignored=0, i, j, native_clock=0;
  BYTE* base=(BYTE*)image;
  IMAGE_DOS_HEADER* dos; IMAGE_NT_HEADERS32* nt;
  MEMORY_BASIC_INFORMATION page_info;
  void* hooks[KINDS]={qpc_stub,qpf_stub,tick_stub,async_stub,time_stub};
  void** originals[KINDS]={&real_qpc,&real_qpf,&real_tick,&real_async,&real_time};
  int result=0;
  if(installed || !base || !plan || (plan->version!=1 && plan->version!=2) || !plan->kind_mask || (plan->kind_mask & ~31u) || plan->metadata_count>8 || plan->rng_observer>1) goto done;
  /* Version 1 ends at rng_observer. Never read a new trailing field from an
   * old descriptor; the optional text mutation requires explicit version 2. */
  if(plan->version==2) {
    native_clock=plan->native_clock_observer;
    if(native_clock!=1 || plan->rng_observer) goto done;
  }
#if !defined(CONTROL_TAPE_NATIVE_CLOCK) || defined(CONTROL_TAPE_INPUT)
  if(native_clock) goto done;
#endif
  dos=(IMAGE_DOS_HEADER*)base;
  if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>4096) goto done;
  nt=(IMAGE_NT_HEADERS32*)(base+dos->e_lfanew);
  if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt->OptionalHeader.SizeOfImage!=plan->image_size || nt->OptionalHeader.ImageBase!=plan->preferred_base || nt->OptionalHeader.AddressOfEntryPoint!=plan->entry_rva || nt->OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT || nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress) goto done;
#ifdef CONTROL_TAPE_OWNED_EARLY
  /* The owned standard-CRT child has exact inspected MinGW TLS callbacks.
   * This exception is not compiled into the Stock guard. Any clock call from
   * such a callback would still hit the exact caller guards and become a gap. */
  if(nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress!=OWN_TLS_RVA || nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size!=OWN_TLS_SIZE) goto done;
#else
  if(nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress) goto done;
#endif
  if((plan->iat_page_rva & 4095u) || !early_span(plan->iat_page_rva,4096,plan->image_size) || !plan->prologue_size || plan->prologue_size>16 || !early_span(plan->prologue_rva,plan->prologue_size,plan->image_size)) goto done;
  for(i=0;i<plan->prologue_size;++i) if(base[plan->prologue_rva+i]!=plan->prologue[i]) goto done;
  if(VirtualQuery(base+plan->iat_page_rva,&page_info,sizeof(page_info))!=sizeof(page_info) || page_info.State!=MEM_COMMIT || (page_info.Protect&PAGE_GUARD) || (BYTE*)page_info.BaseAddress>base+plan->iat_page_rva || (uintptr_t)page_info.BaseAddress+page_info.RegionSize<(uintptr_t)base+plan->iat_page_rva+4096) goto done;
  for(i=0;i<KINDS;++i) if(plan->kind_mask & (1u<<i)) {
    DWORD rva=plan->slot_rva[i];
    if((rva & 3) || rva<plan->iat_page_rva || rva-plan->iat_page_rva>4092 || !plan->resolved_import[i] || *(void**)(base+rva)!=plan->resolved_import[i]) goto done;
    if(rva<nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress || !early_span(rva-nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress,4,nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size)) goto done;
    for(j=0;j<i;++j) if((plan->kind_mask&(1u<<j)) && rva==plan->slot_rva[j]) goto done;
  }
  for(i=0;i<plan->metadata_count;++i) {
    if(!plan->metadata[i].name || !plan->metadata[i].size || plan->metadata[i].size>120 || (plan->metadata[i].size&3) || !early_span(plan->metadata[i].rva,plan->metadata[i].size,plan->image_size) || !writable((DWORD)(uintptr_t)base+plan->metadata[i].rva,plan->metadata[i].size)) goto done;
  }
#ifdef CONTROL_TAPE_RNG
  if(plan->rng_observer && !rng_prepare(base,plan->image_size)) goto done;
#else
  if(plan->rng_observer) goto done;
#endif
#ifdef CONTROL_TAPE_NATIVE_CLOCK
  if(native_clock && !native_clock_prepare(base,plan->image_size)) goto done;
#endif
  /* Quiescent main-image IAT: metadata copies and pointer setup are complete
   * before any selected slot is reachable by the executable's startup. */
  image_base=(DWORD)(uintptr_t)base; image_size=plan->image_size;
  for(i=0;i<KINDS;++i) if(plan->kind_mask&(1u<<i)) *originals[i]=plan->resolved_import[i];
  for(i=0;i<32;++i) early_digest[i]=plan->expected_file_sha256[i];
  initial_count=plan->metadata_count;
  for(i=0;i<initial_count;++i) {
    struct initial_global* g=&initial[i];
    g->name=plan->metadata[i].name; g->rva=plan->metadata[i].rva; g->size=plan->metadata[i].size;
    for(j=0;j<g->size/4;++j) g->words[j]=((const volatile DWORD*)(base+g->rva))[j];
    g->valid=1;
  }
#ifdef CONTROL_TAPE_RNG
  /* Acquire both page protections before changing either imports or code. */
  if(plan->rng_observer && !rng_make_writable(base)) goto done;
#endif
#ifdef CONTROL_TAPE_NATIVE_CLOCK
  if(native_clock && !native_clock_make_writable(base)) goto done;
#endif
  if(!VirtualProtect(base+plan->iat_page_rva,4096,PAGE_READWRITE,&old)) {
#ifdef CONTROL_TAPE_RNG
    if(plan->rng_observer && !rng_restore_protection(base)) result=-1;
#endif
#ifdef CONTROL_TAPE_NATIVE_CLOCK
    if(native_clock && !native_clock_restore_protection(base)) result=-1;
#endif
    goto done;
  }
#ifdef CONTROL_TAPE_RNG
  if(plan->rng_observer) rng_publish(base);
#endif
#ifdef CONTROL_TAPE_NATIVE_CLOCK
  if(native_clock) native_clock_publish(base);
#endif
  for(i=0;i<KINDS;++i) if(plan->kind_mask&(1u<<i)) early_exchange((volatile LONG*)(base+plan->slot_rva[i]),(LONG)(uintptr_t)hooks[i]);
#ifdef CONTROL_TAPE_RNG
  if(plan->rng_observer && !rng_finish_publication(base)) {
    /* Attach must abort after any unsafe code publication/protection failure. */
    result=-1; goto done;
  }
#endif
#ifdef CONTROL_TAPE_NATIVE_CLOCK
  if(native_clock && !native_clock_finish_publication(base)) {result=-1;goto done;}
#endif
  if(!VirtualProtect(base+plan->iat_page_rva,4096,old,&ignored)) {
    /* Never continue the process or fall back to a later start after this. */
    for(i=0;i<KINDS;++i) if(plan->kind_mask&(1u<<i)) early_exchange((volatile LONG*)(base+plan->slot_rva[i]),(LONG)(uintptr_t)plan->resolved_import[i]);
    VirtualProtect(base+plan->iat_page_rva,4096,old,&ignored);
    result=-1; goto done;
  }
  early_install=1; late_identity_verified=0; initial_captured=1; installed=1;
  rng_installed=plan->rng_observer!=0;
#ifdef CONTROL_TAPE_NATIVE_CLOCK
  native_clock_installed=native_clock!=0;
#endif
  early_exchange(&accepting,1); /* readiness is the final publication */
  result=1;
done:
  error_set(error); return result;
}
int control_tape_confirm_file(const char* path) {
  DWORD error=error_get(); int ok=0;
  if(installed && early_install && path && verify_file(path,early_digest)) { late_identity_verified=1; ok=1; }
  error_set(error); return ok;
}
void control_tape_early_status(DWORD out[8]) {
  out[0]=(DWORD)installed; out[1]=(DWORD)early_install; out[2]=(DWORD)late_identity_verified;
  out[3]=(DWORD)next_sequence; out[4]=(DWORD)next_return; out[5]=(DWORD)next_phase;
  out[6]=(DWORD)gap_count; out[7]=(DWORD)drops;
}
int control_tape_install(HMODULE image, const char* image_path) {
  static const DWORD rvas[KINDS] = {0x1e7048,0x1e7050,0x1e7058,0x1e725c,0x1e72cc};
  static const char* dlls[KINDS] = {"kernel32.dll","kernel32.dll","kernel32.dll","user32.dll","winmm.dll"};
  void* hooks[KINDS] = {qpc_stub,qpf_stub,tick_stub,async_stub,time_stub};
  void** originals[KINDS] = {&real_qpc,&real_qpf,&real_tick,&real_async,&real_time};
  BYTE* base = (BYTE*)image;
  IMAGE_DOS_HEADER* dos;
  IMAGE_NT_HEADERS32* nt;
  DWORD old[KINDS], ignored;
  unsigned i, writable_pages = 0;
  if (installed || !base || !image_path || !verify_file(image_path, stock_expected)) return 0;
  dos = (IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return 0;
  nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt->OptionalHeader.SizeOfImage != 0x3c0000 || nt->OptionalHeader.ImageBase != 0x400000) return 0;
  for (i = 0; i < KINDS; ++i) {
    HMODULE dll = GetModuleHandleA(dlls[i]);
    void* expected = dll ? (void*)GetProcAddress(dll, names[i]) : NULL;
    void* current = *(void**)(base + rvas[i]);
    if (!expected || current != expected) return 0;
    *originals[i] = current;
  }
  /* Obtain all protections before mutation; refuse an unexpected import slot. */
  for (i = 0; i < KINDS; ++i) {
    if (!VirtualProtect(base + rvas[i], 4, PAGE_READWRITE, &old[i])) goto restore;
    ++writable_pages;
  }
  image_base = (DWORD)(uintptr_t)base;
  image_size = nt->OptionalHeader.SizeOfImage;
  /* Capture already-existing epochs/frequencies; never obtain fresh API time. */
  for (i = 0; i < sizeof initial / sizeof initial[0]; ++i) {
    SIZE_T got = 0;
    struct initial_global* g = &initial[i];
    if (g->rva + g->size <= image_size && writable(image_base + g->rva, g->size) &&
        ReadProcessMemory(GetCurrentProcess(), base + g->rva, g->words, g->size, &got) && got == g->size) g->valid = 1;
  }
  initial_captured = 1;
  InterlockedExchange(&accepting, 1);
  for (i = 0; i < KINDS; ++i) InterlockedExchange((volatile LONG*)(base + rvas[i]), (LONG)(uintptr_t)hooks[i]);
  installed = 1;
restore:
  while (writable_pages) { --writable_pages; VirtualProtect(base + rvas[writable_pages], 4, old[writable_pages], &ignored); }
  return installed;
}
static volatile LONG tape_writer;
/* One writer owns this buffer until fclose; slots stay reserved until a
 * successful commit. MinGW's tiny default buffer/per-row fflush kept the
 * command worker exporting long enough to miss its existing stop deadline. */
static char tape_io_buffer[128*1024];
#define EVENT_COMMIT_BATCH 128u
static int commit_events(FILE* f,struct event** batch,unsigned* used) {
  unsigned i;int ok;
  if(!*used) return 1;
  ok=!ferror(f) && writer_flush(f)==0;
  for(i=0;i<*used;++i) InterlockedExchange(&batch[i]->state,ok?0:2);
  *used=0;return ok;
}
static int control_tape_flush_body(const char* path) {
  DWORD error = error_get();
  FILE* f;
  unsigned i, count = 0, pending = 0;
  struct event* batch[EVENT_COMMIT_BATCH];unsigned used=0;
  if(early_install && !late_identity_verified) { error_set(error); return -2; }
  if (!installed || !(f = fopen(path, "ab"))) { error_set(error); return -1; }
  if(setvbuf(f,tape_io_buffer,_IOFBF,sizeof tape_io_buffer)) {fclose(f);error_set(error);return -1;}
  if(early_install && !installation_flushed) {
    fputs("{\"event\":\"installation\",\"boundary\":\"dll_process_attach_main_iat\",\"identity\":\"late_full_file_sha256_verified\",\"initial_observations\":\"not_restorable_checkpoint\",\"file_sha256\":\"",f);
    for(i=0;i<32;++i) fprintf(f,"%02x",early_digest[i]);
#ifdef CONTROL_TAPE_OWNED_EARLY
    fputs("\",\"scope\":\"owned_console_only\"}\n",f);
#else
#ifdef CONTROL_TAPE_NATIVE_CLOCK
    if(native_clock_installed) fputs("\",\"scope\":\"stock_main_image_api_native_clock_returns\",\"native_boundary\":\"after_original_native_bridge_fstp32\",\"code_mutations\":[{\"rva\":131220,\"bytes\":7}]}\n",f);
    else
#endif
    if(rng_installed) fputs("\",\"scope\":\"stock_main_image_api_rng_interval\",\"rng_boundary\":\"after_original_TLS_getter\",\"code_mutations\":[{\"rva\":1930240,\"bytes\":7},{\"rva\":1930253,\"bytes\":9}]}\n",f);
    else fputs("\",\"scope\":\"stock_main_image_api_interval\"}\n",f);
#endif
    if(ferror(f) || fflush(f)) { fclose(f); error_set(error); return -1; }
    installation_flushed=1;
  }
  if (initial_captured && !initial_flushed) {
    unsigned j;
    for (i = 0; i < initial_count; ++i) {
      struct initial_global* g = &initial[i];
      fprintf(f,"{\"event\":\"initial_global\",\"name\":\"%s\",\"image_base\":%lu,\"rva\":%lu,\"size\":%lu,\"valid\":%lu,\"words\":[",g->name,image_base,g->rva,g->size,g->valid);
      for (j = 0; j < g->size / 4; ++j) fprintf(f,"%s%lu",j ? "," : "",g->words[j]);
      fputs("]}\n",f);
    }
    if (ferror(f) || fflush(f)) { fclose(f); error_set(error); return -1; }
    initial_flushed = 1;
  }
  for (i = 0; i < CAPACITY; ++i) {
    struct event* e = &events[i];
    if (InterlockedCompareExchange(&e->state, 3, 2) != 2) continue;
    fprintf(f, "{\"event\":\"%s\",\"gap_reason\":%lu,\"seq\":%lu,\"return_seq\":%lu,\"entry_phase\":%lu,\"return_phase\":%lu,\"thread\":%lu,\"lane\":%lu,\"lane_read\":%lu,\"role\":\"unresolved\",\"api\":\"%s\",\"caller\":%lu,\"caller_rva\":%lu,\"arg\":%lu,\"result\":%lu,\"last_error_before\":%lu,\"last_error_after\":%lu,\"output_valid\":%lu,\"output_u32\":[%lu,%lu],\"cw\":[%u,%u],\"mxcsr\":[%lu,%lu]",
      e->gap ? "gap" : (e->kind>=KINDS ? "rng_return" : "api_return"),e->gap,e->sequence,e->returned,e->entry_phase,e->return_phase,e->native_thread,e->lane,e->lane_read,names[e->kind],e->caller,
      e->caller >= image_base && e->caller - image_base < image_size ? e->caller - image_base : 0xffffffffu,
      e->arg,e->result,e->error_before,e->error_after,e->output_valid,e->output[0],e->output[1],e->cw_before,e->cw_after,e->mxcsr_before,e->mxcsr_after);
    if(e->kind>=KINDS) fprintf(f,",\"rng_tls_raw\":%lu,\"rng_before\":%lu,\"rng_after\":%lu,\"eflags\":[%lu,%lu]",e->rng_tls,e->rng_before,e->rng_after,e->rng_flags[0],e->rng_flags[1]);
    fputs("}\n",f);
    batch[used++]=e;
    if(ferror(f) || used==EVENT_COMMIT_BATCH) {
      unsigned committed=used;
      if(!commit_events(f,batch,&used)) {fclose(f);error_set(error);return -1;}
      count+=committed;
    }
  }
  {unsigned committed=used;
   if(!commit_events(f,batch,&used)) {fclose(f);error_set(error);return -1;}
   count+=committed;}
  for (i = 0; i < CAPACITY; ++i) if (InterlockedCompareExchange(&events[i].state, 1, 1) == 1) ++pending;
#ifdef CONTROL_TAPE_NATIVE_CLOCK
  if(native_clock_installed) {
    if(!native_clock_flush(f)) {fclose(f);error_set(error);return -1;}
    for(i=0;i<NATIVE_CLOCK_CAPACITY;++i) if(InterlockedCompareExchange(&native_clock_records[i].state,1,1)==1) ++pending;
    fprintf(f,"{\"event\":\"native_clock_status\",\"returns\":%ld,\"phase_source\":\"shared_next_phase\",\"scope\":\"return_only_not_replay\"}\n",native_clock_returns);
  }
#endif
#ifdef CONTROL_TAPE_INPUT
  if(!input_flush(f)) {fclose(f);error_set(error);return -1;}
  for(i=0;i<INPUT_CAPACITY;++i) if(InterlockedCompareExchange(&input_records[i].call.state,1,1)==1) ++pending;
  fprintf(f,"{\"event\":\"input_observer_status\",\"version\":1,\"scope\":\""
#ifdef CONTROL_TAPE_INPUT_STOCK
          "experimental_stock_consumption"
#else
          "owned_console_only"
#endif
          "\",\"input_entered\":%ld,\"input_returned\":%ld,\"phase_source_raw\":%lu,\"device_registrations\":%lu,\"vtables\":%lu}\n",
          input_entered,input_returned,(DWORD)(uintptr_t)&next_phase,input_device_count,input_vtable_count);
#endif
  fprintf(f,"{\"event\":\"status\",\"format_version\":%d,\"entered\":%ld,\"returned\":%ld,\"phases\":%ld,\"active\":%ld,\"pending\":%u,\"dropped\":%ld,\"gaps\":%ld,\"accepting\":%ld,\"mode\":\"record_only\",\"early_startup\":\"%s\"}\n",
#ifdef CONTROL_TAPE_INPUT
      7,
#else
#ifdef CONTROL_TAPE_NATIVE_CLOCK
      native_clock_installed?9:
#endif
      rng_installed?5:(early_install?4:3),
#endif
      next_sequence,next_return,next_phase,active,pending,drops,gap_count,accepting,early_install?"main_iat_from_proxy_attach":"unobserved");
  if (fclose(f)) count = (unsigned)-1;
  error_set(error);
  return (int)count;
}
int control_tape_flush(const char* path) {
  DWORD error=error_get();int result=0;
  /* Periodic publication may race the final application boundary. It must
   * neither overlap its writer nor append a later status after final close. */
  if(!((DWORD)InterlockedCompareExchange(&observation_gate,0,0)&OBSERVATION_CLOSED) &&
     !InterlockedCompareExchange(&tape_writer,1,0)) {
    if(!((DWORD)InterlockedCompareExchange(&observation_gate,0,0)&OBSERVATION_CLOSED))
      result=control_tape_flush_body(path);
    InterlockedExchange(&tape_writer,0);
  }
  error_set(error);return result;
}
int control_tape_stop(const char* path) {
  DWORD error = error_get();
  unsigned i;
  int result = -1;
  InterlockedOr(&observation_gate,(LONG)OBSERVATION_CLOSED);
  InterlockedExchange(&accepting, 0);
  for (i = 0; i < 1000; ++i) {
    if ((DWORD)InterlockedCompareExchange(&observation_gate,0,0)==OBSERVATION_CLOSED &&
        !InterlockedCompareExchange(&tape_writer,1,0)) {
      if(InterlockedCompareExchange(&observation_refused,0,0)) InterlockedIncrement(&drops);
      result = control_tape_flush_body(path);
      InterlockedExchange(&tape_writer,0);break;
    }
    Sleep(1);
  }
  error_set(error);
  return result;
}
static void* shutdown_target __attribute__((used));
static char shutdown_tape[MAX_PATH],shutdown_report[MAX_PATH];
static DWORD shutdown_image;
static int shutdown_arm_ok;
static int shutdown_artifact_absent(const char* path) {
  DWORD attrs=GetFileAttributesA(path),error=GetLastError();
  return attrs==INVALID_FILE_ATTRIBUTES && (error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND);
}
__attribute__((used,noinline)) static void shutdown_observe(DWORD* regs) {
  DWORD error=error_get();DWORD* stack=(DWORD*)(uintptr_t)(regs[3]+4);
  int guarded=shutdown_arm_ok && stack[0]==shutdown_image+0x1d796f,result=-3;
  FILE* f;
  if(guarded) result=control_tape_stop(shutdown_tape);
  f=fopen(shutdown_report,"wb");
  if(f) {
    fprintf(f,"{\"schema\":1,\"boundary\":\"main_image_before_ExitProcess_after_CRT_callbacks\",\"dll_detach_observed\":false,\"caller\":%lu,\"exit_code\":%lu,\"guarded\":%s,\"stop_result\":%d,\"active\":%ld,\"gate\":%lu,\"dropped\":%ld,\"gaps\":%ld,\"entered\":%ld,\"returned\":%ld}\n",
      stack[0],stack[1],guarded?"true":"false",result,active,(DWORD)observation_gate,drops,gap_count,next_sequence,next_return);
    fclose(f);
  }
  error_set(error);
}
__attribute__((naked)) static void shutdown_stub(void) {
  __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tsubl $12,%esp\n\tpushl %ebx\n\tcall _shutdown_observe\n\taddl $16,%esp\n\tfxrstor (%esp)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tjmp *_shutdown_target\n\t");
}
int control_tape_shutdown_arm(HMODULE module,const char* tape,const char* report) {
  DWORD error=error_get(),old,ignored;int result=0;BYTE* base=(BYTE*)module;
  void** slot;void* expected;
  if(!installed || !accepting || shutdown_target || !base || !tape || !report ||
     (early_install && !late_identity_verified) || strlen(tape)>=MAX_PATH || strlen(report)>=MAX_PATH ||
     (DWORD)(uintptr_t)base!=image_base || image_size!=0x3c0000)goto done;
  if(!shutdown_artifact_absent(tape) || !shutdown_artifact_absent(report))goto done;
  /* Pinned Stock import RVA and original FF15 call at005d7969, return005d796f.
   * Full file identity was already required by install/confirm_file. */
  slot=(void**)(base+0x1e71f0);
  expected=(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"),"ExitProcess");
  if(!expected || *slot!=expected || base[0x1d7969]!=0xff || base[0x1d796a]!=0x15 ||
     *(DWORD*)(base+0x1d796b)!=(DWORD)(uintptr_t)slot)goto done;
  if(!VirtualProtect(slot,4,PAGE_READWRITE,&old))goto done;
  strcpy(shutdown_tape,tape);strcpy(shutdown_report,report);shutdown_image=image_base;
  shutdown_target=expected;
  InterlockedExchange((volatile LONG*)slot,(LONG)(uintptr_t)shutdown_stub);
  result=VirtualProtect(slot,4,old,&ignored)?1:-1;
  shutdown_arm_ok=result==1;
done:
  error_set(error);return result;
}
#ifdef CONTROL_TAPE_TEST
#ifdef CONTROL_TAPE_NATIVE_CLOCK
void* control_tape_test_native_clock_hook(void* resume,DWORD base) {
  installed=1;accepting=1;native_clock_installed=1;
  native_clock_resume=resume;image_base=base;
  return (void*)native_clock_stub;
}
#endif
void* control_tape_test_hook(unsigned kind, void* original) {
  void* hooks[KINDS] = {qpc_stub,qpf_stub,tick_stub,async_stub,time_stub};
  void** originals[KINDS] = {&real_qpc,&real_qpf,&real_tick,&real_async,&real_time};
  if (kind >= KINDS) return NULL;
  *originals[kind] = original;
  installed = 1;
  accepting = 1;
  test_bypass_guards = 1;
  return hooks[kind];
}
void control_tape_test_alignment(DWORD out[6]) {
  unsigned i;
  for (i = 0; i < 6; ++i) out[i] = (DWORD)align_counts[i];
}
void control_tape_test_guard_image(void* image, DWORD size) {
  image_base = (DWORD)(uintptr_t)image;
  image_size = size;
  test_bypass_guards = 0;
}
#endif
