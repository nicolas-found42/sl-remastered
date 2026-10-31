/* Opt-in diagnostic for the verified 32-bit stock walk at RVA 0xae9c0.
 * The hooks only write bounded memory; walk_trace_flush does all file I/O.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_RECORDS 512
#define MAX_THREADS 64
#define MAX_DEPTH 32
#define WALK_RVA 0xae9c0u

struct record {
  volatile LONG state; /* 0 unused, 1 entered, 2 complete, 3 flushed */
  LONG sequence;
  DWORD thread, depth, caller, args[9];
  unsigned char pose[2][0xd4], core[2][28];
  DWORD cache_before[2], cache_after[2];
  float output[3][3];
  unsigned char fx_entry[512], fx_return[512];
  unsigned valid_entry, valid_return;
};
struct frame {
  DWORD address;
  struct record* record;
};
struct thread_state {
  volatile LONG id;
  unsigned depth;
  struct frame frame[MAX_DEPTH];
};
static struct record records[MAX_RECORDS];
static struct thread_state threads[MAX_THREADS];
static volatile LONG next_record, dropped;
static void* walk_trampoline __attribute__((used));
static int installed;

static void walk_entry_stub(void);
static void walk_return_stub(void);

static int copy_memory(void* dst, const void* src, SIZE_T size) {
  SIZE_T got = 0;
  return src && ReadProcessMemory(GetCurrentProcess(), src, dst, size, &got) && got == size;
}
static struct thread_state* thread_state(DWORD id) {
  unsigned i;
  for (i = 0; i < MAX_THREADS; ++i) {
    LONG seen = InterlockedCompareExchange(&threads[i].id, (LONG)id, 0);
    if (seen == 0 || (DWORD)seen == id) return &threads[i];
  }
  return NULL;
}

static void count_drop(void) {
  LONG old;
  do {
    old = InterlockedCompareExchange(&dropped, 0, 0);
    if (old == 0x7fffffffL) return;
  } while (InterlockedCompareExchange(&dropped, old + 1, old) != old);
}

/* regs are the pushal frame; regs[3]+4 is the original stack pointer. */
__attribute__((used, noinline)) static void trace_enter(uint32_t* regs, const unsigned char* fx) {
  DWORD error = GetLastError(), id = GetCurrentThreadId();
  struct thread_state* t = thread_state(id);
  struct record* r;
  uint32_t* stack = (uint32_t*)(uintptr_t)(regs[3] + 4);
  LONG index;
  unsigned i;
  if (!t || t->depth == MAX_DEPTH) {
    count_drop();
    goto done;
  }
  do {
    index = InterlockedCompareExchange(&next_record, 0, 0);
    if (index >= MAX_RECORDS) {
      count_drop();
      goto done;
    }
  } while (InterlockedCompareExchange(&next_record, index + 1, index) != index);
  r = &records[index];
  r->state = 1;
  r->sequence = index + 1;
  r->thread = id;
  r->depth = t->depth;
  r->caller = stack[0];
  memcpy(r->args, stack + 1, sizeof r->args);
  memcpy(r->fx_entry, fx, sizeof r->fx_entry);
  for (i = 0; i < 2; ++i) {
    uintptr_t shape = r->args[2 + i];
    if (copy_memory(r->pose[i], (void*)(uintptr_t)r->args[i], sizeof r->pose[i])) r->valid_entry |= 1u << i;
    if (shape <= UINT32_MAX - 0x20 && copy_memory(r->core[i], (void*)(shape + 0x20), sizeof r->core[i])) r->valid_entry |= 1u << (2 + i);
    if (copy_memory(&r->cache_before[i], (void*)(uintptr_t)r->args[4 + i], 4)) r->valid_entry |= 1u << (4 + i);
  }
  t->frame[t->depth].address = stack[0];
  t->frame[t->depth].record = r;
  ++t->depth;
  stack[0] = (DWORD)(uintptr_t)walk_return_stub;
done:
  SetLastError(error);
}

__attribute__((used, noinline)) static DWORD trace_return(uint32_t* regs, const unsigned char* fx) {
  DWORD error = GetLastError(), result;
  struct thread_state* t = thread_state(GetCurrentThreadId());
  struct record* r;
  unsigned i;
  (void)regs;
  /* Only frames installed by trace_enter reach this stub. */
  if (!t || !t->depth) TerminateProcess(GetCurrentProcess(), 0xeae90001u);
  --t->depth;
  result = t->frame[t->depth].address;
  r = t->frame[t->depth].record;
  memcpy(r->fx_return, fx, sizeof r->fx_return);
  for (i = 0; i < 2; ++i)
    if (copy_memory(&r->cache_after[i], (void*)(uintptr_t)r->args[4 + i], 4)) r->valid_return |= 1u << i;
  for (i = 0; i < 3; ++i)
    if (copy_memory(r->output[i], (void*)(uintptr_t)r->args[6 + i], sizeof r->output[i])) r->valid_return |= 1u << (2 + i);
  InterlockedExchange(&r->state, 2);
  SetLastError(error);
  return result;
}

__attribute__((naked)) static void walk_entry_stub(void) {
  __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tpushl %esp\n\tpushl %ebx\n\tcall _trace_enter\n\taddl $8,%esp\n\tfxrstor (%esp)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tjmp *_walk_trampoline\n\t");
}
__attribute__((naked)) static void walk_return_stub(void) {
  __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tpushl %esp\n\tpushl %ebx\n\tcall _trace_return\n\taddl $8,%esp\n\tfxrstor (%esp)\n\tmovl %eax,-4(%ebx)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tpushl -40(%esp)\n\tret\n\t");
}

