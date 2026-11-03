/* Bounded observation of the original resource/factory/contact path. No
 * replacement discovery, forced wake, geometry or state writes are performed.
 * Raw prefixes retain their original offsets; semantic decoding is separate.
 */
#include "discovery_trace.h"
#include <wincrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RECORDS 32768
#define THREADS 16
#define DEPTH 32
#define NODES 256
#define WORLD_CALLS 120
#define VERTICES 4096
#define PAYLOAD 1024
#define OUTPUT_LIMIT (96u * 1024u * 1024u)

enum { BODY = 8,
       METADATA,
       VERTEX_CHUNK,
       QUEUED_RESOURCE,
       CANDIDATE,
       RETAINED,
       WORLD_SNAPSHOT };
struct record {
  volatile LONG state;
  DWORD sequence, order, return_order, thread, depth, world, kind, phase, address, ecx, caller;
  DWORD args[12], result, valid, bytes;
  unsigned char payload[PAYLOAD];
};
struct frame {
  DWORD return_address, previous_world;
  struct record* record;
};
struct metadata {
  DWORD body;
  unsigned char bytes[0xa0 + 0xac];
};
struct thread_state {
  volatile LONG id;
  DWORD depth, world, metadata_count;
  struct frame frames[DEPTH];
  struct metadata metadata[NODES];
};
struct plan {
  DWORD rva, count, arguments;
  unsigned char prefix[12];
};
static const struct plan plans[DISCOVERY_HOOK_COUNT] = {
    {0xa5190, 5, 1, {0xd9, 0x44, 0x24, 0x04, 0x56}},
    {0xa2540, 6, 0, {0x83, 0xec, 0x6c, 0x8b, 0x51, 0x30}},
    {0xb3c40, 6, 1, {0x51, 0x53, 0x8b, 0x5c, 0x24, 0x0c}},
    {0xb4250, 9, 0, {0x55, 0x8b, 0xec, 0x81, 0xec, 0x8c, 0, 0, 0}},
    {0x9ec00, 9, 1, {0x55, 0x8b, 0xec, 0x81, 0xec, 0x50, 0x01, 0, 0}},
    {0x9b830, 7, 12, {0x83, 0xec, 0x08, 0x8b, 0x54, 0x24, 0x10}},
    {0x9b0b0, 7, 4, {0x51, 0x53, 0x55, 0x8b, 0x6c, 0x24, 0x10}},
    {0x9a050, 6, 1, {0x81, 0xec, 0xcc, 0, 0, 0}}};
static struct record records[RECORDS];
static struct thread_state threads[THREADS];
static volatile LONG reserved, dropped, invalid_reads, traversal_limits;
static volatile LONG stack_limits, world_calls, armed, window_complete;
static volatile LONG event_order;
static unsigned char* image;
static int installed, header_written;
static DWORD output_bytes;
static char arm_nonce[33];
static void* trampolines[DISCOVERY_HOOK_COUNT] __attribute__((used));
static void discovery_return_stub(void);

