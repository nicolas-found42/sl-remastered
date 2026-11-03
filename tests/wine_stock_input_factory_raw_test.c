/* Owned two-device factory admission only. No real DirectInput, Stock or input
 * injection. The actual proxy DLL is separate from the recorder owner DLL. */
#define WIN32_LEAN_AND_MEAN
#include "input_observer.h"
#include <unknwn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int (__cdecl *init_fn)(const char*,void*,const char*);
typedef HRESULT (WINAPI *create_fn)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
typedef void* (__cdecl *hook_fn)(unsigned,void*);
typedef int (__cdecl *stop_fn)(const char*);
static const GUID owner_iid={0xbf798030,0x483a,0x4da2,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0x00}};
static const GUID mouse={0x6f1d2b60,0xd5a0,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}};
static const GUID keyboard={0x6f1d2b61,0xd5a0,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}};
static void* device_table[11];
static void** keys=device_table;
static void** pointer=device_table;
static void* owner_table[4];
static void** owner_object=owner_table;
static unsigned qi_calls,ref_calls,release_calls,factory_calls,device_calls,state_calls;
static HRESULT WINAPI qi(void* object,REFIID iid,void** out) {(void)iid;++qi_calls;*out=object;return 0;}
static ULONG WINAPI addref(void* object) {(void)object;++ref_calls;return 2;}
static ULONG WINAPI release(void* object) {(void)object;++release_calls;return 1;}
static HRESULT WINAPI acquire(void* object) {(void)object;return 0;}
static HRESULT WINAPI state(void* object,DWORD size,void* out) {
  DWORD expected=object==&keys?256:16;
  ++state_calls;
  if(size!=expected) return (HRESULT)0x80070057u;
  memset(out,object==&keys?0x31:0x42,size);SetLastError(0x12345678);return 0;
}
static HRESULT WINAPI data(void* object,DWORD size,void* out,DWORD* count,DWORD flags) {
  (void)object;(void)size;(void)out;(void)flags;*count=0;return 0;
}
static HRESULT WINAPI device(void* object,REFGUID guid,void** out,IUnknown* outer) {
  (void)object;(void)outer;++device_calls;
  if(IsEqualGUID(guid,&keyboard)) *out=&keys;
  else if(IsEqualGUID(guid,&mouse)) *out=&pointer;
  else return (HRESULT)0x80070057u;
  SetLastError(0x87654321);return 0;
}
__attribute__((used,noinline)) static HRESULT WINAPI factory_body(HINSTANCE instance,DWORD version,REFIID iid,void** out,IUnknown* outer) {
  (void)instance;(void)version;(void)iid;(void)outer;++factory_calls;
  *out=&owner_object;SetLastError(0x76543210);return 0;
}
static void factory(void);
static char failure_mode[32];
__attribute__((unused)) static UINT WINAPI fake_system(LPSTR p,UINT n) {if(!strcmp(failure_mode,"missing-system"))return 0;if(!strcmp(failure_mode,"long-system"))return n;if(n<8)return 0;strcpy(p,"C:\\fake");return 7;}
__attribute__((unused)) static HMODULE WINAPI fake_load(LPCSTR p) {(void)p;return !strcmp(failure_mode,"missing-module")?NULL:(HMODULE)1;}
__attribute__((unused)) static FARPROC WINAPI fake_proc(HMODULE m,LPCSTR p) {(void)m;if(!strcmp(failure_mode,"missing-factory"))return NULL;return !strcmp(p,"DirectInput8Create")?(FARPROC)(uintptr_t)factory:NULL;}
__attribute__((unused)) static DWORD WINAPI fake_env(LPCSTR name,LPSTR p,DWORD n) {(void)name;if(!strcmp(failure_mode,"missing-env"))return 0;
 if(!strcmp(failure_mode,"long-env"))return n+1;
 if(!strcmp(failure_mode,"exact-env")){memset(p,'x',n);return n;}
 if(!strcmp(failure_mode,"long-log-path")){memset(p,'x',n-2);p[n-2]=0;return n-2;}
 if(n<2)return 0;
 strcpy(p,".");return 1;}
