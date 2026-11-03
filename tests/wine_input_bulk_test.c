/* Owned bulk20 keyboard buffer transparency. No real device or replay. */
#define main old_owned_main
#include "wine_input_observer_test.c"
#undef main
static BYTE bulk[400],bulk_outputs[3][3][400];
static DWORD bulk_counts[3][3];
static struct snapshot bulk_snapshots[3][3];
static const DWORD accepted_counts[]={1,2,16},refused_counts[]={0,17,0xffffffffu};
__attribute__((used,noinline)) static HRESULT WINAPI bulk_data_body(void* self,DWORD size,void* out,DWORD* n,DWORD flags) {
 HRESULT hr=fake_data_body(self,size,out,n,flags);
#ifdef BULK_RETURN_OVERFLOW
 *n=17;
#endif
 return hr;
}
__attribute__((naked)) static void bulk_data(void) {
 __asm__ volatile("pushfl\n\tpopl _target_entry_flags\n\tpushl 20(%esp)\n\tpushl 20(%esp)\n\tpushl 20(%esp)\n\tpushl 20(%esp)\n\tpushl 20(%esp)\n\tcall _bulk_data_body@20\n\tpushl _target_flags\n\tpopfl\n\tret $20\n\t");
}
int main(int argc,char** argv) {
 const struct parity_input_observer_v1* api;unsigned pass,i;int refused;DWORD requested;
 if(argc!=2)return 2;refused=!strcmp(argv[1],"proxy-first");
 memset(vtables,0,sizeof vtables);vtables[0][7]=fake_acquire;vtables[0][9]=fake_state;vtables[0][10]=bulk_data;devices[0].vtable=vtables[0];devices[0].size=256;
 scenario=7;
 for(pass=0;pass<3;++pass) {
  if(pass==2) {(void)control_tape_test_hook(2,fake_tick);api=parity_input_observer(1,sizeof *api);if(!api||!api->register_device(&devices[0],1,1,1))return 3;}
  for(i=0;i<3;++i) {
   requested=refused?refused_counts[i]:accepted_counts[i];count=requested;
   memset(bulk,0x7e,sizeof bulk);memset(bulk,0xa5,16);memset(bulk+384,0xa5,16);
   capture(pass==2?vtables[0][10]:(void*)bulk_data,&devices[0],20,bulk+16,&count,2,&bulk_snapshots[pass][i]);
   memcpy(bulk_outputs[pass][i],bulk,sizeof bulk);bulk_counts[pass][i]=count;
  }
 }
 save("bulk-raw.bin",bulk_snapshots,sizeof bulk_snapshots);save("bulk-output.bin",bulk_outputs,sizeof bulk_outputs);save("bulk-count.bin",bulk_counts,sizeof bulk_counts);
 if(control_tape_stop("bulk-tape.jsonl")<0)return 4;
 for(i=0;i<3;++i)if(memcmp(&bulk_snapshots[0][i],&bulk_snapshots[1][i],560)||memcmp(&bulk_snapshots[0][i],&bulk_snapshots[2][i],560)||
  memcmp(bulk_outputs[0][i],bulk_outputs[1][i],400)||memcmp(bulk_outputs[0][i],bulk_outputs[2][i],400)||bulk_counts[0][i]!=bulk_counts[1][i]||bulk_counts[0][i]!=bulk_counts[2][i])return 5;
 printf("bulk20_raw_equal=1 cases=3 refused=%d counts=%lu,%lu,%lu\n",refused,refused?refused_counts[0]:accepted_counts[0],refused?refused_counts[1]:accepted_counts[1],refused?refused_counts[2]:accepted_counts[2]);return 0;
}
