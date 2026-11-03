/* Actual A/W COM aliases/lifetimes. Same raw caller as the state-only matrix.
 * Canonical IUnknown is observed to be the W interface in this Wine build.
 * No arbitrary tear-off/aggregation/thread-concurrency claim follows.
 */
#define WinMain state_only_fixture_entry
#include "wine_real_input_test.c"
#undef WinMain

static struct snapshot com_snap[2][3][FXS][8];
static DWORD com_outputs[2][3][FXS][8];
static void* com_out;
static void* com_alias[2];
static void* com_replacement[2];
static void* com_replacement_alias[2];
static const struct parity_input_com_observer_v2* real_com;

static void com_pass(unsigned device,unsigned pass) {
  unsigned f,i;void* self;void* fn;
  for(f=0;f<FXS;++f) {
    static const DWORD flags[4]={0x202,0x203,0x246,0x297};
    cw=(WORD)(0x037f+(f<<10));mxcsr=0x1f80+(f<<13);target_flags=flags[f];
    for(i=0;i<8;++i) {
      DWORD method=i<3?0:(i==3?1:2);
      const GUID* iid=i==0?&IID_IDirectInputDevice8W:(i==1?&IID_IUnknown:&IID_IDirectInputDevice8A);
      self=(i==0 || i==4)?(void*)real_devices[device]:com_alias[device];
      fn=(*(void***)self)[method];com_out=(void*)0xaabbccddu;
      memset(&com_snap[device][pass][f][i],0,sizeof com_snap[0][0][0][0]);
      capture(fn,self,(DWORD)(uintptr_t)iid,&com_out,NULL,method?1:0,&com_snap[device][pass][f][i]);
      com_outputs[device][pass][f][i]=(DWORD)(uintptr_t)com_out;
      outcome("raw-COM-case",device+1,(HRESULT)com_snap[device][pass][f][i].regs[7],100*pass+10*f+i);
    }
  }
}

