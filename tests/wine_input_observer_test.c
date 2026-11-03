/* Owned PE32 console qualification, no game/device/window/audio. Compile the
 * exact recorder implementation with CONTROL_TAPE_INPUT and CONTROL_TAPE_TEST.
 * Snapshot comparisons are raw ABI evidence, not actual Wine device behavior.
 */
#define WIN32_LEAN_AND_MEAN
#include "control_tape.h"
#include "input_observer.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { CASES=16, FXS=4 };
struct snapshot { DWORD regs[8], flags, error, pad[2]; BYTE fx[512]; } __attribute__((aligned(16)));
struct device { void** vtable; DWORD size; };
static void* vtables[2][11];
static struct device devices[4];
static struct snapshot baseline[FXS][CASES], hooked[FXS][CASES];
static struct snapshot unhooked_repeat[FXS][CASES];
static BYTE before[CASES][288], after[FXS][CASES][288], baseline_after[FXS][CASES][288];
static DWORD count_baseline[FXS][CASES], count_hooked[FXS][CASES];
static BYTE buffer[288];
static DWORD count, scenario, calls[3], tick_calls, nested;
static DWORD mxcsr __attribute__((used))=0x1f80;
static WORD cw __attribute__((used))=0x037f;
static DWORD target_flags __attribute__((used))=0x202, target_entry_flags __attribute__((used));
static DWORD entry_baseline[FXS][CASES], entry_hooked[FXS][CASES], entry_repeat[FXS][CASES];
typedef HRESULT (WINAPI *state_fn)(void*,DWORD,void*);
static DWORD WINAPI fake_tick(void) { ++tick_calls; SetLastError(0x12345678); return 0xfedcba98; }
static void* tick_target=(void*)fake_tick;
__attribute__((used,noinline)) static HRESULT WINAPI fake_state_body(void* self,DWORD size,void* out) {
  unsigned i; BYTE* bytes=out;
  (void)self; ++calls[0];
  if(scenario==12) ((DWORD (WINAPI*)(void))tick_target)();
  if(scenario==13 && !nested) {
    BYTE owned[16]; nested=1;
    ((state_fn)devices[1].vtable[9])(&devices[1],16,owned);
    nested=0;
  }
  if(scenario==2) {SetLastError(0xdead0002);return (HRESULT)0x8007001eu;}
  if(scenario==4) {bytes[0]=0x7e;SetLastError(0xdead0004);return (HRESULT)0x8007001eu;}
  for(i=0;i<size && i<256;++i) {
    if(scenario==3 && i==17) break;
    bytes[i]=(BYTE)(i ^ (scenario+0x30));
  }
  SetLastError(0xdead0000u+scenario);
  return scenario==3 ? (HRESULT)0x8007001eu : 0;
}
__attribute__((used,noinline)) static HRESULT WINAPI fake_acquire_body(void* self) {
  (void)self;++calls[1];SetLastError(0xdead0000u+scenario);
  return scenario==5 ? 0 : (scenario==6 ? 1 : (HRESULT)0x80070005u);
}
__attribute__((used,noinline)) static HRESULT WINAPI fake_data_body(void* self,DWORD size,void* out,DWORD* n,DWORD flags) {
  DWORD event[5]={255,0x81,0xaabbccdd,0x12345678,0xabcdef01};
  (void)self;(void)size;(void)flags;++calls[2];
  if(scenario!=8) memcpy(out,event,20);
  *n=scenario==8 || scenario==11 ? 0 : 1;
  SetLastError(0xdead0000u+scenario);
  return scenario==10 ? 1 : (scenario==11 ? (HRESULT)0x8007001eu : 0);
}

/* The first fixture's unconstrained C/import return flags differed between two
 * unhooked passes. Preserve that evidence; qualify deterministic owned targets
 * with explicit return flags instead of attributing it to the interceptor. */