static DWORD WINAPI tick(void) {return 123;}
struct snapshot { DWORD regs[8], flags, error, pad[2]; BYTE fx[512]; } __attribute__((aligned(16)));
static DWORD mxcsr __attribute__((used))=0x1f80;
static WORD cw __attribute__((used))=0x037f;
static DWORD target_flags __attribute__((used))=0x202;
static struct snapshot raw_baseline,raw_repeat,raw_hooked;
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

static struct snapshot target_entry[3];
static DWORD target_args[3][5];
static unsigned target_count;
__attribute__((used,noinline)) static void record_target(DWORD* regs,BYTE* fx) {
 DWORD error=GetLastError();unsigned n=target_count++;
 if(n<3) {memcpy(&target_entry[n],regs,36);target_entry[n].error=error;memcpy(target_entry[n].fx,fx,512);memcpy(target_args[n],regs+10,20);}
 SetLastError(error);
}
__attribute__((naked)) static void factory(void) {
 __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tmovl %esp,%eax\n\tsubl $8,%esp\n\tpushl %eax\n\tpushl %ebx\n\tcall _record_target\n\taddl $16,%esp\n\tfxrstor (%esp)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tjmp _factory_body@20\n\t");
}
int main(int argc,char** argv) {
  HMODULE owner=NULL,proxy=NULL;init_fn initialize;create_fn create;hook_fn hook;stop_fn stop;
  void* observed=NULL;void* k=NULL;void* m=NULL;BYTE output[256];unsigned i;
  GetEnvironmentVariableA("FACTORY_FAILURE_MODE",failure_mode,sizeof failure_mode);
  if(argc!=2 || (strcmp(argv[1],"proxy-first") && strcmp(argv[1],"owner-first"))) return 2;
  if(!strcmp(argv[1],"proxy-first")) {
    proxy=LoadLibraryA("Z:\\work\\stock-input-owned.dll");if(!proxy)return 3;
    initialize=(init_fn)(uintptr_t)GetProcAddress(proxy,"stock_input_test_initialize");
    if(!initialize || initialize("input-owner.dll",(void*)(uintptr_t)factory,"factory-setup.jsonl"))return 4;
  }
  owner=LoadLibraryA("Z:\\work\\input-owner.dll");if(!owner)return 5;
  if(!proxy) proxy=LoadLibraryA("Z:\\work\\stock-input-owned.dll");
  if(!proxy) return 6;
  initialize=(init_fn)(uintptr_t)GetProcAddress(proxy,"stock_input_test_initialize");
  create=(create_fn)(uintptr_t)GetProcAddress(proxy,"DirectInput8Create");
  hook=(hook_fn)(uintptr_t)GetProcAddress(owner,"control_tape_test_hook");
  stop=(stop_fn)(uintptr_t)GetProcAddress(owner,"control_tape_stop");
  if(!initialize || !create || !hook || !stop)return 7;
  if(initialize("input-owner.dll",(void*)(uintptr_t)factory,"factory-setup.jsonl"))return 8;
  (void)hook(2,tick);
#ifdef FACTORY_LAZY_TEST
  {int (__cdecl *resolver)(void**,const char*)=(void*)(uintptr_t)GetProcAddress(proxy,"stock_input_test_resolver");
   void* functions[]={fake_system,fake_load,fake_proc,fake_env};const char* owner_name=!strcmp(failure_mode,"missing-owner")?"absent-owner.dll":!strcmp(failure_mode,"unready-owner")?"unready-owner.dll":"input-owner.dll";
   if(!strcmp(failure_mode,"unready-owner")&&!LoadLibraryA("Z:\\work\\unready-owner.dll"))return 35;
   if(!resolver||!resolver(functions,owner_name))return 9;}
#else
  if(!initialize("input-owner.dll",(void*)(uintptr_t)factory,"factory-setup.jsonl"))return 9;
#endif
  device_table[0]=qi;device_table[1]=addref;device_table[2]=release;device_table[7]=acquire;
  device_table[9]=state;device_table[10]=data;owner_table[3]=device;
  if(failure_mode[0]) {
    HRESULT first=create(NULL,0x800,&owner_iid,&observed,NULL),second=create(NULL,0x800,&owner_iid,&observed,NULL);
    printf("refusal=%s first=%08lx second=%08lx factory_calls=%u\n",failure_mode,(DWORD)first,(DWORD)second,factory_calls);
    return first==E_FAIL && second==E_FAIL && factory_calls==0 ? 0 : 34;
  }
  capture((void*)factory,NULL,0x800,(void*)&owner_iid,(DWORD*)&observed,2,&raw_baseline);
  capture((void*)factory,NULL,0x800,(void*)&owner_iid,(DWORD*)&observed,2,&raw_repeat);
  capture((void*)create,NULL,0x800,(void*)&owner_iid,(DWORD*)&observed,2,&raw_hooked);
  {FILE* f=fopen("factory-raw.bin","wb");if(!f)return 30;
   fwrite(&raw_baseline,1,sizeof raw_baseline,f);fwrite(&raw_repeat,1,sizeof raw_repeat,f);fwrite(&raw_hooked,1,sizeof raw_hooked,f);fclose(f);}
  {FILE* f=fopen("factory-entry.bin","wb");if(!f)return 31;
   fwrite(target_entry,1,sizeof target_entry,f);fwrite(target_args,1,sizeof target_args,f);fclose(f);}
  if(target_count!=3 || memcmp(target_args[0],target_args[1],20) || memcmp(target_args[0],target_args[2],20) ||
     memcmp(&target_entry[0],&target_entry[1],560) || memcmp(&target_entry[0],&target_entry[2],560))return 32;
  if(target_args[0][0] || target_args[0][1]!=0x800 || target_args[0][2]!=(DWORD)(uintptr_t)&owner_iid ||
     target_args[0][3]!=(DWORD)(uintptr_t)&observed || target_args[0][4])return 33;
  printf("raw_repeat=%d raw_hooked=%d snapshot_bytes=%u\n",!memcmp(&raw_baseline,&raw_repeat,sizeof raw_baseline),!memcmp(&raw_baseline,&raw_hooked,sizeof raw_baseline),(unsigned)sizeof raw_baseline);
  if(create(NULL,0x800,&owner_iid,&observed,NULL) || observed!=&owner_object || GetLastError()!=0x76543210)return 10;
  if(((HRESULT (WINAPI*)(void*,REFGUID,void**,IUnknown*))(uintptr_t)owner_table[3])(observed,&keyboard,&k,NULL) ||
     k!=&keys || GetLastError()!=0x87654321)return 11;
  if(((HRESULT (WINAPI*)(void*,REFGUID,void**,IUnknown*))(uintptr_t)owner_table[3])(observed,&mouse,&m,NULL) ||
     m!=&pointer || GetLastError()!=0x87654321)return 12;
  if(qi_calls || ref_calls || release_calls || factory_calls!=4 || device_calls!=2)return 13;
  if(((HRESULT (WINAPI*)(void*,DWORD,void*))(uintptr_t)device_table[9])(k,256,output) || GetLastError()!=0x12345678)return 14;
  for(i=0;i<256;++i)if(output[i]!=0x31)return 15;
  if(((HRESULT (WINAPI*)(void*,DWORD,void*))(uintptr_t)device_table[9])(m,16,output) || GetLastError()!=0x12345678)return 16;
  for(i=0;i<16;++i)if(output[i]!=0x42)return 17;
  if(state_calls!=2 || stop("factory-tape.jsonl")<0)return 18;
  printf("scope=owned_factory_admission_only order=%s devices=2 shared_vtable=1 added_COM_calls=0 state_calls=2\n",argv[1]);
  return 0;
}