int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command_line,int show) {
  WNDCLASSA cls;HRESULT hr;unsigned n,f,i,repeat_fail=0,hook_fail=0;int stopped;
  int refusal=strstr(command_line,"refuse")!=NULL;
  (void)previous;(void)show;
  real_log=fopen("real-com-events.jsonl","wb");if(!real_log)return 70;
  fprintf(real_log,"{\"seq\":%lu,\"event\":\"header\",\"schema\":1,\"scope\":\"owned_real_Wine_COM\",\"refusal\":%s}\n",
          ++real_sequence,refusal?"true":"false");
  memset(&cls,0,sizeof cls);cls.lpfnWndProc=DefWindowProcA;cls.hInstance=instance;cls.lpszClassName="OwnedParityCOM";
  if(!RegisterClassA(&cls))return 71;
  real_window=CreateWindowExA(0,cls.lpszClassName,"Owned private COM qualification",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                            10,10,600,400,NULL,NULL,instance,NULL);
  if(!real_window)return 72;
  ShowWindow(real_window,SW_SHOWNORMAL);UpdateWindow(real_window);SetForegroundWindow(real_window);
  for(i=0;i<10;++i){pump();Sleep(20);}
  hr=DirectInput8Create(instance,DIRECTINPUT_VERSION,&IID_IDirectInput8A,(void**)&real_owner,NULL);
  outcome("DirectInput8Create",0,hr,0);if(FAILED(hr))return 73;
  for(n=0;n<(refusal?1u:2u);++n) {
    hr=IDirectInput8_CreateDevice(real_owner,n?&GUID_SysMouse:&GUID_SysKeyboard,&real_devices[n],NULL);
    outcome("CreateDevice",n+1,hr,0);if(FAILED(hr))return 74;
    hr=IDirectInputDevice8_QueryInterface(real_devices[n],&IID_IDirectInputDevice8W,&com_alias[n]);
    outcome("hold-Unicode",n+1,hr,0);if(FAILED(hr))return 75;
    identity("ANSI-before-hook",n+1,real_devices[n]);identity("Unicode-before-hook",n+1,com_alias[n]);
    hr=IDirectInput8_CreateDevice(real_owner,n?&GUID_SysMouse:&GUID_SysKeyboard,
                                 (IDirectInputDevice8A**)&com_replacement[n],NULL);
    outcome("CreateDevice-replacement-before-hooks",n+1,hr,0);if(FAILED(hr))return 93;
    hr=IUnknown_QueryInterface((IUnknown*)com_replacement[n],&IID_IDirectInputDevice8W,&com_replacement_alias[n]);
    if(FAILED(hr))return 94;
    if(!refusal){com_pass(n,0);com_pass(n,1);}
  }
  tick_target=control_tape_test_hook(2,fake_tick);
  if(parity_input_com_observer2(1,sizeof *real_com)||parity_input_com_observer2(2,1))return 76;
  real_com=parity_input_com_observer2(PARITY_INPUT_COM_VERSION2,sizeof *real_com);
  if(!real_com || real_com->identity!=PARITY_INPUT_COM_IDENTITY2)return 77;
  for(n=0;n<(refusal?1u:2u);++n) {
    void* recreated=NULL;void* new_alias=NULL;DWORD id=10+10*n;ULONG release;
    if(!real_com->register_device(real_devices[n],id,1,n?PARITY_INPUT_MOUSE:PARITY_INPUT_KEYBOARD))return 78;
    if(refusal) {
      if(!real_com->register_device(com_alias[n],99,1,PARITY_INPUT_KEYBOARD))return 79;
      if(real_com->prepare_interface(com_alias[n],&IID_IUnknown))return 80;
      if(!real_com->prepare_interface(com_alias[n],&IID_IDirectInputDevice8W))return 91;
      if(real_com->bind_interface(real_devices[n],com_alias[n],id,1))return 95;
      hr=IDirectInputDevice8_QueryInterface(real_devices[n],&IID_IDirectInputDevice8W,&new_alias);
      outcome("conflicting-alias-QI",n+1,hr,1);if(FAILED(hr)||new_alias!=com_alias[n])return 81;
      IUnknown_Release((IUnknown*)new_alias);
    } else {
      if(!real_com->prepare_interface(com_alias[n],&IID_IDirectInputDevice8W))return 82;
      if(!real_com->bind_interface(real_devices[n],com_alias[n],id,1))return 92;
      com_pass(n,2);
    }
    release=IUnknown_Release((IUnknown*)com_alias[n]);outcome("release-held-Unicode",n+1,(HRESULT)release,1);
    release=IDirectInputDevice8_Release(real_devices[n]);outcome("final-zero-Release",n+1,(HRESULT)release,1);
    if(release!=0)return 83;
    recreated=com_replacement[n];
    identity("recreated-ANSI",n+1,recreated);
    fprintf(real_log,"{\"seq\":%lu,\"event\":\"replacement-registration\",\"created_before_previous_retirement\":true,\"device\":%u,\"same_pointer\":%s}\n",
            ++real_sequence,n+1,recreated==(void*)real_devices[n]?"true":"false");
    if(refusal && real_com->register_device(recreated,id,1,PARITY_INPUT_KEYBOARD))return 86;
    if(!real_com->register_device(recreated,id,2,n?PARITY_INPUT_MOUSE:PARITY_INPUT_KEYBOARD))return 87;
    if(refusal && !real_com->prepare_interface(recreated,&IID_IDirectInputDevice8A))return 88;
    /* This replacement and its held sibling were created before hooking.
     * We test retirement/rebinding, not post-hook factory interception. */
    {
      void* sibling=com_replacement_alias[n];
      if(!real_com->bind_interface(recreated,sibling,id,2))return 96;
      hr=IUnknown_QueryInterface((IUnknown*)recreated,&IID_IDirectInputDevice8W,&new_alias);
      outcome("reincarnated-QI",n+1,hr,2);if(FAILED(hr))return 89;
      IUnknown_Release((IUnknown*)new_alias);IUnknown_Release((IUnknown*)sibling);
    }
    if(IUnknown_Release((IUnknown*)recreated)!=0)return 90;
  }
  stopped=control_tape_stop("real-com-tape.jsonl");
  if(!refusal)for(n=0;n<2;++n)for(f=0;f<FXS;++f)for(i=0;i<8;++i) {
    if(memcmp(&com_snap[n][0][f][i],&com_snap[n][1][f][i],sizeof com_snap[0][0][0][0]) ||
       com_outputs[n][0][f][i]!=com_outputs[n][1][f][i])++repeat_fail;
    if(memcmp(&com_snap[n][0][f][i],&com_snap[n][2][f][i],sizeof com_snap[0][0][0][0]) ||
       com_outputs[n][0][f][i]!=com_outputs[n][2][f][i])++hook_fail;
  }
  save("real-com-snapshots.bin",com_snap,sizeof com_snap);save("real-com-outputs.bin",com_outputs,sizeof com_outputs);
  fprintf(real_log,"{\"seq\":%lu,\"event\":\"complete\",\"cases\":%u,\"unhooked_repeat_differences\":%u,\"hooked_differences\":%u,\"stop\":%d}\n",
          ++real_sequence,refusal?0u:64u,repeat_fail,hook_fail,stopped);
  fclose(real_log);IDirectInput8_Release(real_owner);DestroyWindow(real_window);
  return repeat_fail||hook_fail||stopped<0;
}