static int verify_file(const char* path) {
  static const unsigned char expected[32] = {0x7d, 0x77, 0x46, 0x6a, 0x8c, 0x1a, 0x34, 0xd1, 0x6a, 0x59, 0xd9, 0xcc, 0xf2, 0xe8, 0x05, 0x96, 0x64, 0xdf, 0xf1, 0xbf, 0xb4, 0x21, 0x33, 0x80, 0x74, 0xa4, 0x61, 0xa9, 0x32, 0xe8, 0xed, 0xde};
  HCRYPTPROV provider = 0;
  HCRYPTHASH hash = 0;
  HANDLE f = INVALID_HANDLE_VALUE;
  unsigned char buf[16384], digest[32];
  DWORD got, n = sizeof digest;
  int ok = 0;
  f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE || !CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) || !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) goto done;
  do {
    if (!ReadFile(f, buf, sizeof buf, &got, NULL) || (got && !CryptHashData(hash, buf, got, 0))) goto done;
  } while (got);
  if (CryptGetHashParam(hash, HP_HASHVAL, digest, &n, 0) && n == sizeof digest && memcmp(digest, expected, sizeof digest) == 0) ok = 1;
done:
  if (hash) CryptDestroyHash(hash);
  if (provider) CryptReleaseContext(provider, 0);
  if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
  return ok;
}
static void jump(unsigned char* p, const void* to) {
  int32_t offset = (int32_t)((uintptr_t)to - ((uintptr_t)p + 5));
  p[0] = 0xe9;
  memcpy(p + 1, &offset, sizeof offset);
}

/* Call outside loader lock, before any thread can execute the target. */
int walk_trace_install(HMODULE module, const char* image_path) {
  static const unsigned char prologue[8] = {0x8b, 0x4c, 0x24, 0x08, 0x8b, 0x44, 0x24, 0x04};
  unsigned char *base = (unsigned char*)module, *target, *trampoline;
  IMAGE_DOS_HEADER* dos;
  IMAGE_NT_HEADERS32* nt;
  DWORD old, ignored;
  if (installed || !base || !image_path || !verify_file(image_path)) return 0;
  dos = (IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return 0;
  nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt->OptionalHeader.SizeOfImage < WALK_RVA + 8) return 0;
  target = base + WALK_RVA;
  if (memcmp(target, prologue, sizeof prologue)) return 0;
  trampoline = VirtualAlloc(NULL, 13, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (!trampoline) return 0;
  memcpy(trampoline, prologue, 8);
  jump(trampoline + 8, target + 8);
  if (!VirtualProtect(target, 8, PAGE_EXECUTE_READWRITE, &old)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return 0;
  }
  walk_trampoline = trampoline;
  jump(target, walk_entry_stub);
  memset(target + 5, 0x90, 3);
  FlushInstructionCache(GetCurrentProcess(), target, 8);
  VirtualProtect(target, 8, old, &ignored);
  installed = 1;
  return 1;
}

static void hex(FILE* f, const void* data, size_t n) {
  const unsigned char* p = data;
  size_t i;
  for (i = 0; i < n; ++i) fprintf(f, "%02x", p[i]);
}
/* Called from the proxy's Present thread, never from a walk hook. */
int walk_trace_flush(const char* path) {
  FILE* f;
  unsigned i, j, pending = 0;
  int count = 0;
  if (!installed || !(f = fopen(path, "ab"))) return -1;
  for (i = 0; i < MAX_RECORDS; ++i) {
    struct record* r = &records[i];
    if (InterlockedCompareExchange(&r->state, 2, 2) != 2) continue;
    fprintf(f, "{\"seq\":%ld,\"thread\":%lu,\"depth\":%lu,\"caller\":%lu,\"args\":[", r->sequence, r->thread, r->depth, r->caller);
    for (j = 0; j < 9; ++j) fprintf(f, "%s%lu", j ? "," : "", r->args[j]);
    fprintf(f, "],\"valid_entry\":%u,\"valid_return\":%u,\"pose_a\":\"", r->valid_entry, r->valid_return);
    hex(f, r->pose[0], sizeof r->pose[0]);
    fputs("\",\"pose_b\":\"", f);
    hex(f, r->pose[1], sizeof r->pose[1]);
    fputs("\",\"core_a\":\"", f);
    hex(f, r->core[0], sizeof r->core[0]);
    fputs("\",\"core_b\":\"", f);
    hex(f, r->core[1], sizeof r->core[1]);
    fputs("\",\"cache_before\":\"", f);
    hex(f, r->cache_before, sizeof r->cache_before);
    fputs("\",\"cache_after\":\"", f);
    hex(f, r->cache_after, sizeof r->cache_after);
    fputs("\",\"output\":\"", f);
    hex(f, r->output, sizeof r->output);
    fputs("\",\"fx_entry\":\"", f);
    hex(f, r->fx_entry, sizeof r->fx_entry);
    fputs("\",\"fx_return\":\"", f);
    hex(f, r->fx_return, sizeof r->fx_return);
    fprintf(f, "\",\"dropped_so_far\":%ld}\n", InterlockedCompareExchange(&dropped, 0, 0));
    if (ferror(f) || fflush(f)) {
      fclose(f);
      return -1;
    }
    InterlockedExchange(&r->state, 3);
    ++count;
  }
  for (i = 0; i < MAX_RECORDS; ++i)
    if (InterlockedCompareExchange(&records[i].state, 1, 1) == 1) ++pending;
  fprintf(f, "{\"event\":\"status\",\"reserved\":%ld,\"pending\":%u,\"dropped\":%ld}\n",
          InterlockedCompareExchange(&next_record, 0, 0), pending, InterlockedCompareExchange(&dropped, 0, 0));
  if (fclose(f)) return -1;
  return count;
}