static void increment(volatile LONG* value) {
  LONG old;
  do {
    old = InterlockedCompareExchange(value, 0, 0);
    if (old == 0x7fffffffL) return;
  } while (InterlockedCompareExchange(value, old + 1, old) != old);
}
static int read_memory(void* out, DWORD address, SIZE_T bytes) {
  SIZE_T got = 0;
  if (address && bytes <= UINT32_MAX && address <= UINT32_MAX - (DWORD)bytes &&
      ReadProcessMemory(GetCurrentProcess(), (void*)(uintptr_t)address, out, bytes, &got) && got == bytes) return 1;
  increment(&invalid_reads);
  return 0;
}
static DWORD word(const void* p, unsigned offset) {
  DWORD result;
  memcpy(&result, (const unsigned char*)p + offset, 4);
  return result;
}
static struct thread_state* thread_state(DWORD id) {
  unsigned i;
  for (i = 0; i < THREADS; ++i) {
    LONG old = InterlockedCompareExchange(&threads[i].id, (LONG)id, 0);
    if (old == 0 || (DWORD)old == id) return &threads[i];
  }
  increment(&stack_limits);
  return NULL;
}
static struct record* reserve(struct thread_state* t, DWORD kind, DWORD phase, DWORD address) {
  LONG index;
  struct record* r;
  do {
    index = InterlockedCompareExchange(&reserved, 0, 0);
    if (index >= RECORDS) {
      increment(&dropped);
      return NULL;
    }
  } while (InterlockedCompareExchange(&reserved, index + 1, index) != index);
  r = &records[index];
  r->state = 1;
  r->sequence = (DWORD)index + 1;
  r->order = (DWORD)InterlockedIncrement(&event_order);
  r->thread = (DWORD)t->id;
  r->depth = t->depth;
  r->world = t->world;
  r->kind = kind;
  r->phase = phase;
  r->address = address;
  return r;
}
static void prefix_record(struct thread_state* t, DWORD kind, DWORD phase, DWORD address, DWORD bytes) {
  struct record* r = reserve(t, kind, phase, address);
  if (!r) return;
  r->bytes = bytes;
  r->valid = read_memory(r->payload, address, bytes);
  InterlockedExchange(&r->state, 2);
}
static void body_record(struct thread_state* t, DWORD address, DWORD phase) {
  unsigned char raw[0x218], meta[0xa0 + 0xac];
  DWORD prototype, shape, i;
  struct record* r = reserve(t, BODY, phase, address);
  if (!r) return;
  r->bytes = sizeof raw;
  r->valid = read_memory(raw, address, sizeof raw);
  if (r->valid) memcpy(r->payload, raw, sizeof raw);
  InterlockedExchange(&r->state, 2);
  if (!r->valid) return;
  prototype = word(raw, 0x5c);
  if (!read_memory(meta, prototype, 0xa0)) return;
  shape = word(meta, 0x78);
  if (!read_memory(meta + 0xa0, shape, 0xac)) return;
  for (i = 0; i < t->metadata_count; ++i)
    if (t->metadata[i].body == address) break;
  if (i < t->metadata_count && !memcmp(t->metadata[i].bytes, meta, sizeof meta)) return;
  if (i == NODES) {
    increment(&traversal_limits);
    return;
  }
  if (i == t->metadata_count) ++t->metadata_count;
  t->metadata[i].body = address;
  memcpy(t->metadata[i].bytes, meta, sizeof meta);
  r = reserve(t, METADATA, phase, address);
  if (!r) return;
  r->ecx = prototype;
  r->args[0] = shape;
  r->bytes = sizeof meta;
  r->valid = 1;
  memcpy(r->payload, meta, sizeof meta);
  InterlockedExchange(&r->state, 2);
  if (word(meta + 0xa0, 8) == 6 || word(meta + 0xa0, 8) == 7) {
    DWORD count = word(meta + 0xa0, 0xa0), vertices = word(meta + 0xa0, 0xa4), offset = 0;
    if (count > VERTICES) {
      increment(&traversal_limits);
      return;
    }
    while (offset < count) {
      DWORD n = count - offset;
      if (n > PAYLOAD / 12) n = PAYLOAD / 12;
      r = reserve(t, VERTEX_CHUNK, phase, address);
      if (!r) return;
      r->ecx = shape;
      r->args[0] = offset;
      r->args[1] = count;
      r->bytes = n * 12;
      if (vertices > UINT32_MAX - offset * 12) {
        increment(&invalid_reads);
        r->valid = 0;
      } else
        r->valid = read_memory(r->payload, vertices + offset * 12, r->bytes);
      InterlockedExchange(&r->state, 2);
      offset += n;
    }
  }
}
static void list_records(struct thread_state* t, DWORD kind, DWORD phase, DWORD head, DWORD bytes) {
  DWORD visited[NODES], count = 0, current = head;
  while (current) {
    DWORD links[3], i;
    if (!read_memory(links, current, sizeof links)) return;
    if (!links[1]) return; /* original intrusive list's terminal sentinel */
    for (i = 0; i < count; ++i)
      if (visited[i] == current) {
        increment(&traversal_limits);
        return;
      }
    if (count == NODES) {
      increment(&traversal_limits);
      return;
    }
    visited[count++] = current;
    prefix_record(t, kind, phase, current, bytes);
    current = links[1];
  }
}
static void world_records(struct thread_state* t, DWORD world, DWORD phase) {
  DWORD root, head, pending[NODES], seen[NODES], read = 0, write = 0, count = 0;
  prefix_record(t, WORLD_SNAPSHOT, phase, world, 0x134);
  if (!read_memory(&root, world + 0x84, 4) || !root) return;
  if (!read_memory(&head, root + 0x24, 4)) return;
  if (head) pending[write++] = head;
  while (read < write) {
    DWORD body = pending[read++];
    while (body) {
      DWORD links[3], i;
      for (i = 0; i < count; ++i)
        if (seen[i] == body) {
          increment(&traversal_limits);
          return;
        }
      if (count == NODES) {
        increment(&traversal_limits);
        return;
      }
      seen[count++] = body;
      body_record(t, body, phase);
      if (!read_memory(links, body + 0x20, sizeof links)) return;
      if (links[1]) {
        if (write == NODES) {
          increment(&traversal_limits);
          return;
        }
        pending[write++] = links[1];
      }
      body = links[2];
    }
  }
  if (read_memory(&head, world + 0x118, 4)) list_records(t, RETAINED, phase, head, 0xb0);
}

