/* Owned COM-shaped methods only. No actual DirectInput/Wine COM claim. */
#define main input_state_fixture_main
#include "wine_input_observer_test.c"
#undef main

static void* com_vtable[11];
static void* object_page;
static DWORD references;
static const GUID iid_owned={0x11223344,0x5566,0x7788,{1,2,3,4,5,6,7,8}};
static const struct parity_input_com_observer_v1* com_api;
static struct snapshot com_baseline[FXS][12],com_hooked[FXS][12];
static DWORD output_baseline[FXS][12],output_hooked[FXS][12];

__attribute__((used,noinline)) static HRESULT WINAPI query_body(void* self,const GUID* iid,void** out) {
  (void)iid;
  if(scenario==4) {*out=(void*)0x12345678;SetLastError(0xabcd0004);return (HRESULT)0x80004002u;}
  *out=scenario==5 ? self : (BYTE*)object_page+16;
  ++references;SetLastError(0xabcd0000u+scenario);return 0;
}
__attribute__((used,noinline)) static ULONG WINAPI addref_body(void* self) {
  (void)self;SetLastError(0xabcd0000u+scenario);return ++references;
}
__attribute__((used,noinline)) static ULONG WINAPI release_body(void* self) {
  DWORD result=--references;(void)self;
  if(!result && !VirtualFree(object_page,0,MEM_RELEASE)) ExitProcess(30);
  SetLastError(0xabcd0000u+scenario);return result;
}
__attribute__((naked)) static void query_target(void) {
  __asm__ volatile("pushfl\n\tpopl _target_entry_flags\n\tpushl 12(%esp)\n\tpushl 12(%esp)\n\tpushl 12(%esp)\n\tcall _query_body@12\n\tpushl _target_flags\n\tpopfl\n\tret $12\n\t");
}
__attribute__((naked)) static void addref_target(void) {
  __asm__ volatile("pushfl\n\tpopl _target_entry_flags\n\tpushl 4(%esp)\n\tcall _addref_body@4\n\tpushl _target_flags\n\tpopfl\n\tret $4\n\t");
}
__attribute__((naked)) static void release_target(void) {
  __asm__ volatile("pushfl\n\tpopl _target_entry_flags\n\tpushl 4(%esp)\n\tcall _release_body@4\n\tpushl _target_flags\n\tpopfl\n\tret $4\n\t");
}
static void reset_objects(void) {
  void* memory=VirtualAlloc(object_page,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
  if(!memory || (object_page && memory!=object_page)) ExitProcess(31);
  object_page=memory;*(void***)object_page=com_vtable;*(void***)((BYTE*)object_page+16)=com_vtable;references=1;
}
static void com_pass(unsigned f,int hooked_pass) {
  unsigned i;
  reset_objects();
  if(hooked_pass && !com_api->register_device(object_page,10,2*f+1,PARITY_INPUT_KEYBOARD)) ExitProcess(32);
  for(i=0;i<12;++i) {
    DWORD method=(i==0 || i==4 || i==5 || i==10) ? 0 : ((i==3 || i==9 || i==11) ? 9 : (i==1 ? 1 : 2));
    void* originals[3]={query_target,addref_target,release_target};
    void* fn;
    void* self=(i==1 || i==3 || i==6 || i==8 || i==11) ? (BYTE*)object_page+16 : object_page;
    void* out=(void*)0xaabbccddu;
    struct snapshot* snap=hooked_pass ? &com_hooked[f][i] : &com_baseline[f][i];
    if(i==9) {
      reset_objects();
      if(hooked_pass && !com_api->register_device(object_page,10,2*f+2,PARITY_INPUT_KEYBOARD)) ExitProcess(33);
    }
    scenario=i;cw=(WORD)(0x037f+(f<<10));mxcsr=0x1f80+(f<<13);
    {static const DWORD flags[4]={0x202,0x203,0x246,0x297};target_flags=flags[f];}
    fn=hooked_pass ? com_vtable[method] : (method==9 ? (void*)fake_state : originals[method]);
    memset(snap,0,sizeof *snap);memset(buffer,0x7e,sizeof buffer);
    capture(fn,self,method==0 ? (DWORD)(uintptr_t)&iid_owned : 256,
            method==0 ? (void*)&out : buffer,NULL,method==0 || method==9 ? 0 : 1,snap);
    if(hooked_pass) output_hooked[f][i]=(DWORD)(uintptr_t)out;else output_baseline[f][i]=(DWORD)(uintptr_t)out;
  }
  if(hooked_pass && !com_api->retire_device(object_page,10,2*f+2)) ExitProcess(34);
  if(!VirtualFree(object_page,0,MEM_RELEASE)) ExitProcess(35);
}
int main(int argc,char** argv) {
  unsigned f,i,failures=0;
  int stopped,refuse=argc==2 && !strcmp(argv[1],"refuse");
  com_vtable[0]=query_target;com_vtable[1]=addref_target;com_vtable[2]=release_target;
  com_vtable[9]=fake_state;com_vtable[7]=fake_acquire;com_vtable[10]=fake_data;
  for(f=0;f<(refuse?1u:4u);++f) {
    com_pass(f,0);
    if(f==0) {
      tick_target=control_tape_test_hook(2,fake_tick);
      if(parity_input_com_observer(99,sizeof *com_api) || parity_input_com_observer(1,1)) return 2;
      com_api=parity_input_com_observer(PARITY_INPUT_COM_VERSION,sizeof *com_api);
      if(!com_api || com_api->identity!=PARITY_INPUT_COM_IDENTITY) return 3;
    }
    com_pass(f,1);
    for(i=0;i<12;++i) if(memcmp(&com_baseline[f][i],&com_hooked[f][i],sizeof com_baseline[0][0]) ||
                         output_baseline[f][i]!=output_hooked[f][i]) ++failures;
  }
  if(refuse) {
    struct snapshot scratch;
    reset_objects();
    /* Explicit retirement from com_pass covered both base and alias. A call on
     * the reused but unregistered alias must forward and retain a Gap. */
    scenario=9;
    capture(com_vtable[9],(BYTE*)object_page+16,256,buffer,NULL,0,&scratch);
    if(com_api->register_device(object_page,10,2,PARITY_INPUT_KEYBOARD)) return 36;
    if(!com_api->register_device(object_page,10,3,PARITY_INPUT_KEYBOARD)) return 37;
    capture(com_vtable[9],object_page,256,buffer,NULL,0,&scratch);
    capture(com_vtable[2],object_page,0,NULL,NULL,1,&scratch);
  }
  stopped=control_tape_stop("lifecycle-tape.jsonl");
  save("lifecycle-baseline.bin",com_baseline,sizeof com_baseline);save("lifecycle-hooked.bin",com_hooked,sizeof com_hooked);
  save("lifecycle-out-baseline.bin",output_baseline,sizeof output_baseline);save("lifecycle-out-hooked.bin",output_hooked,sizeof output_hooked);
  printf("scope=owned_console_only lifecycle_cases=%u failures=%u stop=%d freed_object_on_zero=1 same_pointer_reused=1\n",refuse?15u:48u,failures,stopped);
  return failures || stopped<0;
}
