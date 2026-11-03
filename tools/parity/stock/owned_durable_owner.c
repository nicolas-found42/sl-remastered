/* Owned imported PE32 mapping fixture only. No Stock hook or readiness claim.
 * The external collector creates the mapping before CreateProcess. Attach
 * opens/maps/validates it before the first owned consumer, with no fallback,
 * worker creation, waiting or file I/O under loader lock. Exact backend and
 * loader/FX behavior must still be measured before using this design in Stock.
 */
#define OWNED_DURABLE_OWNER
#include "owned_durable_api.h"
#include "durable_ring.h"
#include <string.h>
static struct dr_view view;
static HANDLE mapping,ready;
static volatile LONG held_thread;
static int bound;

static int offer(uint32_t kind,uint32_t index,uint32_t stage) {
  struct dr_reservation r;
  struct owned_durable_event event={index,GetCurrentThreadId(),stage,(uint32_t)(uintptr_t)view.memory};
  if(kind==OWNED_ATTACH || kind==OWNED_DETACH)event.index=GetCurrentProcessId();
  return dr_reserve(view,&r)==DR_OK && dr_commit(view,r,kind,&event,sizeof event)==DR_OK;
}
int __cdecl owned_durable_offer(uint32_t kind,uint32_t index) {return bound && offer(kind,index,0);}
int __cdecl owned_durable_space(void) {
  struct dr_header* h=view.memory;DWORD next,ack;
  if(!bound || !h || InterlockedCompareExchange((volatile LONG*)&h->flags,0,0))return 0;
  next=(DWORD)InterlockedCompareExchange((volatile LONG*)&h->next_ticket,0,0);
  ack=(DWORD)InterlockedCompareExchange((volatile LONG*)&h->ack_ticket,0,0);
  return next>=ack && next-ack<OWNED_DURABLE_SLOTS;
}
int __cdecl owned_durable_pending(void) {
  struct dr_reservation r;return bound && dr_reserve(view,&r)==DR_OK;
}
int __cdecl owned_durable_hold_writing(void) {
  if(!bound)return 0;
  InterlockedExchange(&held_thread,(LONG)GetCurrentThreadId());return offer(OWNED_DATA,0xabcdef01u,1);
}
void dr_test_reserved(uint32_t ticket) {(void)ticket;}
void dr_test_reading(uint32_t ticket) {(void)ticket;}
void dr_test_writing(uint32_t ticket) {
  (void)ticket;
  if((DWORD)InterlockedCompareExchange(&held_thread,0,0)==GetCurrentThreadId()) {
    SetEvent(ready);
    Sleep(5000); /* Only this owned death fixture; never called under attach. */
  }
}
__attribute__((target("no-sse,no-sse2")))
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) {
  (void)instance;
  if(reason==DLL_PROCESS_ATTACH) {
    char name[96],text[33],event_name[112];uint32_t nonce[4];DWORD n;
    n=GetEnvironmentVariableA("SLRR_DURABLE_MAP_NAME",name,sizeof name);
    if(n!=51 || memcmp(name,"Local\\slrr-durable-",19))return FALSE;
    n=GetEnvironmentVariableA("SLRR_DURABLE_NONCE",text,sizeof text);
    if(n!=32 || !owned_parse_nonce(text,nonce))return FALSE;
    mapping=OpenFileMappingA(FILE_MAP_ALL_ACCESS,FALSE,name);if(!mapping)return FALSE;
    view.bytes=dr_required_bytes(OWNED_DURABLE_SLOTS);
    view.memory=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,view.bytes);
    if(!view.memory || dr_validate(view,nonce)!=DR_OK)return FALSE;
    memcpy(event_name,name,52);memcpy(event_name+51,"-ready",7);
    ready=OpenEventA(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,event_name);if(!ready)return FALSE;
    bound=offer(OWNED_ATTACH,0,1);return bound?TRUE:FALSE;
  }
  if(reason==DLL_PROCESS_DETACH && bound) {
    /* Never unmap while other detach callbacks could still use the recorder.
     * This fixture imports the owner for its complete process lifetime and
     * never FreeLibrarys it. OS teardown reclaims its view/handles. */
    offer(OWNED_DETACH,0,reserved?1u:0u);
  }
  return TRUE;
}
