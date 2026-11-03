/* Cold configuration refusal controls. No actual device or method is called. */
#define main prior_state_fixture_main
#include "wine_input_observer_test.c"
#undef main

static void* alias_tables[2][11];
static struct device base_object,alias_object,foreign_object,unknown_object;
static HRESULT WINAPI alias_qi(void* self,const GUID* iid,void** out) {
  (void)self;(void)iid;(void)out;return (HRESULT)0x80004002u;
}
static ULONG WINAPI alias_ref(void* self) {(void)self;return 1;}

int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show) {
  static const GUID iid_w={0x54d41081,0xdc15,0x4833,{0xa4,0x1b,0x74,0x8f,0x73,0xa3,0x81,0x79}};
  static const GUID unknown={0};
  const struct parity_input_com_observer_v2* api;
  FILE* f;unsigned i,failures=0;int result,expected,stopped;DWORD error;void* saved;
  (void)instance;(void)previous;(void)command;(void)show;
  for(i=0;i<2;++i) {
    alias_tables[i][0]=alias_qi;alias_tables[i][1]=alias_ref;alias_tables[i][2]=alias_ref;
    alias_tables[i][7]=fake_acquire;alias_tables[i][9]=fake_state;alias_tables[i][10]=fake_data;
  }
  base_object.vtable=alias_tables[0];alias_object.vtable=alias_tables[1];
  foreign_object.vtable=alias_tables[1];unknown_object.vtable=alias_tables[1];
  tick_target=control_tape_test_hook(2,fake_tick);
  api=parity_input_com_observer2(PARITY_INPUT_COM_VERSION2,sizeof *api);if(!api)return 60;
  f=fopen("cold-guards.jsonl","wb");if(!f)return 62;
#define CHECK(NAME,EXPRESSION,EXPECTED) do { \
  SetLastError(0x31415926);result=(EXPRESSION);expected=(EXPECTED);error=GetLastError(); \
  fprintf(f,"{\"case\":\"%s\",\"result\":%d,\"expected\":%d,\"last_error\":%lu}\n",NAME,result,expected,error); \
  if(result!=expected || error!=0x31415926)++failures; \
} while(0)
  CHECK("register-base",api->register_device(&base_object,1,1,PARITY_INPUT_KEYBOARD),1);
  CHECK("prepare-W",api->prepare_interface(&alias_object,&iid_w),1);
  CHECK("bind-W",api->bind_interface(&base_object,&alias_object,1,1),1);
  CHECK("idempotent-binding",api->bind_interface(&base_object,&alias_object,1,1),1);
  alias_object.vtable=alias_tables[0];
  CHECK("changed-vtable-refused",api->bind_interface(&base_object,&alias_object,1,1),0);
  alias_object.vtable=alias_tables[1];
  CHECK("restored-binding",api->bind_interface(&base_object,&alias_object,1,1),1);
  saved=alias_tables[1][1];alias_tables[1][1]=alias_ref;
  CHECK("changed-target-cell-refused",api->bind_interface(&base_object,&alias_object,1,1),0);
  alias_tables[1][1]=saved;
  CHECK("restored-target-cell",api->bind_interface(&base_object,&alias_object,1,1),1);
  CHECK("register-foreign",api->register_device(&foreign_object,2,1,PARITY_INPUT_KEYBOARD),1);
  CHECK("conflicting-id-refused",api->bind_interface(&base_object,&foreign_object,1,1),0);
  CHECK("unknown-base-refused",api->bind_interface(&unknown_object,&alias_object,1,1),0);
  CHECK("unknown-IID-refused",api->prepare_interface(&alias_object,&unknown),0);
  CHECK("retire-base-and-alias",api->retire_device(&base_object,1,1),1);
  CHECK("stale-incarnation-refused",api->register_device(&base_object,1,1,PARITY_INPUT_KEYBOARD),0);
  CHECK("new-incarnation",api->register_device(&base_object,1,2,PARITY_INPUT_KEYBOARD),1);
  CHECK("rebind-new-incarnation",api->bind_interface(&base_object,&alias_object,1,2),1);
#undef CHECK
  stopped=control_tape_stop("cold-guards-tape.jsonl");fclose(f);
  printf("cold_guard_cases=16 failures=%u stop=%d\n",failures,stopped);
  return failures?61:0;
}
