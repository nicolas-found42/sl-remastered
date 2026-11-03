/* Two actual owned DLLs, both load orders, no Stock or device APIs. */
#define WIN32_LEAN_AND_MEAN
#include "input_observer.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef const struct parity_input_observer_v1* (__cdecl *bind_fn)(const char*);
typedef const struct parity_input_com_observer_v2* (__cdecl *bind2_fn)(const char*);
typedef void* (__cdecl *hook_fn)(unsigned,void*);
typedef int (__cdecl *stop_fn)(const char*);
static DWORD (WINAPI *clock_hook)(void);
static DWORD WINAPI tick(void) {return 123;}
static HRESULT WINAPI state(void* self,DWORD cb,void* out) {
  (void)self;
  if(cb!=256 || clock_hook()!=123) return (HRESULT)0x80004005u;
  memset(out,0x91,cb);return 0;
}
static HRESULT WINAPI acquire(void* self) {(void)self;return 1;}
static HRESULT WINAPI data(void* self,DWORD cb,void* out,DWORD* count,DWORD flags) {
  (void)self;(void)cb;(void)out;(void)flags;*count=0;return 0;
}
int main(int argc,char** argv) {
  HMODULE owner=NULL,client=NULL;
  bind_fn bind=NULL;hook_fn hook;stop_fn stop;parity_input_getter getter;
  bind2_fn bind2;
  const struct parity_input_com_observer_v2* com;
  const struct parity_input_observer_v1* api;
  void* vtable[11]={0};void** object=vtable;BYTE bytes[256];
  int unavailable=0,not_ready=0,i;
  if(argc!=2) return 2;
  if(!strcmp(argv[1],"client-first")) {
    client=LoadLibraryA("Z:\\work\\input-client.dll");
    if(!client) return 3;
    bind=(bind_fn)(uintptr_t)GetProcAddress(client,"parity_input_bind");
    if(!bind || bind("input-owner.dll")) return 4;
    bind2=(bind2_fn)(uintptr_t)GetProcAddress(client,"parity_input_com_bind2");
    if(!bind2 || bind2("input-owner.dll")) return 16;
    unavailable=1;
  }
  owner=LoadLibraryA("Z:\\work\\input-owner.dll");
  if(!owner) return 5;
  if(!client) {
    client=LoadLibraryA("Z:\\work\\input-client.dll");
    if(!client) return 6;
    bind=(bind_fn)(uintptr_t)GetProcAddress(client,"parity_input_bind");
  }
  if(!bind || bind("input-owner.dll")) return 7;
  bind2=(bind2_fn)(uintptr_t)GetProcAddress(client,"parity_input_com_bind2");
  if(!bind2 || bind2("input-owner.dll")) return 17;
  not_ready=1;
  hook=(hook_fn)(uintptr_t)GetProcAddress(owner,"control_tape_test_hook");
  stop=(stop_fn)(uintptr_t)GetProcAddress(owner,"control_tape_stop");
  getter=(parity_input_getter)(uintptr_t)GetProcAddress(owner,"parity_input_observer");
  if(!hook || !stop || !getter) return 8;
  clock_hook=(DWORD (WINAPI*)(void))(uintptr_t)hook(2,tick);
  SetLastError(0x55667788);
  api=bind("input-owner.dll");
  if(GetLastError()!=0x55667788 || !api || api!=getter(1,sizeof *api) || bind("kernel32.dll")) return 9;
  SetLastError(0x55667788);
  com=bind2("input-owner.dll");
  if(GetLastError()!=0x55667788 || !com || com->phase_source!=api->phase_source ||
     bind2("kernel32.dll") || bind2(NULL)) return 18;
  if(clock_hook()!=123 || *api->phase_source!=2) return 10;
  vtable[9]=state;vtable[7]=acquire;vtable[10]=data;
  if(!api->register_device(&object,1,1,PARITY_INPUT_KEYBOARD)) return 11;
  if(((HRESULT (WINAPI*)(void*,DWORD,void*))vtable[9])(&object,256,bytes)) return 12;
  for(i=0;i<256;++i) if(bytes[i]!=0x91) return 13;
  if(*api->phase_source!=6) return 14;
  if(stop("binding-tape.jsonl")<0 || bind("input-owner.dll")) return 15;
  if(bind2("input-owner.dll")) return 19;
  printf("scope=owned_console_only order=%s unavailable_refused=%d not_ready_refused=%d phases=%ld same_owner_table=1\n",argv[1],unavailable,not_ready,*api->phase_source);
  return 0;
}
