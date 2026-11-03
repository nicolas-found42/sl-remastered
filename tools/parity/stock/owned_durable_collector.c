/* Owned GUI-subsystem collector: actual child process handle and shared view
 * survive target exit. No Stock executable, GUI window, audio or replay path.
 */
#include "owned_durable_api.h"
#include "durable_ring.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
static FILE *journal,*events;
static unsigned records,attach_count,detach_count,data_count,marker_count,late_count;
static unsigned char seen[OWNED_DURABLE_DATA_COUNT];
static uint32_t source_view,marker_ticket,late_ticket,marker_thread,late_thread;
static int semantic=1;
static FILE* create_new(const char* path) {
  HANDLE handle=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
  if(handle==INVALID_HANDLE_VALUE)return NULL;
  int fd=_open_osfhandle((intptr_t)handle,_O_WRONLY|_O_BINARY);
  if(fd<0) {CloseHandle(handle);return NULL;}
  FILE* stream=_fdopen(fd,"wb");
  if(!stream)_close(fd); /* fd owns the Win32 handle after successful adoption. */
  return stream;
}

static int archive(struct dr_view view,const struct dr_record* r,int frozen) {
  struct owned_durable_event event={0};
  if(r->length!=sizeof event)semantic=0;
  else {
    memcpy(&event,r->payload,sizeof event);
    if(!event.view_raw || !event.thread)semantic=0;
    if(!source_view)source_view=event.view_raw;
    if(source_view!=event.view_raw)semantic=0;
    if(r->kind==OWNED_ATTACH) {++attach_count;if(r->ticket!=1 || event.stage!=1)semantic=0;}
    else if(r->kind==OWNED_DETACH) {++detach_count;if(event.stage!=1)semantic=0;}
    else if(r->kind==OWNED_DATA) {
      ++data_count;
      if(event.index>=OWNED_DURABLE_DATA_COUNT || seen[event.index]++)semantic=0;
    } else if(r->kind==OWNED_EXIT_MARKER) {++marker_count;marker_ticket=r->ticket;marker_thread=event.thread;}
    else if(r->kind==OWNED_LATE) {++late_count;late_ticket=r->ticket;late_thread=event.thread;}
    else semantic=0;
  }
  if(fwrite(r,sizeof *r,1,journal)!=1 || fflush(journal)) {dr_archive_failed(view);return 0;}
  if(fprintf(events,"{\"event\":\"record\",\"frozen_tail\":%s,\"ticket\":%lu,\"generation\":%lu,\"kind\":%lu,\"length\":%lu,\"index\":%lu,\"thread\":%lu,\"stage\":%lu,\"view_raw\":%lu}\n",
       frozen?"true":"false",(unsigned long)r->ticket,(unsigned long)r->generation,
       (unsigned long)r->kind,(unsigned long)r->length,(unsigned long)event.index,
       (unsigned long)event.thread,(unsigned long)event.stage,(unsigned long)event.view_raw)<0 || fflush(events)) {
    dr_archive_failed(view);return 0;
  }
  ++records;return 1;
}
static int drain(struct dr_view view) {
  for(;;) {
    struct dr_record r;int status=dr_take(view,&r);
    if(status==DR_WAIT)return 1;
    if(status!=DR_OK)return 0;
    int written=archive(view,&r,0);
    if(dr_ack(view,&r,written?DR_ARCHIVE_WRITTEN:DR_ARCHIVE_FAILED)!=DR_OK)return 0;
  }
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR input,int show) {
  char mode[16],nonce_text[33],extra,map_name[96],child_nonce[33],child_map[96],ready_name[112];
  char child_path[MAX_PATH],command[512];uint32_t nonce[4];
  struct dr_view view={0};struct dr_summary frozen;HANDLE mapping,ready;
  STARTUPINFOA startup;PROCESS_INFORMATION process;DWORD since,waited,exit_code=STILL_ACTIVE;
  int runtime_ok=1,frozen_ok=0,forced=0,expected=0,negative;
  FILE* result;BOOL created;
  (void)instance;(void)previous;(void)show;
  if(sscanf(input,"%15s %32s %c",mode,nonce_text,&extra)!=2 || !owned_parse_nonce(nonce_text,nonce))return 51;
  if(strcmp(mode,"normal") && strcmp(mode,"late") && strcmp(mode,"pending") && strcmp(mode,"writing") &&
     strcmp(mode,"abrupt") && strcmp(mode,"missing") && strcmp(mode,"nonce"))return 52;
  negative=!strcmp(mode,"missing") || !strcmp(mode,"nonce");
  if(snprintf(map_name,sizeof map_name,"Local\\slrr-durable-%s",nonce_text)!=51)return 53;
  memcpy(child_nonce,nonce_text,33);memcpy(child_map,map_name,52);
  if(negative)child_nonce[0]=child_nonce[0]=='0'?'1':'0';
  if(!strcmp(mode,"missing"))memcpy(child_map+19,child_nonce,33);
  view.bytes=dr_required_bytes(OWNED_DURABLE_SLOTS);
  mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,view.bytes,map_name);
  if(!mapping || GetLastError()==ERROR_ALREADY_EXISTS)return 54;
  view.memory=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,view.bytes);
  if(!view.memory || dr_init(view,OWNED_DURABLE_SLOTS,nonce)!=DR_OK)return 55;
  memcpy(ready_name,map_name,52);memcpy(ready_name+51,"-ready",7);
  ready=CreateEventA(NULL,TRUE,FALSE,ready_name);if(!ready || GetLastError()==ERROR_ALREADY_EXISTS)return 56;
  if(!SetEnvironmentVariableA("SLRR_DURABLE_MAP_NAME",child_map) ||
     !SetEnvironmentVariableA("SLRR_DURABLE_NONCE",child_nonce))return 57;
  DWORD length=GetModuleFileNameA(NULL,child_path,sizeof child_path);char* slash;
  if(!length || length>=sizeof child_path || !(slash=strrchr(child_path,'\\')) ||
     (size_t)(slash-child_path)+sizeof "\\owned-durable-child.exe">sizeof child_path)return 58;
  strcpy(slash+1,"owned-durable-child.exe");
  int count=snprintf(command,sizeof command,"\"%s\" %s",child_path,negative?"normal":mode);
  if(count<0 || count>=(int)sizeof command)return 59;
  journal=create_new("journal.bin");if(!journal)return 60;
  events=create_new("events.jsonl");if(!events) {fclose(journal);return 60;}
  result=create_new("result.json");if(!result) {fclose(journal);fclose(events);return 60;}
  memset(&startup,0,sizeof startup);startup.cb=sizeof startup;memset(&process,0,sizeof process);
  created=CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&process);
  if(!created)return 61;
  CloseHandle(process.hThread);since=GetTickCount();
  for(;;) {
    if(!drain(view)) {runtime_ok=0;break;}
    if(!strcmp(mode,"abrupt") && !forced && WaitForSingleObject(ready,0)==WAIT_OBJECT_0) {
      if(!TerminateProcess(process.hProcess,39)) {runtime_ok=0;break;}
      forced=1;
    }
    waited=WaitForSingleObject(process.hProcess,0);
    if(waited==WAIT_OBJECT_0)break;
    if(waited!=WAIT_TIMEOUT || GetTickCount()-since>20000) {runtime_ok=0;break;}
    Sleep(1);
  }
  if(WaitForSingleObject(process.hProcess,0)!=WAIT_OBJECT_0) {
    runtime_ok=0;
    if(TerminateProcess(process.hProcess,98))WaitForSingleObject(process.hProcess,5000);
  }
  waited=WaitForSingleObject(process.hProcess,0);
  if(waited==WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess,&exit_code)) {
    if(!drain(view))runtime_ok=0;
    frozen_ok=dr_frozen_summary(view,&frozen)==DR_OK;
    if(frozen_ok)for(uint32_t ticket=frozen.ack_ticket;ticket<frozen.next_ticket;++ticket) {
      struct dr_record r;
      if(dr_frozen_record(view,ticket,&r)==DR_OK && !archive(view,&r,1))runtime_ok=0;
    }
  } else runtime_ok=0;
  if(frozen_ok && runtime_ok && semantic && !frozen.flags && !frozen.collector_lock) {
    if(negative) {
      /* Installed MinGW ntstatus.h STATUS_DLL_INIT_FAILED: exact refusal, not
       * any arbitrary missing-executable/timeout/nonzero result. */
      expected=exit_code==0xc0000142u && records==0 && frozen.next_ticket==1 && frozen.ack_ticket==1;
    } else if(!strcmp(mode,"normal")) {
      expected=exit_code==0 && records==130 && attach_count==1 && detach_count==1 &&
          data_count==OWNED_DURABLE_DATA_COUNT && !marker_count && !late_count &&
          frozen.next_ticket==131 && frozen.ack_ticket==131;
      for(unsigned i=0;i<OWNED_DURABLE_DATA_COUNT;++i)expected&=seen[i]==1;
    } else if(!strcmp(mode,"late")) {
      expected=exit_code==0 && records==4 && attach_count==1 && detach_count==1 && !data_count &&
          marker_count==1 && late_count==1 && marker_ticket==2 && late_ticket==3 &&
          marker_thread!=late_thread && frozen.next_ticket==5 && frozen.ack_ticket==5;
    } else if(!strcmp(mode,"abrupt")) {
      expected=forced && exit_code==39 && records==1 && attach_count==1 && !detach_count &&
          frozen.next_ticket==3 && frozen.ack_ticket==2 && frozen.reserved==1 && !frozen.committed;
    } else {
      expected=exit_code==37 && records==2 && attach_count==1 && detach_count==1 &&
          frozen.next_ticket==4 && frozen.ack_ticket==2 && frozen.committed==1 &&
          ((!strcmp(mode,"pending") && frozen.reserved==1 && !frozen.writing) ||
           (!strcmp(mode,"writing") && frozen.writing==1 && !frozen.reserved));
    }
    if(frozen.incomplete || frozen.reading)expected=0;
  }
  int journal_closed=fclose(journal),events_closed=fclose(events);
  if(journal_closed || events_closed) {dr_archive_failed(view);runtime_ok=0;expected=0;}
  if(frozen_ok && dr_frozen_summary(view,&frozen)!=DR_OK) {frozen_ok=0;expected=0;}
  if(frozen_ok) {
    FILE* raw=create_new("mapping-final.bin");int copied=0,closed=0;
    if(raw) {
      copied=fwrite(view.memory,view.bytes,1,raw)==1 && fflush(raw)==0;
      closed=fclose(raw)==0;
    }
    if(!copied || !closed) {dr_archive_failed(view);runtime_ok=0;expected=0;}
    if(dr_frozen_summary(view,&frozen)!=DR_OK) {frozen_ok=0;expected=0;}
  }
  fprintf(result,"{\"schema\":1,\"scope\":\"owned PE32 mapping lifetime only\",\"mode\":\"%s\",\"nonce\":\"%s\",\"child_nonce\":\"%s\",\"mapping\":\"Local\\\\slrr-durable-%s\",\"child_mapping\":\"Local\\\\slrr-durable-%s\",\"child_pid\":%lu,\"wait_result\":%lu,\"exit_code\":%lu,\"forced\":%s,\"runtime_ok\":%s,\"semantic_ok\":%s,\"records\":%u,\"collector_view_raw\":%lu,\"producer_view_raw\":%lu,\"frozen\":",
    mode,nonce_text,child_nonce,nonce_text,child_map+19,(unsigned long)process.dwProcessId,(unsigned long)waited,
    (unsigned long)exit_code,forced?"true":"false",runtime_ok?"true":"false",semantic?"true":"false",
    records,(unsigned long)(uintptr_t)view.memory,(unsigned long)source_view);
  if(frozen_ok)fprintf(result,"{\"next_ticket\":%lu,\"ack_ticket\":%lu,\"flags\":%lu,\"collector_lock\":%lu,\"reserved\":%lu,\"writing\":%lu,\"committed\":%lu,\"reading\":%lu,\"incomplete\":%lu}",
    (unsigned long)frozen.next_ticket,(unsigned long)frozen.ack_ticket,(unsigned long)frozen.flags,
    (unsigned long)frozen.collector_lock,(unsigned long)frozen.reserved,(unsigned long)frozen.writing,
    (unsigned long)frozen.committed,(unsigned long)frozen.reading,(unsigned long)frozen.incomplete);
  else fputs("null",result);
  fprintf(result,",\"owned_expectations_met\":%s,\"target_abi_qualified\":false,\"process_replay_qualified\":false}\n",expected?"true":"false");
  if(fclose(result))expected=0;
  CloseHandle(process.hProcess);CloseHandle(ready);UnmapViewOfFile(view.memory);CloseHandle(mapping);
  return expected?0:62;
}