__attribute__((naked)) static void fake_state(void) {
  __asm__ volatile("pushfl\n\tpopl _target_entry_flags\n\tpushl 12(%esp)\n\tpushl 12(%esp)\n\tpushl 12(%esp)\n\tcall _fake_state_body@12\n\tpushl _target_flags\n\tpopfl\n\tret $12\n\t");
}
__attribute__((naked)) static void fake_acquire(void) {
  __asm__ volatile("pushfl\n\tpopl _target_entry_flags\n\tpushl 4(%esp)\n\tcall _fake_acquire_body@4\n\tpushl _target_flags\n\tpopfl\n\tret $4\n\t");
}
__attribute__((naked)) static void fake_data(void) {
  __asm__ volatile("pushfl\n\tpopl _target_entry_flags\n\tpushl 20(%esp)\n\tpushl 20(%esp)\n\tpushl 20(%esp)\n\tpushl 20(%esp)\n\tpushl 20(%esp)\n\tcall _fake_data_body@20\n\tpushl _target_flags\n\tpopfl\n\tret $20\n\t");
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
__attribute__((naked)) static void capture(void* fn,void* self,DWORD size,void* data,DWORD* n,DWORD method,struct snapshot* out) {
  __asm__ volatile(
    "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
    "fninit\n\tfldcw _cw\n\tfldpi\n\tfldz\n\tldmxcsr _mxcsr\n\t"
    "pcmpeqd %xmm0,%xmm0\n\tpcmpeqd %xmm7,%xmm7\n\t"
    "movl $0x11111111,%eax\n\tmovd %eax,%xmm1\n\tpshufd $0,%xmm1,%xmm1\n\t"
    "movl $0x22222222,%eax\n\tmovd %eax,%xmm2\n\tpshufd $0,%xmm2,%xmm2\n\t"
    "movl $0x33333333,%eax\n\tmovd %eax,%xmm3\n\tpshufd $0,%xmm3,%xmm3\n\t"
    "movl $0x44444444,%eax\n\tmovd %eax,%xmm4\n\tpshufd $0,%xmm4,%xmm4\n\t"
    "movl $0x55555555,%eax\n\tmovd %eax,%xmm5\n\tpshufd $0,%xmm5,%xmm5\n\t"
    "movl $0x66666666,%eax\n\tmovd %eax,%xmm6\n\tpshufd $0,%xmm6,%xmm6\n\t"
    "movl $0x55aa55aa,%fs:0x34\n\t"
    "cmpl $1,28(%ebp)\n\tje 2f\n\tcmpl $2,28(%ebp)\n\tjne 1f\n\t"
    "pushl $0\n\tpushl 24(%ebp)\n\t1: pushl 20(%ebp)\n\tpushl 16(%ebp)\n\t2: pushl 12(%ebp)\n\t"
    "movl $0x11111111,%eax\n\tmovl $0x22222222,%ecx\n\tmovl $0x33333333,%edx\n\t"
    "movl $0x44444444,%ebx\n\tmovl $0x55555555,%esi\n\tmovl $0x66666666,%edi\n\t"
    "pushl _target_flags\n\tpopfl\n\tcall *8(%ebp)\n\tpushfl\n\tpushal\n\tcld\n\tmovl %esp,%esi\n\tmovl 32(%ebp),%edi\n\t"
    "movl $9,%ecx\n\trep movsl\n\tmovl %fs:0x34,%eax\n\tmovl %eax,(%edi)\n\t"
    "movl 32(%ebp),%edi\n\tfxsave 48(%edi)\n\t"
    "lea -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tleave\n\tret\n\t");
}
#pragma GCC diagnostic pop

static void save(const char* name,const void* data,size_t size) {
  FILE* f=fopen(name,"wb");if(!f || fwrite(data,1,size,f)!=size || fclose(f)) ExitProcess(20);
}
static void run_pass(int hooked_pass,int refuse) {
  unsigned f,i; struct snapshot* out;
  for(f=0;f<FXS;++f) {
    static const DWORD flag_cases[FXS]={0x202,0x203,0x246,0x297};
    cw=(WORD)(0x037f+(f<<10));mxcsr=0x1f80+(f<<13);
    target_flags=flag_cases[f];
    for(i=0;i<CASES;++i) {
      DWORD method=i>=5 && i<=7 ? 1 : (i>=8 && i<=11 ? 2 : 0);
      unsigned which=i==1 ? 1 : (i==14 ? 2 : 0);
      DWORD size=method==2 ? 20 : devices[which].size;
      void* fn=devices[which].vtable[method==0 ? 9 : (method==1 ? 7 : 10)];
      if(refuse && i==15) {which=3;size=256;}
      if(refuse && i==14) size=15;
      scenario=i;count=1;
      memset(buffer,0xa5,sizeof buffer);memset(buffer+16,0x7e,256);
      memcpy(before[i],buffer,sizeof buffer);
      out=hooked_pass ? &hooked[f][i] : &baseline[f][i];memset(out,0,sizeof *out);
      capture(fn,&devices[which],size,buffer+16,&count,method,out);
      if(hooked_pass) {memcpy(after[f][i],buffer,sizeof buffer);count_hooked[f][i]=count;entry_hooked[f][i]=target_entry_flags;}
      else {memcpy(baseline_after[f][i],buffer,sizeof buffer);count_baseline[f][i]=count;entry_baseline[f][i]=target_entry_flags;}
    }
  }
}
int main(int argc,char** argv) {
  const struct parity_input_observer_v1* api;
  unsigned i,f,failures=0,bytes=0;int refuse,stopped;
  if(argc!=2) return 2;
  refuse=!strcmp(argv[1],"refuse");
  for(i=0;i<2;++i) {vtables[i][9]=fake_state;vtables[i][7]=fake_acquire;vtables[i][10]=fake_data;}
  for(i=0;i<4;++i) {devices[i].vtable=vtables[i==2 ? 1 : 0];devices[i].size=i==1 ? 16 : 256;}
  run_pass(0,refuse);
  /* An unhooked repeat independently identifies unstable fixture output before
   * attributing a raw machine-state difference to an interception callback. */
  run_pass(1,refuse);
  memcpy(unhooked_repeat,hooked,sizeof hooked);
  save("unhooked-repeat.bin",unhooked_repeat,sizeof unhooked_repeat);
  memcpy(entry_repeat,entry_hooked,sizeof entry_repeat);
  save("entry-repeat.bin",entry_repeat,sizeof entry_repeat);
  tick_target=control_tape_test_hook(2,fake_tick);
  if(parity_input_observer(99,sizeof *api) || parity_input_observer(1,1)) return 3;
  api=parity_input_observer(PARITY_INPUT_VERSION,sizeof *api);
  if(!api || api->identity!=PARITY_INPUT_IDENTITY) return 4;
  for(i=0;i<3;++i) if(!api->register_device(&devices[i],i+1,1,i==1 ? PARITY_INPUT_MOUSE : PARITY_INPUT_KEYBOARD)) return 5;
  if(!api->retire_device(&devices[0],1,1) || !api->register_device(&devices[0],1,2,PARITY_INPUT_KEYBOARD)) return 6;
  run_pass(1,refuse);
  stopped=control_tape_stop("input-tape.jsonl");
  if(parity_input_observer(1,sizeof *api)) return 7;
  for(f=0;f<FXS;++f) for(i=0;i<CASES;++i) {
    if(memcmp(&baseline[f][i],&hooked[f][i],sizeof baseline[0][0]) ||
       memcmp(baseline_after[f][i],after[f][i],sizeof buffer) || count_baseline[f][i]!=count_hooked[f][i]) {
      ++failures;
      if(failures==1) for(bytes=0;bytes<sizeof baseline[0][0];++bytes)
        if(((BYTE*)&baseline[f][i])[bytes]!=((BYTE*)&hooked[f][i])[bytes]) {
          printf("first_mismatch fx=%u case=%u byte=%u baseline=%02x hooked=%02x\n",f,i,bytes,((BYTE*)&baseline[f][i])[bytes],((BYTE*)&hooked[f][i])[bytes]);break;
        }
    }
  }
  save("baseline.bin",baseline,sizeof baseline);save("hooked.bin",hooked,sizeof hooked);
  save("post-baseline.bin",baseline_after,sizeof baseline_after);save("post-hooked.bin",after,sizeof after);
  save("count-baseline.bin",count_baseline,sizeof count_baseline);save("count-hooked.bin",count_hooked,sizeof count_hooked);
  save("entry-baseline.bin",entry_baseline,sizeof entry_baseline);save("entry-hooked.bin",entry_hooked,sizeof entry_hooked);
  printf("scope=owned_console_only cases=%u snapshots_equal=%d failures=%u stop=%d calls_state=%lu calls_acquire=%lu calls_data=%lu clocks=%lu phase_source=%lu\n",
         CASES*FXS,failures==0,failures,stopped,calls[0],calls[1],calls[2],tick_calls,(DWORD)(uintptr_t)api->phase_source);
  return failures ? 1 : 0;
}
