/* Owned actual discovery entry/return stub qualification; no Stock graph.
 * Raw baseline/repeat/observed snapshots retain the backend FX image verbatim. */
#define WIN32_LEAN_AND_MEAN
#include "discovery_trace.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum { CASES = 384 };
struct snapshot {
  DWORD regs[8], flags, error, pad[2];
  BYTE fx[512];
  DWORD stored_bits;
  WORD cw, sw;
  DWORD canaries[2];
} __attribute__((aligned(16)));
static struct snapshot baseline[CASES], repeat[CASES], observed[CASES];
static WORD cw __attribute__((used));
static DWORD mxcsr __attribute__((used)), flags __attribute__((used));
static double input_value __attribute__((used));
static DWORD extra_stack __attribute__((used));
static DWORD frame_base __attribute__((used)), arena_base __attribute__((used));
static struct snapshot* snapshot_target __attribute__((used));
static DWORD behavior __attribute__((used)), return_flags __attribute__((used));
static WORD return_cw __attribute__((used));
static DWORD return_mxcsr __attribute__((used)), parameter_seed __attribute__((used));
static DWORD landed_esp __attribute__((used));
static void* callee_target __attribute__((used));

/* Callee cleanup comes from exact Stock RET instructions, not signatures.
 * Baseline and observer jump to this identical owned callee. */
__attribute__((used, naked)) static void callee_state(void) {
  __asm__ volatile(
      "cmpl $0,_behavior\n\tje 1f\n\t"
      "movl $0xa1a1a1a1,%eax\n\tmovl $0xc2c2c2c2,%ecx\n\tmovl $0xd3d3d3d3,%edx\n\t"
      "cmpl $2,_behavior\n\tjne 1f\n\t"
      "fninit\n\tfldcw _return_cw\n\tldmxcsr _return_mxcsr\n\tfld1\n\t"
      "pxor %xmm0,%xmm0\n\tpxor %xmm7,%xmm7\n\tmovl $0x55667788,%fs:0x34\n\t"
      "1: pushl _return_flags\n\tpopfl\n\tret\n\t");
}
/* CALLing the state helper uses the real stack, then each target performs
 * its independently observed cleanup. A CALL/RET pair preserves entry EFLAGS. */
