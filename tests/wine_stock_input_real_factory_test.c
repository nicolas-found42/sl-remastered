/* Actual Wine factory/device construction. Owned private display only.
 * Full return snapshots are compared without masks. Original entry cannot be
 * observed inside Wine by this fixture; owned seam entry proofs are separate. */
#define main owned_mock_factory_main
#include "wine_stock_input_device_raw_test.c"
#undef main
#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
static struct snapshot actual_factory[3],actual_device[3];
static DWORD factory_outputs[3],device_outputs[3],release_counts[3];
static void write_bytes(const char* name,void* p,size_t n) {FILE* f=fopen(name,"wb");if(!f||fwrite(p,1,n,f)!=n||fclose(f))ExitProcess(80);}
static FILE* input_log;
static LRESULT CALLBACK private_wnd(HWND w,UINT m,WPARAM k,LPARAM l) {
 if((m==WM_KEYDOWN||m==WM_KEYUP)&&input_log) {fprintf(input_log,"message %u %lu\n",m,(DWORD)k);fflush(input_log);}
 return DefWindowProcA(w,m,k,l);
}
static void private_pump(void) {MSG m;while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}}
static int coherent_keyboard(void* child,HINSTANCE instance) {
 WNDCLASSA cls;HWND window;IDirectInputDevice8A* dev=child;DIPROPDWORD property;
 BYTE states[3][256];INPUT event;DIDEVICEOBJECTDATA events[16];DWORD n;unsigned i,j;HRESULT hr;
 memset(&cls,0,sizeof cls);cls.lpfnWndProc=private_wnd;cls.hInstance=instance;cls.lpszClassName="PrivateFactoryInput";
 if(!RegisterClassA(&cls))return 50;
 input_log=fopen("private-key.txt","wb");if(!input_log)return 51;
 window=CreateWindowExA(0,cls.lpszClassName,"Owned private keyboard",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,320,240,NULL,NULL,instance,NULL);
 if(!window)return 52;ShowWindow(window,SW_SHOWNORMAL);SetForegroundWindow(window);
 for(i=0;i<20;++i){private_pump();Sleep(10);}
 if(FAILED(IDirectInputDevice8_SetDataFormat(dev,&c_dfDIKeyboard))||FAILED(IDirectInputDevice8_SetCooperativeLevel(dev,window,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE)))return 53;
 memset(&property,0,sizeof property);property.diph.dwSize=sizeof property;property.diph.dwHeaderSize=sizeof property.diph;property.diph.dwHow=DIPH_DEVICE;property.dwData=16;
 if(FAILED(IDirectInputDevice8_SetProperty(dev,DIPROP_BUFFERSIZE,&property.diph))||FAILED(IDirectInputDevice8_Acquire(dev)))return 54;
 if(FAILED(IDirectInputDevice8_GetDeviceState(dev,256,states[0])))return 55;
 n=16;(void)IDirectInputDevice8_GetDeviceData(dev,sizeof events[0],events,&n,0);
 for(j=0;j<2;++j) {
  memset(&event,0,sizeof event);event.type=INPUT_KEYBOARD;event.ki.wScan=DIK_A;event.ki.dwFlags=KEYEVENTF_SCANCODE|(j?KEYEVENTF_KEYUP:0);
  if(SendInput(1,&event,sizeof event)!=1)return 56;
  for(i=0;i<100;++i){private_pump();Sleep(10);hr=IDirectInputDevice8_GetDeviceState(dev,256,states[j+1]);if(FAILED(hr))return 57;if((states[j+1][DIK_A]&0x80)==(j?0:0x80))break;}
  if(i==100)return 58;
  fprintf(input_log,"state %u %u\n",j+1,states[j+1][DIK_A]);
 }
 n=16;hr=IDirectInputDevice8_GetDeviceData(dev,sizeof events[0],events,&n,0);if(FAILED(hr))return 59;
 write_bytes("private-key-states.bin",states,sizeof states);write_bytes("private-key-buffer.bin",events,n*sizeof events[0]);
 fprintf(input_log,"buffer_count %lu\n",n);fflush(input_log);
 if(states[0][DIK_A] || n!=2 || events[0].dwOfs!=DIK_A || events[0].dwData!=0x80 || events[1].dwOfs!=DIK_A || events[1].dwData)return 60;
 if(FAILED(IDirectInputDevice8_Unacquire(dev)))return 61;
 DestroyWindow(window);fclose(input_log);input_log=NULL;return 0;
}
int main(int argc,char** argv) {
 HMODULE owner,proxy,real;hook_fn hook;init_fn initialize;create_fn direct,wrapped;stop_fn stop;
 void* instance=GetModuleHandleA(NULL);void* object=NULL;void* child=NULL;void* direct_device=NULL;
 unsigned i;int input_status;int repeat_factory,hook_factory,repeat_device,hook_device;
 if(argc!=2 || (strcmp(argv[1],"owner-first")&&strcmp(argv[1],"proxy-first")))return 2;
 owner=NULL;proxy=NULL;
 if(GetModuleHandleA("input-owner.dll")||GetModuleHandleA("stock-input-owned.dll"))return 10;
 if(!strcmp(argv[1],"proxy-first")){proxy=LoadLibraryA("Z:\\work\\stock-input-owned.dll");if(!proxy)return 11;}
 owner=LoadLibraryA("Z:\\work\\input-owner.dll");
 if(!proxy)proxy=LoadLibraryA("Z:\\work\\stock-input-owned.dll");
 write_bytes("actual-load-order.txt",argv[1],strlen(argv[1]));
 real=LoadLibraryA("C:\\windows\\syswow64\\dinput8.dll");if(!owner||!proxy||!real)return 3;
 hook=(void*)(uintptr_t)GetProcAddress(owner,"control_tape_test_hook");stop=(void*)(uintptr_t)GetProcAddress(owner,"control_tape_stop");
 initialize=(void*)(uintptr_t)GetProcAddress(proxy,"stock_input_test_initialize");
 direct=(void*)(uintptr_t)GetProcAddress(real,"DirectInput8Create");wrapped=(void*)(uintptr_t)GetProcAddress(proxy,"DirectInput8Create");
 {char path[MAX_PATH];DWORD count=GetModuleFileNameA(real,path,sizeof path);if(!count||count>=sizeof path)return 9;write_bytes("actual-module-path.bin",path,count);}
 if(!hook||!stop||!initialize||!direct||!wrapped)return 4;
 (void)hook(2,tick);if(!initialize("input-owner.dll",(void*)direct,"actual-setup.jsonl"))return 5;
 for(i=0;i<3;++i) {
  capture(i==2?(void*)wrapped:(void*)direct,instance,0x800,(void*)&IID_IDirectInput8A,(DWORD*)&object,2,&actual_factory[i]);
  if(actual_factory[i].regs[7]||!object)return 6;
  factory_outputs[i]=(DWORD)(uintptr_t)object;
  if(!i)direct_device=(*(void***)object)[3];
  if(i!=2)release_counts[i]=((ULONG(WINAPI*)(void*))(*(void***)object)[2])(object);
 }
 for(i=0;i<3;++i) {
  capture(i==2?(*(void***)object)[3]:direct_device,object,(DWORD)(uintptr_t)&GUID_SysKeyboard,&child,NULL,3,&actual_device[i]);
  if(actual_device[i].regs[7]||!child)return 7;
  device_outputs[i]=(DWORD)(uintptr_t)child;
  if(i!=2)(void)((ULONG(WINAPI*)(void*))(*(void***)child)[2])(child);
 }
 input_status=coherent_keyboard(child,(HINSTANCE)instance);if(input_status)return input_status;
 (void)((ULONG(WINAPI*)(void*))(*(void***)child)[2])(child);
 release_counts[2]=((ULONG(WINAPI*)(void*))(*(void***)object)[2])(object);
 write_bytes("actual-factory-return.bin",actual_factory,sizeof actual_factory);
 write_bytes("actual-device-return.bin",actual_device,sizeof actual_device);
 write_bytes("actual-outputs.bin",factory_outputs,sizeof factory_outputs);write_bytes("actual-device-outputs.bin",device_outputs,sizeof device_outputs);
 write_bytes("actual-release-counts.bin",release_counts,sizeof release_counts);
 repeat_factory=!memcmp(&actual_factory[0],&actual_factory[1],560);hook_factory=!memcmp(&actual_factory[0],&actual_factory[2],560);
 repeat_device=!memcmp(&actual_device[0],&actual_device[1],560);hook_device=!memcmp(&actual_device[0],&actual_device[2],560);
 printf("actual_Wine=1 factory_repeat=%d factory_hook=%d device_repeat=%d device_hook=%d\n",repeat_factory,hook_factory,repeat_device,hook_device);
 if(stop("actual-tape.jsonl")<0)return 8;
 return repeat_factory&&hook_factory&&repeat_device&&hook_device?0:44;
}
