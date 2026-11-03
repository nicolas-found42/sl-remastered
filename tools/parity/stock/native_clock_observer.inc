/* Observe the original Native Float32 bridge after its FSTP at00420090.
 * Only the exact currentTime callee is recorded. No observer FPU arithmetic or
 * interpretation of the backend's opaque FXSAVE layout is needed. */
#define NATIVE_CLOCK_CAPACITY 4096u
struct native_clock_record {
  volatile LONG state;
  DWORD ordinal, phase, thread, lane, lane_read, caller, last_error;
  DWORD callee, value_f32_bits;
  BYTE fx[512];
};
static struct native_clock_record native_clock_records[NATIVE_CLOCK_CAPACITY];
static volatile LONG native_clock_returns;
static int native_clock_installed;
static DWORD native_clock_protection;
static void* native_clock_resume __attribute__((used));
static void native_clock_stub(void);

__attribute__((used,noinline)) static void native_clock_return(DWORD* regs,const BYTE* fx) {
  DWORD error=error_get();
  const DWORD* stack=(const DWORD*)(uintptr_t)(regs[3]+4);
  DWORD callee=stack[8],value=stack[23];
  struct lane* lane;
  struct native_clock_record* record;
  LONG ordinal;
  if(callee!=image_base+0x7bfe0 || !observation_enter()) goto done;
  lane=get_lane();
  if(!lane) {drop();goto leave;}
  ordinal=InterlockedIncrement(&native_clock_returns);
  if(ordinal<=0) {drop();goto leave;}
  record=&native_clock_records[((DWORD)ordinal-1u)%NATIVE_CLOCK_CAPACITY];
  if(InterlockedCompareExchange(&record->state,1,0)) {drop();goto leave;}
  record->ordinal=(DWORD)ordinal;
  record->phase=(DWORD)InterlockedIncrement(&next_phase);
  if((LONG)record->phase<=0) drop();
  record->thread=(DWORD)lane->id;record->lane=(DWORD)(lane-lanes);
  record->lane_read=++lane->reads;
  record->caller=image_base+0x20090;
  record->callee=callee;record->value_f32_bits=value;
  record->last_error=error;
  memcpy(record->fx,fx,sizeof record->fx);
  InterlockedExchange(&record->state,2);
leave:
  observation_leave();
done:
  dirty_fx();error_set(error);
}
__attribute__((naked)) static void native_clock_stub(void) {
  __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tmovl %esp,%edi\n\txorl %eax,%eax\n\tmovl $128,%ecx\n\trep stosl\n\tfxsave (%esp)\n\tmovl %esp,%eax\n\tsubl $8,%esp\n\tpushl %eax\n\tpushl %ebx\n\tcall _native_clock_return\n\taddl $16,%esp\n\tfxrstor (%esp)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tmovl 0x5c(%esp),%ebp\n\taddl $4,%esp\n\tjmp *_native_clock_resume\n\t");
}
/* Exact pinned wrapper/scale identify the selected callee. The original bridge
 * CALL and FSTP remain untouched; replay only MOV EBP,[ESP+5c]; ADD ESP,4. */
static int native_clock_prepare(BYTE* base,DWORD size) {
  static const BYTE expected[]={0xe8,0xdb,0x56,0x0d,0,0xd8,0x0d,0xa4,0x10,0x5f,0,0xc3,0x90,0x90,0x90,0x90};
  static const BYTE bridge[]={0xff,0x54,0x24,0x20,0xd9,0x5c,0x24,0x5c,0x8b,0x6c,0x24,0x5c,0x83,0xc4,0x04};
  return size==0x3c0000 && (uintptr_t)base==0x400000 &&
    !memcmp(base+0x7bfe0,expected,sizeof expected) &&
    !memcmp(base+0x2008c,bridge,sizeof bridge) &&
    *(const DWORD*)(base+0x1f10a4)==0x3a83126f;
}
static int native_clock_make_writable(BYTE* base) {
  return VirtualProtect(base+0x20094,7,PAGE_EXECUTE_READWRITE,&native_clock_protection)!=0;
}
static int native_clock_restore_protection(BYTE* base) {
  DWORD ignored;
  return VirtualProtect(base+0x20094,7,native_clock_protection,&ignored)!=0;
}
static void native_clock_publish(BYTE* base) {
  BYTE* tail=base+0x20094;
  native_clock_resume=base+0x2009b;
  tail[0]=0xe9;
  *(DWORD*)(tail+1)=(DWORD)(uintptr_t)native_clock_stub-(DWORD)(uintptr_t)(tail+5);
  tail[5]=tail[6]=0x90;
}
static int native_clock_finish_publication(BYTE* base) {
  int ok=FlushInstructionCache(GetCurrentProcess(),base+0x20094,7)!=0;
  return native_clock_restore_protection(base) && ok;
}
static int native_clock_flush(FILE* f) {
  unsigned i,j;
  for(i=0;i<NATIVE_CLOCK_CAPACITY;++i) {
    struct native_clock_record* r=&native_clock_records[i];
    if(InterlockedCompareExchange(&r->state,3,2)!=2) continue;
    fprintf(f,"{\"event\":\"native_clock_return\",\"native\":\"java.lang.System.currentTime()F\",\"ordinal\":%lu,\"phase\":%lu,\"thread\":%lu,\"lane\":%lu,\"lane_read\":%lu,\"role\":\"unresolved\",\"caller\":%lu,\"callee\":%lu,\"value_f32_bits\":%lu,\"last_error\":%lu,\"fxsave_hex\":\"",r->ordinal,r->phase,r->thread,r->lane,r->lane_read,r->caller,r->callee,r->value_f32_bits,r->last_error);
    for(j=0;j<sizeof r->fx;++j) fprintf(f,"%02x",r->fx[j]);
    fputs("\"}\n",f);
    if(ferror(f)||fflush(f)) {InterlockedExchange(&r->state,2);return 0;}
    InterlockedExchange(&r->state,0);
  }
  return 1;
}
