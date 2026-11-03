/* Owned inline Native Float32 bridge qualification, never Stock or replay.
 * Execute real FSTP32 before original MOV/ADD or the production observer.
 * Retain consumed bits and raw snapshots; backend FXSAVE remains opaque. */
#define WIN32_LEAN_AND_MEAN
#include "control_tape.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum { CASES=384 };
struct snapshot {
  DWORD regs[8],flags,error,pad[2];BYTE fx[512];
  DWORD stored_bits;WORD cw,sw;DWORD canaries[2];
} __attribute__((aligned(16)));
static struct snapshot baseline[CASES],repeat[CASES],observed[CASES];
static WORD cw __attribute__((used));
static DWORD mxcsr __attribute__((used)),flags __attribute__((used));
static double input_value __attribute__((used));
static DWORD extra_stack __attribute__((used));
static DWORD frame_base __attribute__((used)),arena_base __attribute__((used));
static struct snapshot* snapshot_target __attribute__((used));

static LONG CALLBACK legacy_descriptor_fault(EXCEPTION_POINTERS* exception) {
  if(exception->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
    static const char report[]="{\"scope\":\"owned_descriptor_guard_page\",\"legacy_descriptor_oob\":true}\n";
    DWORD wrote;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),report,sizeof report-1,&wrote,NULL);ExitProcess(91);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
