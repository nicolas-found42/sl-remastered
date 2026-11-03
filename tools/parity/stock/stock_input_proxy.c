/* Experimental record-only Stock DirectInput adapter. Factory/setup logging is
 * an explicit intervention, not a qualified transparent construction boundary.
 * Device consumption is owned by d3d9's guarded observer and shared phase source.
 * No synthetic state, command/stats threads or device hot-method wrappers. */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include "input_observer.h"
#include <dinput.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

typedef HRESULT (WINAPI *create_fn)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
typedef HRESULT (WINAPI *device_fn)(void*,REFGUID,void**,IUnknown*);
static const GUID iid_a={0x54d41080,0xdc15,0x4833,{0xa4,0x1b,0x74,0x8f,0x73,0xa3,0x81,0x79}};
static const GUID iid_owner_a={0xbf798030,0x483a,0x4da2,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0x00}};
static const GUID mouse={0x6f1d2b60,0xd5a0,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}};
static const GUID keyboard={0x6f1d2b61,0xd5a0,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}};
static char log_path[MAX_PATH];
static const struct parity_input_com_observer_v2* api;
static create_fn real_create __attribute__((used));
static int initialization_failed;
static void** owner_vtable;
static device_fn real_device __attribute__((used));
static DWORD next_device;
static void log_row(const char* format,...) {
  DWORD error=GetLastError();FILE* f=fopen(log_path,"a");va_list args;
  if(f) {va_start(args,format);vfprintf(f,format,args);va_end(args);fputc('\n',f);fclose(f);}
  SetLastError(error);
}
#ifdef CONTROL_TAPE_TEST
/* Owned module/load-order qualification only; absent from actual Stock builds.
 * The injected factory is called through the production admission path. */
__declspec(dllexport) int __cdecl stock_input_test_initialize(const char* owner,void* factory,const char* path) {
  if(real_create || initialization_failed || !factory || !path || strlen(path)>=sizeof log_path) return 0;
  api=parity_input_com_bind2(owner);
  if(!api) return 0;
  strcpy(log_path,path);real_create=(create_fn)(uintptr_t)factory;
  log_row("{\"event\":\"setup\",\"scope\":\"owned_factory_admission_only\",\"observer_bound\":1}");
  return 1;
}
#endif
static int patch_owner(void** table,void* hook) {
  DWORD old,ignored;
  if(owner_vtable) return owner_vtable==table && table[3]==hook;
  if(!VirtualProtect(table+3,sizeof(void*),PAGE_READWRITE,&old)) return 0;
  owner_vtable=table;real_device=(device_fn)(uintptr_t)table[3];table[3]=hook;
  return VirtualProtect(table+3,sizeof(void*),old,&ignored)!=0;
}
static int bind_device(void* object,DWORD kind) {
  DWORD id=++next_device;
  /* The observed owner8A CreateDevice contract supplies Device8A. Do not
   * manufacture QI/AddRef/Release calls to discover siblings. A naturally
   * returned unsupported alias remains an explicit observer Gap. */
  int result=api->prepare_interface(object,&iid_a) && api->register_device(object,id,1,kind);
  log_row("{\"event\":\"device_setup\",\"device_id\":%lu,\"kind\":%lu,\"object\":%lu,\"added_com_calls\":0,\"aliases_bound\":0,\"bound\":%d,\"gap\":%d}",id,kind,(DWORD)(uintptr_t)object,result,!result);
  return result;
}
static const char* owner_name="d3d9.dll";
#ifdef CONTROL_TAPE_TEST
static UINT (WINAPI *system_directory)(LPSTR,UINT);
static HMODULE (WINAPI *load_library)(LPCSTR);
static FARPROC (WINAPI *get_proc)(HMODULE,LPCSTR);
static DWORD (WINAPI *environment)(LPCSTR,LPSTR,DWORD);
#define RESOLVE(F,D) (F?F:D)
__declspec(dllexport) int __cdecl stock_input_test_resolver(void** functions,const char* owner) {
 if(real_create||initialization_failed||!functions||!owner)return 0;
 system_directory=(void*)functions[0];load_library=(void*)functions[1];get_proc=(void*)functions[2];environment=(void*)functions[3];owner_name=owner;return 1;
}
#else
#define RESOLVE(F,D) D
#endif
static HRESULT WINAPI factory_refused(HINSTANCE a __attribute__((unused)),DWORD b __attribute__((unused)),REFIID c __attribute__((unused)),void** d __attribute__((unused)),IUnknown* e __attribute__((unused))) {return E_FAIL;}
static void factory_prepare(void) {
  if(!real_create) {
    char path[MAX_PATH],dir[MAX_PATH];HMODULE module;create_fn candidate;DWORD length;
    /* A failed setup must never publish a factory with missing context. The
     * next call remains refused instead of silently skipping initialization. */
    if(initialization_failed) return;
    initialization_failed=1;real_create=factory_refused;
    length=RESOLVE(system_directory,GetSystemDirectoryA)(path,sizeof path);
    if(!length || length>=sizeof path || length+13>=sizeof path) return;
    strcat(path,"\\dinput8.dll");module=RESOLVE(load_library,LoadLibraryA)(path);
    if(!module) return;
    candidate=(create_fn)(uintptr_t)RESOLVE(get_proc,GetProcAddress)(module,"DirectInput8Create");
    if(!candidate) return;
    length=RESOLVE(environment,GetEnvironmentVariableA)("SLRR_DUMP_DIR",dir,sizeof dir);
    if(!length || length>=sizeof dir) return;
    if(snprintf(log_path,sizeof log_path,"%s\\stock-input-setup.jsonl",dir)>=(int)sizeof log_path) return;
    api=parity_input_com_bind2(owner_name);
    if(!api) {log_row("{\"event\":\"factory_gap\",\"reason\":\"observer_unbound\"}");return;}
    log_row("{\"event\":\"setup\",\"scope\":\"experimental_stock_consumption\",\"observer_bound\":%d,\"construction_transparency_qualified\":false,\"replay_qualified\":false}",api!=NULL);
    real_create=candidate;
    initialization_failed=0;
  }
}
/* Experimental bounded normal-return trampolines. Caller stack arguments and
 * machine state pass unchanged into the target; only its return slot changes.
 * Eight nested calls per thread. Exceptions/unwind remain unqualified. */
