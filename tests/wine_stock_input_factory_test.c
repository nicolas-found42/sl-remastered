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
static HRESULT WINAPI factory(HINSTANCE instance,DWORD version,REFIID iid,void** out,IUnknown* outer) {
  (void)instance;(void)version;(void)iid;(void)outer;++factory_calls;
  *out=&owner_object;SetLastError(0x76543210);return 0;
}
static DWORD WINAPI tick(void) {return 123;}
int main(int argc,char** argv) {
  HMODULE owner=NULL,proxy=NULL;init_fn initialize;create_fn create;hook_fn hook;stop_fn stop;
  void* observed=NULL;void* k=NULL;void* m=NULL;BYTE output[256];unsigned i;
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
  if(!initialize("input-owner.dll",(void*)(uintptr_t)factory,"factory-setup.jsonl"))return 9;
  device_table[0]=qi;device_table[1]=addref;device_table[2]=release;device_table[7]=acquire;
  device_table[9]=state;device_table[10]=data;owner_table[3]=device;
  if(create(NULL,0x800,&owner_iid,&observed,NULL) || observed!=&owner_object || GetLastError()!=0x76543210)return 10;
  if(((HRESULT (WINAPI*)(void*,REFGUID,void**,IUnknown*))(uintptr_t)owner_table[3])(observed,&keyboard,&k,NULL) ||
     k!=&keys || GetLastError()!=0x87654321)return 11;
  if(((HRESULT (WINAPI*)(void*,REFGUID,void**,IUnknown*))(uintptr_t)owner_table[3])(observed,&mouse,&m,NULL) ||
     m!=&pointer || GetLastError()!=0x87654321)return 12;
  if(qi_calls || ref_calls || release_calls || factory_calls!=1 || device_calls!=2)return 13;
  if(((HRESULT (WINAPI*)(void*,DWORD,void*))(uintptr_t)device_table[9])(k,256,output) || GetLastError()!=0x12345678)return 14;
  for(i=0;i<256;++i)if(output[i]!=0x31)return 15;
  if(((HRESULT (WINAPI*)(void*,DWORD,void*))(uintptr_t)device_table[9])(m,16,output) || GetLastError()!=0x12345678)return 16;
  for(i=0;i<16;++i)if(output[i]!=0x42)return 17;
  if(state_calls!=2 || stop("factory-tape.jsonl")<0)return 18;
  printf("scope=owned_factory_admission_only order=%s devices=2 shared_vtable=1 added_COM_calls=0 state_calls=2\n",argv[1]);
  return 0;
}