static int legacy_descriptor_control(void) {
  SYSTEM_INFO system;BYTE* memory;BYTE* image;
  struct control_tape_early_plan* plan;DWORD ignored;int result;
  GetSystemInfo(&system);
  memory=VirtualAlloc(NULL,system.dwPageSize*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
  image=VirtualAlloc(NULL,system.dwPageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
  if(!memory || !image) return 0;
  if(!VirtualProtect(memory+system.dwPageSize,system.dwPageSize,PAGE_NOACCESS,&ignored)) return 0;
  plan=(struct control_tape_early_plan*)(memory+system.dwPageSize-
                                        offsetof(struct control_tape_early_plan,native_clock_observer));
  plan->version=1;plan->kind_mask=31;
  result=control_tape_early_bootstrap((HMODULE)image,plan);
  VirtualFree(memory,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);return result==0;
}
__attribute__((naked)) static void continuation(void) {__asm__ volatile("ret\n\t");}
__attribute__((naked)) static void plain(void) {
  __asm__ volatile("movl 0x5c(%esp),%ebp\n\taddl $4,%esp\n\tret\n\t");
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
__attribute__((naked)) static void capture(void* fn,struct snapshot* out,DWORD callee) {
  __asm__ volatile(
    "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\tsubl $128,%esp\n\t"
    "movl %ebp,_frame_base\n\tmovl %esp,_arena_base\n\tmovl 12(%ebp),%edi\n\tmovl %edi,_snapshot_target\n\t"
    "movl $0xfeedface,(%esp)\n\tmovl $1f,4(%esp)\n\tmovl 16(%ebp),%eax\n\tmovl %eax,0x20(%esp)\n\t"
    "movl $0x12345678,0x60(%esp)\n\tmovl $0x9abcdef0,0x78(%esp)\n\t"
    "fninit\n\tfldcw _cw\n\tcmpl $0,_extra_stack\n\tje 2f\n\tfldpi\n\t2: fldl _input_value\n\tfstps 0x5c(%esp)\n\t"
    "movl 0x5c(%esp),%eax\n\tmovl %eax,560(%edi)\n\tldmxcsr _mxcsr\n\t"
    "pcmpeqd %xmm0,%xmm0\n\tpcmpeqd %xmm7,%xmm7\n\tmovl $0x55aa55aa,%fs:0x34\n\t"
    "movl $0x11111111,%eax\n\tmovl $0x22222222,%ecx\n\tmovl $0x33333333,%edx\n\t"
    "movl $0x44444444,%ebx\n\tmovl $0x55555555,%esi\n\tmovl $0x66666666,%edi\n\t"
    "pushl _flags\n\tpopfl\n\tjmp *8(%ebp)\n\t"
    "1: pushfl\n\tpushal\n\tcld\n\tmovl %esp,%esi\n\tmovl _snapshot_target,%edi\n\tmovl $9,%ecx\n\trep movsl\n\t"
    "movl %fs:0x34,%eax\n\tmovl %eax,(%edi)\n\tmovl _snapshot_target,%edi\n\tfxsave 48(%edi)\n\t"
    "fnstcw 564(%edi)\n\tfnstsw 566(%edi)\n\tmovl _arena_base,%eax\n\tmovl 0x60(%eax),%edx\n\tmovl %edx,568(%edi)\n\t"
    "movl 0x78(%eax),%edx\n\tmovl %edx,572(%edi)\n\tmovl _frame_base,%eax\n\t"
    "lea -12(%eax),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tmovl %eax,%ebp\n\tleave\n\tret\n\t");
}
#pragma GCC diagnostic pop
static int save(const char* name,const void* bytes,size_t size) {
  FILE* f=fopen(name,"wb");if(!f) return 0;
  return fwrite(bytes,1,size,f)==size && fclose(f)==0;
}
static DWORD writer_ticks;
static DWORD WINAPI writer_tick(void) {return 0x12340000u+writer_ticks++;}
static int writer_control(const char* label,int failure) {
  enum { ROWS=2000 };
  DWORD (WINAPI *tick)(void)=control_tape_test_hook(2,writer_tick);
  DWORD start,elapsed;unsigned i;int first=0,stopped;
  char path[MAX_PATH],failed_path[MAX_PATH];
  snprintf(path,sizeof path,"Z:\\tmp\\%s-tape.jsonl",label);
  snprintf(failed_path,sizeof failed_path,"Z:\\tmp\\%s-failed.jsonl",label);
  for(i=0;i<ROWS;++i) if(tick()!=0x12340000u+i) return 25;
  control_tape_test_writer(failure?0:3,failure);
  start=GetTickCount();
  if(failure) first=control_tape_stop(failed_path);
  stopped=control_tape_stop(path);
  elapsed=GetTickCount()-start;
  printf("{\"scope\":\"owned_writer_only\",\"rows\":%u,\"elapsed_ms\":%lu,\"first_result\":%d,\"stop_result\":%d,\"failure_control\":%d}\n",ROWS,elapsed,first,stopped,failure);
  return stopped==ROWS && (!failure || first<0) && elapsed<5000 ? 0:1;
}
int main(int argc,char** argv) {
  static const DWORD flag_values[]={0x202,0x203,0x246,0x697};
  static const double values[]={1.0,1.0/3.0,-0.0,3.141592653589793,1e-40,1e40};
  void* hook;unsigned extra,mode,fl,value,callee,index=0,failures=0,unstable=0;int stopped;
  if(argc==2 && (!strcmp(argv[1],"writer-fast") || !strcmp(argv[1],"writer-old"))) return writer_control(argv[1],0);
  if(argc==2 && !strcmp(argv[1],"writer-fail")) return writer_control(argv[1],1);
  if(sizeof(struct snapshot)!=576 || !AddVectoredExceptionHandler(1,legacy_descriptor_fault) || !legacy_descriptor_control()) return 20;
  hook=control_tape_test_native_clock_hook(continuation,0x400000);
  for(extra=0;extra<2;++extra) for(mode=0;mode<4;++mode) for(fl=0;fl<4;++fl)
    for(value=0;value<6;++value) for(callee=0;callee<2;++callee) {
      DWORD target=callee?0x47c000:0x47bfe0;
      extra_stack=extra;cw=(WORD)(0x37f+(mode<<10));mxcsr=0x1f80+(mode<<13);
      flags=flag_values[fl];input_value=values[value];
      capture(plain,&baseline[index],target);capture(plain,&repeat[index],target);capture(hook,&observed[index],target);
      if(memcmp(&baseline[index],&repeat[index],sizeof(struct snapshot))) ++unstable;
      if(memcmp(&baseline[index],&observed[index],sizeof(struct snapshot)) ||
         observed[index].canaries[0]!=0x12345678 || observed[index].canaries[1]!=0x9abcdef0 ||
         observed[index].regs[2]!=observed[index].stored_bits) ++failures;
      ++index;
    }
  stopped=control_tape_stop("native-clock-tape.jsonl");
  /* Private volatile staging: the adapter losslessly retains every array
   * before the next case, avoiding duplicate uncompressed retained payloads. */
  if(!save("Z:\\tmp\\native-clock-baseline.bin",baseline,sizeof baseline) ||
     !save("Z:\\tmp\\native-clock-repeat.bin",repeat,sizeof repeat) ||
     !save("Z:\\tmp\\native-clock-observed.bin",observed,sizeof observed)) return 21;
  printf("{\"scope\":\"owned_post_fstp32_bridge_only\",\"legacy_descriptor_refused_safely\":true,\"cases\":%u,\"unstable\":%u,\"failures\":%u,\"stopped\":%d}\n",index,unstable,failures,stopped>=0);
  return failures || unstable || stopped<0 ? 1 : 0;
}