__attribute__((naked)) static void callee0(void) {
  __asm__ volatile("call _callee_state\n\tret\n\t");
}
__attribute__((naked)) static void callee4(void) {
  __asm__ volatile("call _callee_state\n\tret $4\n\t");
}
__attribute__((naked)) static void callee16(void) {
  __asm__ volatile("call _callee_state\n\tret $16\n\t");
}
__attribute__((naked)) static void callee48(void) {
  __asm__ volatile("call _callee_state\n\tret $48\n\t");
}
__attribute__((naked)) static void plain(void) {
  __asm__ volatile("jmp *_callee_target\n\t");
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
__attribute__((naked)) static void capture(void* fn, struct snapshot* out) {
  __asm__ volatile(
      "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\tsubl $128,%esp\n\t"
      "movl %ebp,_frame_base\n\tmovl %esp,_arena_base\n\tmovl 12(%ebp),%edi\n\tmovl %edi,_snapshot_target\n\t"
      "movl $1f,(%esp)\n\tmovl _parameter_seed,%eax\n\tleal 4(%esp),%esi\n\tmovl $12,%ecx\n\t"
      "3: movl %eax,(%esi)\n\taddl $4,%esi\n\tincl %eax\n\tloop 3b\n\t"
      "movl $0x12345678,0x60(%esp)\n\tmovl $0x9abcdef0,0x78(%esp)\n\t"
      "fninit\n\tfldcw _cw\n\tcmpl $0,_extra_stack\n\tje 2f\n\tfldpi\n\t2: fldl _input_value\n\tfstps 0x5c(%esp)\n\t"
      "movl 0x5c(%esp),%eax\n\tmovl %eax,560(%edi)\n\tldmxcsr _mxcsr\n\t"
      "pcmpeqd %xmm0,%xmm0\n\tpcmpeqd %xmm7,%xmm7\n\tmovl $0x55aa55aa,%fs:0x34\n\t"
      "movl $0x11111111,%eax\n\tmovl $0x22222222,%ecx\n\tmovl $0x33333333,%edx\n\t"
      "movl $0x44444444,%ebx\n\tmovl $0x55555555,%esi\n\tmovl $0x66666666,%edi\n\t"
      "pushl _flags\n\tpopfl\n\tjmp *8(%ebp)\n\t"
      "1: movl %esp,_landed_esp\n\tpushfl\n\tpushal\n\tcld\n\tmovl %esp,%esi\n\tmovl _snapshot_target,%edi\n\tmovl $9,%ecx\n\trep movsl\n\t"
      "movl %fs:0x34,%eax\n\tmovl %eax,(%edi)\n\tmovl _snapshot_target,%edi\n\tfxsave 48(%edi)\n\t"
      "movl _landed_esp,%eax\n\tmovl %eax,40(%edi)\n\t"
      "movl _arena_base,%esi\n\taddl $4,%esi\n\tmovl $12,%ecx\n\tmovl $0x811c9dc5,%eax\n\t"
      "4: xorl (%esi),%eax\n\timull $16777619,%eax\n\taddl $4,%esi\n\tloop 4b\n\tmovl %eax,44(%edi)\n\t"
      "fnstcw 564(%edi)\n\tfnstsw 566(%edi)\n\tmovl _arena_base,%eax\n\tmovl 0x60(%eax),%edx\n\tmovl %edx,568(%edi)\n\t"
      "movl 0x78(%eax),%edx\n\tmovl %edx,572(%edi)\n\tmovl _frame_base,%eax\n\t"
      "lea -12(%eax),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tmovl %eax,%ebp\n\tleave\n\tret\n\t");
}
#pragma GCC diagnostic pop
static int save(const char* name, const void* bytes, size_t size) {
  FILE* f = fopen(name, "wb");
  if (!f) return 0;
  return fwrite(bytes, 1, size, f) == size && fclose(f) == 0;
}

static DWORD argument_hash(DWORD seed) {
  DWORD hash = 0x811c9dc5;
  unsigned i;
  for (i = 0; i < 12; ++i) hash = (hash ^ (seed + i)) * 16777619u;
  return hash;
}
static int expected_state(const struct snapshot* s, DWORD cleanup) {
  unsigned i;
  DWORD expected_error = behavior == 2 ? 0x55667788 : 0x55aa55aa;
  DWORD expected_eax = behavior ? 0xa1a1a1a1 : 0x11111111;
  DWORD expected_ecx = behavior ? 0xc2c2c2c2 : 0x22222222;
  DWORD expected_edx = behavior ? 0xd3d3d3d3 : 0x33333333;
  if (s->pad[0] != arena_base + 4 + cleanup || s->regs[3] != arena_base + cleanup ||
      s->pad[1] != argument_hash(parameter_seed) || s->regs[7] != expected_eax ||
      s->regs[6] != expected_ecx || s->regs[5] != expected_edx ||
      s->regs[4] != 0x44444444 || s->regs[1] != 0x55555555 || s->regs[0] != 0x66666666 ||
      s->error != expected_error || (s->flags & 0xcd5) != (return_flags & 0xcd5) ||
      s->cw != return_cw || memcmp(s->fx + 24, &return_mxcsr, 4) ||
      s->canaries[0] != 0x12345678 || s->canaries[1] != 0x9abcdef0) return 0;
  for (i = 0; i < 16; ++i)
    if (s->fx[160 + i] != (behavior == 2 ? 0 : 255) || s->fx[272 + i] != (behavior == 2 ? 0 : 255)) return 0;
  if (behavior == 2 && ((s->sw >> 11) & 7) != 7) return 0;
  return 1;
}
int main(void) {
  static const DWORD flag_values[] = {0x202, 0x203, 0x246, 0x697};
  static const DWORD cleanup_bytes[] = {4, 0, 4, 0, 0, 48, 16, 4};
  static void* callees[] = {callee4, callee0, callee4, callee0, callee0, callee48, callee16, callee4};
  unsigned hook, mode, fl, value, index = 0, failures = 0, unstable = 0, saved_failures = 0, expectation_failures = 0;
  struct discovery_trace_status status;
  if (sizeof(struct snapshot) != 576) return 20;
  for (hook = 0; hook < DISCOVERY_HOOK_COUNT; ++hook)
    for (mode = 0; mode < 4; ++mode)
      for (fl = 0; fl < 4; ++fl)
        for (value = 0; value < 3; ++value) {
          DWORD cleanup = cleanup_bytes[hook];
          void* entry;
          behavior = value;
          extra_stack = value & 1;
          cw = (WORD)(0x37f + (mode << 10));
          mxcsr = 0x1f80 + (mode << 13);
          return_cw = behavior == 2 ? (WORD)(cw ^ 0x400) : cw;
          return_mxcsr = behavior == 2 ? mxcsr ^ 0x2000 : mxcsr;
          flags = flag_values[fl];
          return_flags = flags ^ (behavior == 1 ? 1 : behavior == 2 ? 0x40
                                                                    : 0);
          input_value = behavior == 0 ? 1.0 / 3.0 : behavior == 1 ? -0.0
                                                                  : 1e40;
          parameter_seed = 0xabc00000 + index * 16;
          callee_target = callees[hook];
          entry = discovery_trace_test_hook(hook, callee_target);
          capture(plain, &baseline[index]);
          if (!expected_state(&baseline[index], cleanup)) ++expectation_failures;
          capture(plain, &repeat[index]);
          if (!expected_state(&repeat[index], cleanup)) ++expectation_failures;
          capture(entry, &observed[index]);
          if (!expected_state(&observed[index], cleanup)) ++expectation_failures;
          if (memcmp(&baseline[index], &repeat[index], sizeof(struct snapshot))) ++unstable;
          if (memcmp(&baseline[index], &observed[index], sizeof(struct snapshot))) ++failures;
          if (!discovery_trace_test_saved(cw, mxcsr, return_cw, return_mxcsr, parameter_seed)) ++saved_failures;
          ++index;
        }
  discovery_trace_status(&status);
  if (!save("Z:\\tmp\\discovery-baseline.bin", baseline, sizeof baseline) ||
      !save("Z:\\tmp\\discovery-repeat.bin", repeat, sizeof repeat) ||
      !save("Z:\\tmp\\discovery-observed.bin", observed, sizeof observed)) return 21;
  printf("{\"scope\":\"owned_actual_discovery_callee_cleanup_and_return_state\",\"cases\":%u,\"unstable\":%u,\"failures\":%u,\"saved_failures\":%u,\"expectation_failures\":%u,\"reserved\":%lu,\"pending\":%lu,\"dropped\":%lu,\"stack_limits\":%lu}\n",
         index, unstable, failures, saved_failures, expectation_failures, status.reserved, status.pending, status.dropped, status.stack_limits);
  return failures || unstable || saved_failures || expectation_failures || status.reserved != CASES ||
                 status.pending || status.dropped || status.stack_limits
             ? 1
             : 0;
}
