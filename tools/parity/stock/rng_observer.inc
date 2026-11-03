/* Included by the one recorder owner. FUN_005d73fb / FUN_005d7408, all.c
 * 379185. The original CALL to 005dd8d3 is untouched. These observation phases
 * enclose only the state update after that getter, not allocation/failure.
 * Numeric TIDs/blocks are raw diagnostics, not qualified lifetime identities.
 */
static const BYTE rng_seed_bytes[]={0xe8,0xd3,0x64,0,0,0x8b,0x4c,0x24,4,0x89,0x48,0x14,0xc3};
static const BYTE rng_draw_bytes[]={0xe8,0xc6,0x64,0,0,0x8b,0x48,0x14,0x69,0xc9,0xfd,0x43,3,0,0x81,0xc1,0xc3,0x9e,0x26,0,0x89,0x48,0x14,0x8b,0xc1,0xc1,0xe8,0x10,0x25,0xff,0x7f,0,0,0xc3};
static DWORD rng_page_protection;
static void rng_seed_observed(void),rng_draw_observed(void);
static void rng_seed_plain(void),rng_draw_plain(void);

static DWORD rng_guard(DWORD kind,DWORD caller,DWORD pointer,DWORD native_thread,struct lane* lane) {
  DWORD target=kind==KINDS?0x1d73fb:0x1d7408;
  if(caller<image_base+5 || caller-image_base>=image_size) return 10;
  /* Accept only a direct CALL to the exact guarded CRT entry. Indirect/tail
   * callers remain gaps until independently inspected, never broad bypasses. */
  if(*(const BYTE*)(uintptr_t)(caller-5)!=0xe8 ||
     caller+*(const DWORD*)(uintptr_t)(caller-4)!=image_base+target) return 11;
  if(pointer<0x10000 || (pointer&3) || !writable(pointer,0x74)) return 12;
  /* Getter initializes +0 with GetCurrentThreadId. Do not call it ourselves. */
  if(*(const DWORD*)(uintptr_t)pointer!=native_thread) return 13;
  if(lane->rng_tls && lane->rng_tls!=pointer) return 14;
  return 0;
}

__attribute__((used,noinline)) static void* rng_before(DWORD* regs,const BYTE* fx,DWORD kind) {
  DWORD error=error_get();
  DWORD* stack=(DWORD*)(uintptr_t)(regs[3]+4);
  struct lane* t;
  struct event* e;
  LONG seq;
  void* tail=kind==KINDS?(void*)rng_seed_plain:(void*)rng_draw_plain;
  if(!observation_enter()) { dirty_fx(); error_set(error); return tail; }
  t=get_lane();
  if(!t || t->depth==DEPTH) { drop(); goto refused; }
  seq=InterlockedIncrement(&next_sequence);
  if(seq<=0) { drop(); goto refused; }
  e=&events[((DWORD)seq-1u)%CAPACITY];
  if(InterlockedCompareExchange(&e->state,1,0)!=0) { drop(); goto refused; }
  e->entry_phase=(DWORD)InterlockedIncrement(&next_phase);
  if((LONG)e->entry_phase<=0) drop();
  e->sequence=(DWORD)seq; e->native_thread=(DWORD)t->id;
  e->lane=(DWORD)(t-lanes); e->lane_read=++t->reads;
  e->kind=kind; e->caller=stack[0]; e->arg=kind==KINDS?stack[1]:0;
  e->error_before=error; e->rng_tls=regs[7];
  e->rng_before=e->rng_after=e->returned=e->return_phase=e->result=e->error_after=0;
  e->output_valid=e->output[0]=e->output[1]=0;
  e->cw_after=0; e->mxcsr_after=0;
  e->rng_flags[0]=regs[8]; e->rng_flags[1]=0;
  memcpy(&e->cw_before,fx,2); memcpy(&e->mxcsr_before,fx+24,4);
  e->gap=rng_guard(kind,e->caller,e->rng_tls,e->native_thread,t);
#ifdef CONTROL_TAPE_TEST
  if(test_bypass_guards) e->gap=0;
#endif
  if(e->gap) {
    InterlockedIncrement(&gap_count); InterlockedExchange(&e->state,2);
    goto refused;
  }
  e->rng_before=*(const DWORD*)(uintptr_t)(e->rng_tls+0x14);
  t->rng_tls=e->rng_tls;
  t->frame[t->depth].caller=stack[0]; t->frame[t->depth++].event=e;
  tail=kind==KINDS?(void*)rng_seed_observed:(void*)rng_draw_observed;
  dirty_fx(); error_set(error); return tail;
refused:
  observation_leave(); dirty_fx(); error_set(error); return tail;
}

__attribute__((used,noinline)) static void rng_after(DWORD* regs,const BYTE* fx) {
  DWORD error=error_get();
  struct lane* t=get_lane();
  struct event* e;
  if(!t || !t->depth || t->frame[t->depth-1].event->kind<KINDS) {
    /* Incomplete publication stays visibly invalid. Never pop another call. */
    drop(); goto done;
  }
  e=t->frame[--t->depth].event;
  e->return_phase=(DWORD)InterlockedIncrement(&next_phase);
  if((LONG)e->return_phase<=0) drop();
  e->result=regs[7]; e->error_after=error; e->rng_flags[1]=regs[8];
  e->rng_after=*(const DWORD*)(uintptr_t)(e->rng_tls+0x14);
  memcpy(&e->cw_after,fx,2); memcpy(&e->mxcsr_after,fx+24,4);
  e->returned=(DWORD)InterlockedIncrement(&next_return);
  InterlockedExchange(&e->state,2); observation_leave();
done:
  dirty_fx(); error_set(error);
}

