/* Actual Wine DirectInput8 in an owned private Xvfb window. Never Stock.
 * Reuse the already-qualified raw caller/FX snapshot harness, but do NOT replace
 * actual Wine method entry or return flags. Unhooked repeats must settle first.
 */
#define DIRECTINPUT_VERSION 0x0800
#define COBJMACROS
#define main owned_input_fixture_main
#include "wine_input_observer_test.c"
#undef main
#include <dinput.h>

enum { REAL_CASES=8 };
static IDirectInput8A* real_owner;
static IDirectInputDevice8A* real_devices[2];
static struct snapshot real_snap[3][FXS][REAL_CASES];
static BYTE real_outputs[3][FXS][REAL_CASES][288];
static DWORD real_counts[3][FXS][REAL_CASES];
static FILE* real_log;
static DWORD real_sequence;
static HWND real_window;

static void hex_bytes(const void* value,unsigned size) {
  const BYTE* bytes=value;unsigned i;
  for(i=0;i<size;++i) fprintf(real_log,"%02x",bytes[i]);
}

static void pump(void) {
  MSG msg;
  while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageA(&msg);}
}
static void identity(const char* event,DWORD device,void* object) {
  void** vt=object ? *(void***)object : NULL;
  unsigned i,slots=strstr(event,"IUnknown")?3u:11u;
  fprintf(real_log,"{\"seq\":%lu,\"event\":\"%s\",\"device\":%lu,\"object\":%lu,\"vtable\":%lu,\"slots_read\":%u,\"methods\":[",
          ++real_sequence,event,device,(DWORD)(uintptr_t)object,(DWORD)(uintptr_t)vt,slots);
  for(i=0;i<slots;++i) fprintf(real_log,"%s%lu",i?",":"",vt?(DWORD)(uintptr_t)vt[i]:0);
  fputs("]}\n",real_log);fflush(real_log);
}
static void outcome(const char* method,DWORD device,HRESULT hr,DWORD revision) {
  fprintf(real_log,"{\"seq\":%lu,\"event\":\"method\",\"method\":\"%s\",\"device\":%lu,\"result\":%lu,\"revision\":%lu}\n",
          ++real_sequence,method,device,(DWORD)hr,revision);fflush(real_log);
}
static void query_aliases(IUnknown* object,DWORD device,int owner) {
  void* unknown=NULL;void* alias=NULL;void* canonical=NULL;HRESULT hr;
  hr=IUnknown_QueryInterface(object,&IID_IUnknown,&unknown);outcome("QI-IUnknown",device,hr,0);
  if(FAILED(hr)||!unknown) ExitProcess(81);
  identity("canonical-IUnknown",device,unknown);
  hr=IUnknown_QueryInterface(object,owner?&IID_IDirectInput8W:&IID_IDirectInputDevice8W,&alias);
  outcome("QI-unicode",device,hr,0);identity("unicode-interface",device,alias);
  if(FAILED(hr)||!alias) ExitProcess(82);
  hr=IUnknown_QueryInterface((IUnknown*)alias,&IID_IUnknown,&canonical);outcome("QI-unicode-IUnknown",device,hr,0);
  if(FAILED(hr)||canonical!=unknown) ExitProcess(83);
  identity("unicode-canonical-IUnknown",device,canonical);
  outcome("Release-canonical",device,(HRESULT)IUnknown_Release((IUnknown*)canonical),0);
  outcome("Release-unicode",device,(HRESULT)IUnknown_Release((IUnknown*)alias),0);
  outcome("Release-IUnknown",device,(HRESULT)IUnknown_Release((IUnknown*)unknown),0);
}
static void configure(unsigned n,DWORD revision) {
  IDirectInputDevice8A* dev=real_devices[n];HRESULT hr;
  DIPROPDWORD property;unsigned i;
  const DIDATAFORMAT* format=n?&c_dfDIMouse:&c_dfDIKeyboard;
  fprintf(real_log,"{\"seq\":%lu,\"event\":\"data-format\",\"device\":%u,\"revision\":%lu,\"size\":%lu,\"object_size\":%lu,\"flags\":%lu,\"data_size\":%lu,\"objects\":[",
          ++real_sequence,n+1,revision,format->dwSize,format->dwObjSize,format->dwFlags,format->dwDataSize);
  for(i=0;i<format->dwNumObjs;++i) {
    const DIOBJECTDATAFORMAT* object=&format->rgodf[i];
    fprintf(real_log,"%s{\"guid\":\"",i?",":"");
    if(object->pguid) hex_bytes(object->pguid,sizeof(GUID));
    fprintf(real_log,"\",\"offset\":%lu,\"type\":%lu,\"flags\":%lu}",object->dwOfs,object->dwType,object->dwFlags);
  }
  fputs("]}\n",real_log);
  hr=IDirectInputDevice8_SetDataFormat(dev,format);
  outcome("SetDataFormat",n+1,hr,revision);if(FAILED(hr)) ExitProcess(84);
  fprintf(real_log,"{\"seq\":%lu,\"event\":\"cooperative-configuration\",\"device\":%u,\"revision\":%lu,\"hwnd\":%lu,\"flags\":%u}\n",
          ++real_sequence,n+1,revision,(DWORD)(uintptr_t)real_window,DISCL_NONEXCLUSIVE|DISCL_FOREGROUND);
  hr=IDirectInputDevice8_SetCooperativeLevel(dev,real_window,DISCL_NONEXCLUSIVE|DISCL_FOREGROUND);
  outcome("SetCooperativeLevel",n+1,hr,revision);if(FAILED(hr)) ExitProcess(85);
  memset(&property,0,sizeof property);property.diph.dwSize=sizeof property;
  property.diph.dwHeaderSize=sizeof property.diph;property.diph.dwHow=DIPH_DEVICE;property.dwData=16;
  hr=IDirectInputDevice8_SetProperty(dev,DIPROP_BUFFERSIZE,&property.diph);
  outcome("SetProperty-buffer16",n+1,hr,revision);if(FAILED(hr)) ExitProcess(86);
  hr=IDirectInputDevice8_Acquire(dev);outcome("Acquire",n+1,hr,revision);if(FAILED(hr)) ExitProcess(87);
}
static void actual_pass(unsigned pass) {
  unsigned f,i,n;
  for(f=0;f<FXS;++f) {
    static const DWORD flags[4]={0x202,0x203,0x246,0x297};
    cw=(WORD)(0x037f+(f<<10));mxcsr=0x1f80+(f<<13);target_flags=flags[f];
    /* A disclosed owned-fixture priming read clears initial relative motion.
     * It is never used in the Stock adapter or counted as transparent replay. */
    for(n=0;n<2;++n) {
      HRESULT hr=IDirectInputDevice8_Acquire(real_devices[n]);
      BYTE scratch[256];
      outcome("pass-Acquire",n+1,hr,pass+1);if(FAILED(hr)) ExitProcess(88);
      hr=IDirectInputDevice8_GetDeviceState(real_devices[n],n?16:256,scratch);
      outcome("prime-state",n+1,hr,pass+1);if(FAILED(hr)) ExitProcess(89);
    }
    for(i=0;i<REAL_CASES;++i) {
      DWORD method=(i==2 || i==7)?2:((i==3 || i==4)?1:0);
      n=(i==1 || i==4 || i==6)?1:0;
      if(i==5) {
        unsigned j;
        for(j=0;j<2;++j) outcome("Unacquire",j+1,IDirectInputDevice8_Unacquire(real_devices[j]),pass+1);
      }
      memset(buffer,0xa5,sizeof buffer);memset(buffer+16,0x7e,256);count=1;
      memset(&real_snap[pass][f][i],0,sizeof real_snap[0][0][0]);
      capture((*(void***)real_devices[n])[method==0?9:(method==1?7:10)],real_devices[n],
              method==2?20:(n?16:256),buffer+16,&count,method,&real_snap[pass][f][i]);
      memcpy(real_outputs[pass][f][i],buffer,sizeof buffer);real_counts[pass][f][i]=count;
      outcome("captured-case",n+1,(HRESULT)real_snap[pass][f][i].regs[7],100*pass+10*f+i);
    }
  }
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command_line,int show) {
  WNDCLASSA cls;HRESULT hr;unsigned n,f,i,repeat_fail=0,hook_fail=0;int stopped;
  const struct parity_input_observer_v1* api;
  (void)previous;(void)command_line;(void)show;
  real_log=fopen("real-input-events.jsonl","wb");if(!real_log) return 70;
  fprintf(real_log,"{\"seq\":%lu,\"event\":\"header\",\"schema\":1,\"scope\":\"owned_real_Wine_DirectInput\",\"process_replay_qualified\":false}\n",++real_sequence);
  memset(&cls,0,sizeof cls);cls.lpfnWndProc=DefWindowProcA;cls.hInstance=instance;cls.lpszClassName="OwnedParityInput";
  if(!RegisterClassA(&cls)) return 71;
  real_window=CreateWindowExA(0,cls.lpszClassName,"Owned private input qualification",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                            10,10,600,400,NULL,NULL,instance,NULL);
  if(!real_window) return 72;
  ShowWindow(real_window,SW_SHOWNORMAL);UpdateWindow(real_window);
  SetForegroundWindow(real_window);SetActiveWindow(real_window);SetFocus(real_window);
  for(i=0;i<10;++i){pump();Sleep(20);}
  fprintf(real_log,"{\"seq\":%lu,\"event\":\"window\",\"hwnd\":%lu,\"foreground\":%lu,\"thread\":%lu,\"pid\":%lu}\n",
          ++real_sequence,(DWORD)(uintptr_t)real_window,(DWORD)(uintptr_t)GetForegroundWindow(),GetCurrentThreadId(),GetCurrentProcessId());
  if(GetForegroundWindow()!=real_window) return 73;
  hr=DirectInput8Create(instance,DIRECTINPUT_VERSION,&IID_IDirectInput8A,(void**)&real_owner,NULL);
  outcome("DirectInput8Create",0,hr,0);if(FAILED(hr)) return 74;
  identity("direct-input-owner",0,real_owner);query_aliases((IUnknown*)real_owner,0,1);
  for(n=0;n<2;++n) {
    DIDEVICEINSTANCEA info;HMODULE module;char path[MAX_PATH];void** vt;
    hr=IDirectInput8_CreateDevice(real_owner,n?&GUID_SysMouse:&GUID_SysKeyboard,&real_devices[n],NULL);
    outcome("CreateDevice",n+1,hr,0);if(FAILED(hr)) return 75;
    identity("device-created",n+1,real_devices[n]);query_aliases((IUnknown*)real_devices[n],n+1,0);
    vt=*(void***)real_devices[n];
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (const char*)vt[9],&module) || !GetModuleFileNameA(module,path,sizeof path)) return 76;
    fprintf(real_log,"{\"seq\":%lu,\"event\":\"state-target-module\",\"device\":%u,\"base\":%lu,\"path_hex\":\"",
            ++real_sequence,n+1,(DWORD)(uintptr_t)module);
    for(i=0;path[i];++i) fprintf(real_log,"%02x",(unsigned char)path[i]);
    fputs("\"}\n",real_log);
    memset(&info,0,sizeof info);info.dwSize=sizeof info;
    hr=IDirectInputDevice8_GetDeviceInfo(real_devices[n],&info);outcome("GetDeviceInfo",n+1,hr,0);
    if(FAILED(hr)) return 77;
    fprintf(real_log,"{\"seq\":%lu,\"event\":\"device-info\",\"device\":%u,\"instance_guid\":\"",++real_sequence,n+1);
    hex_bytes(&info.guidInstance,sizeof(GUID));fputs("\",\"product_guid\":\"",real_log);
    hex_bytes(&info.guidProduct,sizeof(GUID));fprintf(real_log,"\",\"device_type\":%lu}\n",info.dwDevType);
    configure(n,1);
  }
  actual_pass(0);actual_pass(1);
  tick_target=control_tape_test_hook(2,fake_tick);
  api=parity_input_observer(PARITY_INPUT_VERSION,sizeof *api);if(!api) return 78;
  for(n=0;n<2;++n) if(!api->register_device(real_devices[n],n+1,1,n?PARITY_INPUT_MOUSE:PARITY_INPUT_KEYBOARD)) return 79;
  actual_pass(2);
  for(f=0;f<FXS;++f)for(i=0;i<REAL_CASES;++i) {
    if(memcmp(&real_snap[0][f][i],&real_snap[1][f][i],sizeof real_snap[0][0][0]) ||
       memcmp(real_outputs[0][f][i],real_outputs[1][f][i],sizeof buffer) || real_counts[0][f][i]!=real_counts[1][f][i]) ++repeat_fail;
    if(memcmp(&real_snap[0][f][i],&real_snap[2][f][i],sizeof real_snap[0][0][0]) ||
       memcmp(real_outputs[0][f][i],real_outputs[2][f][i],sizeof buffer) || real_counts[0][f][i]!=real_counts[2][f][i]) ++hook_fail;
  }
  stopped=control_tape_stop("real-input-tape.jsonl");
  save("real-snapshots.bin",real_snap,sizeof real_snap);save("real-output.bin",real_outputs,sizeof real_outputs);
  save("real-counts.bin",real_counts,sizeof real_counts);
  fprintf(real_log,"{\"seq\":%lu,\"event\":\"complete\",\"cases\":%u,\"unhooked_repeat_differences\":%u,\"hooked_differences\":%u,\"stop\":%d}\n",
          ++real_sequence,FXS*REAL_CASES,repeat_fail,hook_fail,stopped);
  fclose(real_log);
  for(n=0;n<2;++n) {IDirectInputDevice8_Unacquire(real_devices[n]);IDirectInputDevice8_Release(real_devices[n]);}
  IDirectInput8_Release(real_owner);DestroyWindow(real_window);
  return repeat_fail || hook_fail || stopped<0;
}
