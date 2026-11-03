/* Synthetic owned lifecycle fixture, never Stock or replay evidence. */
#define CONTROL_TAPE_TEST
#define CONTROL_TAPE_INPUT
#define CONTROL_TAPE_RNG
#include "../tools/parity/stock/control_tape.c"
#include <stdlib.h>
static HANDLE held,release_parent;
static DWORD (WINAPI *tick_hook)(void);
static BOOL (WINAPI *frequency_hook)(LARGE_INTEGER*);
static volatile LONG held_once;
static BOOL WINAPI fake_frequency(LARGE_INTEGER* value) {value->QuadPart=123456;return TRUE;}
static DWORD WINAPI fake_tick(void) {
  if(InterlockedCompareExchange(&held_once,1,0)==0) {
    LARGE_INTEGER value;
    SetEvent(held);
    if(WaitForSingleObject(release_parent,5000)!=WAIT_OBJECT_0) ExitProcess(71);
    if(!frequency_hook(&value) || value.QuadPart!=123456) ExitProcess(72);
  }
  return 123;
}
static DWORD WINAPI worker(void* ignored) {(void)ignored;return tick_hook();}
static DWORD WINAPI release_worker(void* ignored) {
  (void)ignored;
  while(!((DWORD)InterlockedCompareExchange(&observation_gate,0,0)&OBSERVATION_CLOSED)) Sleep(1);
  if(tick_hook()!=123) ExitProcess(73); /* New root during close is forwarded but not recorded. */
  SetEvent(release_parent);return 0;
}
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  if(!strcmp(argv[1],"nested")) {
    HANDLE parent,releaser;DWORD value;int stopped;
    held=CreateEventA(NULL,TRUE,FALSE,NULL);release_parent=CreateEventA(NULL,TRUE,FALSE,NULL);
    if(!held || !release_parent)return 3;
    tick_hook=(void*)control_tape_test_hook(2,fake_tick);
    frequency_hook=(void*)control_tape_test_hook(1,fake_frequency);
    parent=CreateThread(NULL,0,worker,NULL,0,NULL);
    if(!parent || WaitForSingleObject(held,5000)!=WAIT_OBJECT_0)return 4;
    releaser=CreateThread(NULL,0,release_worker,NULL,0,NULL);if(!releaser)return 5;
    stopped=control_tape_stop("nested.jsonl");
    if(stopped!=2 || WaitForSingleObject(parent,5000)!=WAIT_OBJECT_0 ||
       !GetExitCodeThread(parent,&value) || value!=123)return 6;
    if(tick_hook()!=123 || next_sequence!=2 || next_return!=2 || next_phase!=4 || active || drops || gap_count)return 7;
    if((DWORD)observation_gate!=OBSERVATION_CLOSED)return 8;
    puts("nested_after_close_recorded=1 root_during_close_ignored=1 root_after_drain_ignored=1 parent_result=123 calls=2 phases=4");
    return 0;
  }
  if(!strcmp(argv[1],"exit") || !strcmp(argv[1],"timeout")) {
    BYTE* image=VirtualAlloc(NULL,0x3c0000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    void* target=(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"),"ExitProcess");
    void (WINAPI *call_exit)(UINT);
    if(!image || !target)return 10;
    control_tape_test_hook(2,GetTickCount);
    control_tape_test_guard_image(image,0x3c0000);
    /* Structural import/caller guard controls. Full Stock hash is NOT claimed. */
    *(void**)(image+0x1e71f0)=target;
    if(control_tape_shutdown_arm((HMODULE)image,"exit.jsonl","shutdown.json")!=0)return 11;
    memcpy(image+0x1d7965,"\xff\x74\x24\x04\xff\x15",6);
    *(DWORD*)(image+0x1d796b)=(DWORD)(uintptr_t)(image+0x1e71f0);
    *(void**)(image+0x1e71f0)=NULL;
    if(control_tape_shutdown_arm((HMODULE)image,"exit.jsonl","shutdown.json")!=0)return 12;
    *(void**)(image+0x1e71f0)=target;
    if(control_tape_shutdown_arm((HMODULE)image,"exit.jsonl","shutdown.json")!=1)return 13;
    if(!strcmp(argv[1],"timeout")) {
      HANDLE parent;
      held=CreateEventA(NULL,TRUE,FALSE,NULL);release_parent=CreateEventA(NULL,TRUE,FALSE,NULL);
      if(!held || !release_parent)return 15;
      tick_hook=(void*)control_tape_test_hook(2,fake_tick);
      parent=CreateThread(NULL,0,worker,NULL,0,NULL);
      if(!parent || WaitForSingleObject(held,5000)!=WAIT_OBJECT_0)return 16;
      /* Real recorded parent remains inside the target until forwarded exit
       * terminates this process. No pending/active counter is fabricated. */
    }
    call_exit=(void*)(image+0x1d7965);
    call_exit(37);
    return 14;
  }
  return 9;
}
