#define WIN32_LEAN_AND_MEAN
#include "input_observer.h"
#include <stdint.h>

static int belongs_to(HMODULE owner,const void* address) {
  HMODULE found=NULL;
  return address && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(const char*)address,&found) && found==owner;
}

/* Called at a normal proxy API boundary, never in DllMain/hot callbacks. The
 * launcher must still freeze/hash both DLLs; a numeric ABI identity is not a
 * substitute for the producer's file identity or a malicious-DLL defense. */
__declspec(dllexport) const struct parity_input_observer_v1* __cdecl parity_input_bind(const char* owner_name) {
  DWORD error=GetLastError();
  HMODULE owner=owner_name ? GetModuleHandleA(owner_name) : NULL;
  parity_input_getter getter=owner ? (parity_input_getter)(uintptr_t)GetProcAddress(owner,"parity_input_observer") : NULL;
  const struct parity_input_observer_v1* api=NULL;
  if(!getter || !belongs_to(owner,(const void*)(uintptr_t)getter)) goto done;
  api=getter(PARITY_INPUT_VERSION,sizeof *api);
  if(!api || !belongs_to(owner,api) || api->version!=PARITY_INPUT_VERSION || api->size!=sizeof *api ||
     api->identity!=PARITY_INPUT_IDENTITY || !belongs_to(owner,(const void*)api->phase_source) ||
     !belongs_to(owner,(const void*)(uintptr_t)api->register_device) ||
     !belongs_to(owner,(const void*)(uintptr_t)api->retire_device)) api=NULL;
done:
  SetLastError(error);return api;
}

__declspec(dllexport) const struct parity_input_com_observer_v2* __cdecl parity_input_com_bind2(const char* owner_name) {
  typedef const struct parity_input_com_observer_v2* (__cdecl *getter_fn)(DWORD,DWORD);
  DWORD error=GetLastError();
  HMODULE owner=owner_name ? GetModuleHandleA(owner_name) : NULL;
  getter_fn getter=owner ? (getter_fn)(uintptr_t)GetProcAddress(owner,"parity_input_com_observer2") : NULL;
  const struct parity_input_com_observer_v2* api=NULL;
  if(!getter || !belongs_to(owner,(const void*)(uintptr_t)getter)) goto done;
  api=getter(PARITY_INPUT_COM_VERSION2,sizeof *api);
  if(!api || !belongs_to(owner,api) || api->version!=PARITY_INPUT_COM_VERSION2 || api->size!=sizeof *api ||
     api->identity!=PARITY_INPUT_COM_IDENTITY2 || !belongs_to(owner,(const void*)api->phase_source) ||
     !belongs_to(owner,(const void*)(uintptr_t)api->register_device) ||
     !belongs_to(owner,(const void*)(uintptr_t)api->prepare_interface) ||
     !belongs_to(owner,(const void*)(uintptr_t)api->bind_interface) ||
     !belongs_to(owner,(const void*)(uintptr_t)api->retire_device)) api=NULL;
done:
  SetLastError(error);return api;
}
