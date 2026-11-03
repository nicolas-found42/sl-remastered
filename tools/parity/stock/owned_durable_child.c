/* Synthetic owned consumer; pacing is test-only and outside the ring protocol. */
#include "owned_durable_api.h"
#include <string.h>
static HANDLE start;
static int pace(void) {
  DWORD since=GetTickCount();
  while(!owned_durable_space()) {
    if(GetTickCount()-since>5000)return 0;
    Sleep(1);
  }
  return 1;
}
static DWORD WINAPI late_worker(void* unused) {
  (void)unused;
  if(WaitForSingleObject(start,5000)!=WAIT_OBJECT_0)return 81;
  return pace() && owned_durable_offer(OWNED_LATE,1)?0:82;
}
static DWORD WINAPI writing_worker(void* unused) {(void)unused;return owned_durable_hold_writing()?0:83;}
static HANDLE ready_event(void) {
  char name[112];DWORD n=GetEnvironmentVariableA("SLRR_DURABLE_MAP_NAME",name,96);
  if(n!=51)return NULL;
  memcpy(name+51,"-ready",7);return OpenEventA(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,name);
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show) {
  (void)instance;(void)previous;(void)show;
  if(!strcmp(command,"normal")) {
    for(unsigned i=0;i<OWNED_DURABLE_DATA_COUNT;++i)
      if(!pace() || !owned_durable_offer(OWNED_DATA,i))return 71;
    return 0;
  }
  if(!strcmp(command,"late")) {
    DWORD code;HANDLE worker;
    start=CreateEventA(NULL,TRUE,FALSE,NULL);if(!start)return 72;
    worker=CreateThread(NULL,0,late_worker,NULL,0,NULL);if(!worker)return 73;
    if(!pace() || !owned_durable_offer(OWNED_EXIT_MARKER,0))return 74;
    SetEvent(start);
    if(WaitForSingleObject(worker,5000)!=WAIT_OBJECT_0 || !GetExitCodeThread(worker,&code) || code)return 75;
    return 0;
  }
  if(!strcmp(command,"pending")) {
    if(!owned_durable_pending())return 76;
    ExitProcess(37);
  }
  if(!strcmp(command,"writing")) {
    HANDLE ready=ready_event();HANDLE worker=CreateThread(NULL,0,writing_worker,NULL,0,NULL);
    if(!ready || !worker || WaitForSingleObject(ready,5000)!=WAIT_OBJECT_0)return 77;
    ExitProcess(37); /* Real worker dies inside a WRITING publication. */
  }
  if(!strcmp(command,"abrupt")) {
    HANDLE ready=ready_event();if(!ready || !owned_durable_pending())return 78;
    SetEvent(ready);Sleep(10000);return 79;
  }
  return 80;
}