static void create_device(void);
static void factory_return(void);
struct factory_frame { DWORD caller,args[5],kind; };
static __thread struct factory_frame frames[8];
static __thread unsigned depth;
__attribute__((used,noinline)) static void factory_enter(DWORD* regs,DWORD kind) {
  DWORD error=GetLastError();DWORD* stack=regs+9;struct factory_frame* f;
  if(!kind&&!real_create)factory_prepare();
  if(depth==8) {log_row("{\"event\":\"factory_gap\",\"reason\":\"depth_overflow\"}");SetLastError(error);return;}
  f=&frames[depth++];f->caller=stack[0];f->kind=kind;
  memcpy(f->args,stack+1,(kind?4:5)*sizeof(DWORD));
  stack[0]=(DWORD)(uintptr_t)factory_return;
  SetLastError(error);
}
__attribute__((used,noinline)) static DWORD factory_complete(DWORD* regs) {
  DWORD error=GetLastError(),caller;struct factory_frame* f;HRESULT hr=(HRESULT)regs[7];
  int admitted=0;void** out;
  if(!depth) TerminateProcess(GetCurrentProcess(),0xe7a90004u);
  f=&frames[--depth];caller=f->caller;
  if(f->kind) {
    REFGUID guid=(REFGUID)(uintptr_t)f->args[1];DWORD kind;
    out=(void**)(uintptr_t)f->args[2];
    kind=IsEqualGUID(guid,&keyboard)?PARITY_INPUT_KEYBOARD:IsEqualGUID(guid,&mouse)?PARITY_INPUT_MOUSE:0;
    if(SUCCEEDED(hr)&&out&&*out&&!f->args[3]&&kind&&api)admitted=bind_device(*out,kind);
    log_row("{\"event\":\"create_device\",\"hr\":%lu,\"kind\":%lu,\"bound\":%d,\"gap\":%d}",(DWORD)hr,kind,admitted,SUCCEEDED(hr)&&!admitted);
  } else {
    out=(void**)(uintptr_t)f->args[3];
    admitted=SUCCEEDED(hr)&&f->args[1]==0x800&&IsEqualGUID((REFIID)(uintptr_t)f->args[2],&iid_owner_a)&&!f->args[4]&&out&&*out&&api;
    if(admitted)admitted=patch_owner(*(void***)*out,(void*)(uintptr_t)create_device);
    log_row("{\"event\":\"create_owner\",\"hr\":%lu,\"admitted\":%d,\"gap\":%d}",(DWORD)hr,admitted,SUCCEEDED(hr)&&!admitted);
  }
  SetLastError(error);return caller;
}
#define FACTORY_ENTRY(NAME,KIND,TARGET) \
__attribute__((naked,used)) static void NAME(void) { \
 __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tsubl $8,%esp\n\tpushl $" #KIND "\n\tpushl %ebx\n\tcall _factory_enter\n\taddl $16,%esp\n\tfxrstor (%esp)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tjmp *_" #TARGET "\n\t"); }
FACTORY_ENTRY(factory_entry,0,real_create)
FACTORY_ENTRY(create_device,1,real_device)
__attribute__((naked)) static void factory_return(void) {
 __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tsubl $12,%esp\n\tpushl %ebx\n\tcall _factory_complete\n\taddl $16,%esp\n\tfxrstor (%esp)\n\tmovl %eax,-4(%ebx)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tpushl -40(%esp)\n\tret\n\t");
}
__declspec(dllexport) __attribute__((naked)) HRESULT WINAPI DirectInput8Create(HINSTANCE a __attribute__((unused)),DWORD b __attribute__((unused)),REFIID c __attribute__((unused)),void** d __attribute__((unused)),IUnknown* e __attribute__((unused))) {
 __asm__ volatile("jmp _factory_entry\n\t");
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void* reserved) {(void)module;(void)reason;(void)reserved;return TRUE;}