/* The entire straight-line original tail is represented by exact bytes, with
 * no relocated operand. Both observed/plain paths preserve cdecl caller cleanup.
 * The accepted path gets a post observer; refusal executes the same body and
 * returns directly, without corrupting the nesting stack of another call.
 */
#define RNG_SEED_BODY \
  ".byte 0x8b,0x4c,0x24,0x04,0x89,0x48,0x14\n\t"
#define RNG_DRAW_BODY \
  ".byte 0x8b,0x48,0x14,0x69,0xc9,0xfd,0x43,0x03,0x00,0x81,0xc1,0xc3,0x9e,0x26,0x00,0x89,0x48,0x14,0x8b,0xc1,0xc1,0xe8,0x10,0x25,0xff,0x7f,0x00,0x00\n\t"
__attribute__((naked)) static void rng_seed_plain(void) { __asm__ volatile(RNG_SEED_BODY "ret\n\t"); }
__attribute__((naked)) static void rng_draw_plain(void) { __asm__ volatile(RNG_DRAW_BODY "ret\n\t"); }
__attribute__((naked)) static void rng_seed_observed(void) { __asm__ volatile(RNG_SEED_BODY "jmp _rng_post_stub\n\t"); }
__attribute__((naked)) static void rng_draw_observed(void) { __asm__ volatile(RNG_DRAW_BODY "jmp _rng_post_stub\n\t"); }

#ifdef RNG_OMIT_FXRESTORE
#define RNG_FXRESTORE ""
#else
#define RNG_FXRESTORE "fxrstor (%esp)\n\t"
#endif
#define RNG_PRE_STUB(NAME,KIND) \
__attribute__((naked)) static void NAME(void) { \
  __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tmovl %esp,%eax\n\tsubl $4,%esp\n\tpushl $" #KIND "\n\tpushl %eax\n\tpushl %ebx\n\tcall _rng_before\n\taddl $16,%esp\n\t" RNG_FXRESTORE "movl %eax,-4(%ebx)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tjmp *-40(%esp)\n\t"); \
}
RNG_PRE_STUB(rng_seed_pre_stub,5)
RNG_PRE_STUB(rng_draw_pre_stub,6)
__attribute__((naked,used)) static void rng_post_stub(void) {
  __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tmovl %esp,%eax\n\tsubl $8,%esp\n\tpushl %eax\n\tpushl %ebx\n\tcall _rng_after\n\taddl $16,%esp\n\t" RNG_FXRESTORE "movl %ebx,%esp\n\tpopal\n\tpopfl\n\tret\n\t");
}

__attribute__((noinline,target("no-sse,no-sse2")))
static int rng_prepare(BYTE* base,DWORD size) {
  unsigned i;
  MEMORY_BASIC_INFORMATION info;
  /* No absolute-base condition here: both spans are position-independent and their
   * bytes are compared exactly, so any relocated absolute operand would refuse. The
   * Stock descriptor (stock_bootstrap.c) separately refuses a relocated main image. */
  if(size!=0x3c0000) return 0;
  for(i=0;i<sizeof rng_seed_bytes;++i) if(base[0x1d73fb+i]!=rng_seed_bytes[i]) return 0;
  for(i=0;i<sizeof rng_draw_bytes;++i) if(base[0x1d7408+i]!=rng_draw_bytes[i]) return 0;
  if(VirtualQuery(base+0x1d7000,&info,sizeof info)!=sizeof info || info.State!=MEM_COMMIT ||
     (info.Protect&PAGE_GUARD) || (info.Protect!=PAGE_EXECUTE_READ && info.Protect!=PAGE_EXECUTE_READWRITE) ||
     (BYTE*)info.BaseAddress>base+0x1d7000 || (uintptr_t)info.BaseAddress+info.RegionSize<(uintptr_t)base+0x1d8000) return 0;
  return 1;
}
__attribute__((noinline,target("no-sse,no-sse2")))
static int rng_make_writable(BYTE* base) {
  return VirtualProtect(base+0x1d7000,4096,PAGE_EXECUTE_READWRITE,&rng_page_protection)!=0;
}
__attribute__((noinline,target("no-sse,no-sse2")))
static int rng_restore_protection(BYTE* base) {
  DWORD ignored;
  return VirtualProtect(base+0x1d7000,4096,rng_page_protection,&ignored)!=0;
}
__attribute__((noinline,target("no-sse,no-sse2")))
static void rng_branch(BYTE* at,DWORD span,const void* to) {
  DWORD displacement=(DWORD)(uintptr_t)to-(DWORD)(uintptr_t)at-5;
  unsigned i;
  at[0]=0xe9;
  for(i=0;i<4;++i) at[1+i]=(BYTE)(displacement>>(8*i));
  for(i=5;i<span;++i) at[i]=0x90;
}
__attribute__((noinline,target("no-sse,no-sse2")))
static void rng_publish(BYTE* base) {
  rng_branch(base+0x1d7400,7,rng_seed_pre_stub);
  rng_branch(base+0x1d740d,9,rng_draw_pre_stub);
}
__attribute__((noinline,target("no-sse,no-sse2")))
static int rng_finish_publication(BYTE* base) {
  int flushed=FlushInstructionCache(GetCurrentProcess(),base+0x1d7000,4096)!=0;
  int restored=rng_restore_protection(base);
  return flushed && restored;
}
#ifdef CONTROL_TAPE_TEST
void* control_tape_test_rng_hook(unsigned kind) {
  if(kind!=KINDS && kind!=KINDS+1) return NULL;
  installed=accepting=rng_installed=1;
  test_bypass_guards=1;
  return kind==KINDS?(void*)rng_seed_pre_stub:(void*)rng_draw_pre_stub;
}
#endif