/* pushal registers: EDI,ESI,EBP,original ESP,EBX,EDX,ECX,EAX.
 * regs[3]+4 points at the original return address, below our saved EFLAGS. */
__attribute__((used, noinline)) static void discovery_enter(DWORD hook, DWORD* regs) {
  DWORD error = GetLastError(), stack[13] = {0}, id = GetCurrentThreadId();
  struct thread_state* t;
  struct record* r;
  DWORD original_stack = regs[3] + 4;
  if (InterlockedCompareExchange(&armed, 0, 0) != 1 || InterlockedCompareExchange(&window_complete, 0, 0)) goto done;
  t = thread_state(id);
  if (!t) goto done;
  if (t->depth == DEPTH) {
    increment(&stack_limits);
    goto done;
  }
  if (!read_memory(stack, original_stack, (plans[hook].arguments + 1) * 4)) goto done;
  if (hook == DISCOVERY_WORLD) {
    LONG n = InterlockedIncrement(&world_calls);
    if (n > WORLD_CALLS) {
      InterlockedExchange(&window_complete, 1);
      goto done;
    }
  }
  r = reserve(t, hook, 0, (DWORD)(uintptr_t)image + plans[hook].rva);
  if (!r) goto done;
  r->ecx = regs[6];
  r->caller = stack[0];
  memcpy(r->args, stack + 1, plans[hook].arguments * 4);
  r->valid = 1;
  t->frames[t->depth].return_address = stack[0];
  t->frames[t->depth].record = r;
  t->frames[t->depth].previous_world = t->world;
  if (hook == DISCOVERY_WORLD) t->world = (DWORD)world_calls;
  r->world = t->world;
  ++t->depth;
  if (hook == DISCOVERY_WORLD) world_records(t, r->ecx, 0);
  if (hook == DISCOVERY_QUEUE) {
    DWORD head;
    if (read_memory(&head, (DWORD)(uintptr_t)image + 0x243738, 4)) list_records(t, QUEUED_RESOURCE, 0, head, 0x1c);
  }
  if (hook == DISCOVERY_CANDIDATES) {
    DWORD head;
    if (read_memory(&head, (DWORD)(uintptr_t)image + 0x243808, 4)) list_records(t, CANDIDATE, 0, head, 0x58);
  }
  *(DWORD*)(uintptr_t)original_stack = (DWORD)(uintptr_t)discovery_return_stub;
done:
#ifdef DISCOVERY_TRACE_TEST
  /* The immutable missing-restoration build must detect this disturbance. */
  __asm__ volatile("fninit\n\tfld1\n\tpxor %xmm0,%xmm0\n\t");
#endif
  SetLastError(error);
}
__attribute__((used, noinline)) static DWORD discovery_leave(DWORD* regs) {
  DWORD error = GetLastError(), address;
  struct thread_state* t = thread_state(GetCurrentThreadId());
  struct record* r;
  if (!t || !t->depth) TerminateProcess(GetCurrentProcess(), 0xeada0001u);
  --t->depth;
  r = t->frames[t->depth].record;
  address = t->frames[t->depth].return_address;
  r->result = regs[7];
  if (r->kind == DISCOVERY_WORLD) world_records(t, r->ecx, 1);
  if (r->kind == DISCOVERY_INITIALIZE) body_record(t, r->args[0], 2);
  if (r->kind == DISCOVERY_FINALIZE) body_record(t, r->args[0], 3);
  if (r->kind == DISCOVERY_ADMIT && r->result) {
    r->bytes = 0xb0;
    r->valid = read_memory(r->payload, r->result, r->bytes);
  }
  r->return_order = (DWORD)InterlockedIncrement(&event_order);
  t->world = t->frames[t->depth].previous_world;
  InterlockedExchange(&r->state, 2);
  SetLastError(error);
  return address;
}
#define ENTRY(name, index, offset)                                                                                                                                                                                                                                                             \
  __attribute__((naked)) static void name(void) {                                                                                                                                                                                                                                              \
    __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tpushl %ebx\n\tpushl $" #index "\n\tcall _discovery_enter\n\taddl $8,%esp\n\tfxrstor (%esp)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tjmp *_trampolines+" #offset "\n\t"); \
  }
ENTRY(world_entry, 0, 0)
ENTRY(queue_entry, 1, 4) ENTRY(register_entry, 2, 8)
    ENTRY(factory_entry, 3, 12) ENTRY(candidate_entry, 4, 16) ENTRY(admit_entry, 5, 20)
        ENTRY(initialize_entry, 6, 24) ENTRY(finalize_entry, 7, 28)
            __attribute__((naked)) static void discovery_return_stub(void) {
  __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tpushl %ebx\n\tcall _discovery_leave\n\taddl $4,%esp\n\tfxrstor (%esp)\n\tmovl %eax,-4(%ebx)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tpushl -40(%esp)\n\tret\n\t");
}

static int verify_file(const char* path) {
  static const BYTE expected[32] = {0x7d, 0x77, 0x46, 0x6a, 0x8c, 0x1a, 0x34, 0xd1, 0x6a, 0x59, 0xd9, 0xcc, 0xf2, 0xe8, 0x05, 0x96, 0x64, 0xdf, 0xf1, 0xbf, 0xb4, 0x21, 0x33, 0x80, 0x74, 0xa4, 0x61, 0xa9, 0x32, 0xe8, 0xed, 0xde};
  HCRYPTPROV provider = 0;
  HCRYPTHASH hash = 0;
  HANDLE f = INVALID_HANDLE_VALUE;
  BYTE data[16384], digest[32];
  DWORD got, n = sizeof digest;
  int ok = 0;
  f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE || !CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) || !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) goto done;
  do {
    if (!ReadFile(f, data, sizeof data, &got, NULL) || (got && !CryptHashData(hash, data, got, 0))) goto done;
  } while (got);
  ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &n, 0) && n == sizeof digest && !memcmp(digest, expected, sizeof digest);
done:
  if (hash) CryptDestroyHash(hash);
  if (provider) CryptReleaseContext(provider, 0);
  if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
  return ok;
}
static void jump(BYTE* target, const void* destination) {
  int32_t offset = (int32_t)((uintptr_t)destination - ((uintptr_t)target + 5));
  target[0] = 0xe9;
  memcpy(target + 1, &offset, 4);
}
int discovery_trace_install(HMODULE module, const char* image_path) {
  void* entries[] = {world_entry, queue_entry, register_entry, factory_entry, candidate_entry, admit_entry, initialize_entry, finalize_entry};
  IMAGE_DOS_HEADER* dos;
  IMAGE_NT_HEADERS32* pe;
  DWORD old[DISCOVERY_HOOK_COUNT], ignored;
  unsigned i, j;
  BYTE* base = (BYTE*)module;
  if (installed || !base || !image_path || !verify_file(image_path)) return 0;
  dos = (void*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return 0;
  pe = (void*)(base + dos->e_lfanew);
  if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || pe->OptionalHeader.SizeOfImage != 0x3c0000) return 0;
  for (i = 0; i < DISCOVERY_HOOK_COUNT; ++i)
    if (plans[i].count < 5 || memcmp(base + plans[i].rva, plans[i].prefix, plans[i].count)) return 0;
  for (i = 0; i < DISCOVERY_HOOK_COUNT; ++i) {
    BYTE* t = VirtualAlloc(NULL, plans[i].count + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) goto failed;
    trampolines[i] = t;
    memcpy(t, plans[i].prefix, plans[i].count);
    jump(t + plans[i].count, base + plans[i].rva + plans[i].count);
  }
  for (i = 0; i < DISCOVERY_HOOK_COUNT; ++i)
    if (!VirtualProtect(base + plans[i].rva, plans[i].count, PAGE_EXECUTE_READWRITE, &old[i])) {
      for (j = 0; j < i; ++j) VirtualProtect(base + plans[j].rva, plans[j].count, old[j], &ignored);
      goto failed;
    }
  image = base;
  for (i = 0; i < DISCOVERY_HOOK_COUNT; ++i) {
    jump(base + plans[i].rva, entries[i]);
    memset(base + plans[i].rva + 5, 0x90, plans[i].count - 5);
    FlushInstructionCache(GetCurrentProcess(), base + plans[i].rva, plans[i].count);
    VirtualProtect(base + plans[i].rva, plans[i].count, old[i], &ignored);
  }
  installed = 1;
  return 1;
failed:
  for (i = 0; i < DISCOVERY_HOOK_COUNT; ++i)
    if (trampolines[i]) {
      VirtualFree(trampolines[i], 0, MEM_RELEASE);
      trampolines[i] = NULL;
    }
  return 0;
}
int discovery_trace_arm(const char* nonce) {
  unsigned i;
  if (!installed || !nonce || strlen(nonce) != 32) return 0;
  for (i = 0; i < 32; ++i)
    if (!((nonce[i] >= '0' && nonce[i] <= '9') || (nonce[i] >= 'a' && nonce[i] <= 'f'))) return 0;
  if (InterlockedCompareExchange(&armed, -1, 0) != 0) return 0;
  memcpy(arm_nonce, nonce, 33);
  InterlockedExchange(&armed, 1);
  return 1;
}
void discovery_trace_status(struct discovery_trace_status* s) {
  unsigned i;
  memset(s, 0, sizeof *s);
  s->version = 1;
  s->installed = installed;
  s->armed = InterlockedCompareExchange(&armed, 0, 0) == 1;
  s->world_calls = (DWORD)world_calls;
  s->reserved = (DWORD)reserved;
  s->dropped = (DWORD)dropped;
  s->invalid_reads = (DWORD)invalid_reads;
  s->traversal_limits = (DWORD)traversal_limits;
  s->stack_limits = (DWORD)stack_limits;
  s->window_complete = (DWORD)window_complete;
  for (i = 0; i < RECORDS; ++i)
    if (InterlockedCompareExchange(&records[i].state, 1, 1) == 1) ++s->pending;
}
int discovery_trace_flush(const char* path) {
  static const char digits[] = "0123456789abcdef";
  FILE* f;
  unsigned i, j;
  int count = 0;
  struct discovery_trace_status status;
  char hex[PAYLOAD * 2 + 1];
  if (!installed || InterlockedCompareExchange(&armed, 0, 0) != 1) return 0;
  if (!(f = fopen(path, "ab"))) return -1;
  if (!header_written) {
    int n = fprintf(f, "{\"event\":\"discovery_header\",\"version\":1,\"nonce\":\"%s\",\"image_sha256\":\"7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde\",\"record_limit\":%u,\"node_limit\":%u,\"world_limit\":%u}\n", arm_nonce, RECORDS, NODES, WORLD_CALLS);
    if (n < 0) goto failed;
    output_bytes += (DWORD)n;
    header_written = 1;
  }
  for (i = 0; i < RECORDS; ++i) {
    struct record* r = &records[i];
    int n;
    if (InterlockedCompareExchange(&r->state, 2, 2) != 2) continue;
    if (output_bytes > OUTPUT_LIMIT - 4096) {
      increment(&dropped);
      goto failed;
    }
    n = fprintf(f, "{\"event\":\"discovery_record\",\"seq\":%lu,\"order\":%lu,\"return_order\":%lu,\"thread\":%lu,\"depth\":%lu,\"world\":%lu,\"kind\":%lu,\"phase\":%lu,\"address\":%lu,\"ecx\":%lu,\"caller\":%lu,\"result\":%lu,\"valid\":%lu,\"args\":[", r->sequence, r->order, r->return_order, r->thread, r->depth, r->world, r->kind, r->phase, r->address, r->ecx, r->caller, r->result, r->valid);
    if (n < 0) goto failed;
    output_bytes += (DWORD)n;
    for (j = 0; j < 12; ++j) {
      n = fprintf(f, "%s%lu", j ? "," : "", r->args[j]);
      if (n < 0) goto failed;
      output_bytes += (DWORD)n;
    }
    for (j = 0; j < r->bytes; ++j) {
      hex[2 * j] = digits[r->payload[j] >> 4];
      hex[2 * j + 1] = digits[r->payload[j] & 15];
    }
    hex[2 * r->bytes] = 0;
    n = fprintf(f, "],\"payload\":\"%s\"}\n", hex);
    if (n < 0) goto failed;
    output_bytes += (DWORD)n;
    InterlockedExchange(&r->state, 3);
    ++count;
  }
  discovery_trace_status(&status);
  if (fprintf(f, "{\"event\":\"discovery_status\",\"reserved\":%lu,\"pending\":%lu,\"dropped\":%lu,\"invalid_reads\":%lu,\"traversal_limits\":%lu,\"stack_limits\":%lu,\"world_calls\":%lu,\"window_complete\":%lu}\n", status.reserved, status.pending, status.dropped, status.invalid_reads, status.traversal_limits, status.stack_limits, status.world_calls, status.window_complete) < 0) goto failed;
  if (fflush(f) || fclose(f)) return -1;
  return count;
failed:
  fclose(f);
  return -1;
}
