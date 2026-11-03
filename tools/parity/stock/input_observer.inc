/* Included exactly once by the recorder owner. No independent clock/ring phase
 * source in the DirectInput proxy. Records share next_sequence/next_return,
 * next_phase, get_lane(), nesting, active, accepting and stop with API/RNG calls.
 * Bounded owned qualification precedes any actual Stock integration.
 */
#include "input_observer.h"
#ifdef CONTROL_TAPE_INPUT_STOCK
#define INPUT_CAPTURE_SCOPE "experimental_stock_consumption"
#else
#define INPUT_CAPTURE_SCOPE "owned_console_only"
#endif
#define INPUT_CAPACITY 4096u
#define INPUT_DEVICES 16u
#define INPUT_VTABLES 8u
#define INPUT_STATE (KINDS + 2u)
#define INPUT_ACQUIRE (KINDS + 3u)
#define INPUT_DATA (KINDS + 4u)
#define INPUT_QUERY (KINDS + 5u)
#define INPUT_ADDREF (KINDS + 6u)
#define INPUT_RELEASE (KINDS + 7u)
struct input_record {
  struct event call;
  DWORD device_id, incarnation, device_kind, size, count_pointer, count_in, count_out, flags;
  DWORD pre_valid, post_valid;
  DWORD object_raw, related_raw, retired_interfaces, post_size;
  DWORD buffer_bytes;
  BYTE pre[320], post[320];
};
struct input_device { void* object; DWORD id, incarnation, kind, vtable_index; int live; };
static struct input_record input_records[INPUT_CAPACITY];
static struct input_device input_devices[INPUT_DEVICES];
static void** input_vtables[INPUT_VTABLES];
static void* input_targets[6][INPUT_VTABLES] __attribute__((used));
static int input_com_vtables[INPUT_VTABLES];
static int input_alias_layout[INPUT_VTABLES];
static DWORD input_device_count, input_vtable_count;
static volatile LONG input_entered, input_returned;

static int input_buffer(DWORD address, DWORD size) {
  /* Caller memory is never owned by the recorder. This bounds a snapshot, not
   * lifetime/alias safety against a concurrent remap or writer. Such callers
   * remain outside the qualification, just like the existing clock hooks. */
  return writable(address, size);
}
static int input_readable(DWORD address,DWORD size) {
  MEMORY_BASIC_INFORMATION info;
  if(!size || address>0xffffffffu-size || VirtualQuery((void*)(uintptr_t)address,&info,sizeof info)!=sizeof info ||
     info.State!=MEM_COMMIT || (info.Protect & (PAGE_GUARD|PAGE_NOACCESS)) ||
     !(info.Protect & (PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return 0;
  return (uintptr_t)info.BaseAddress<=address && (uint64_t)(uintptr_t)info.BaseAddress+info.RegionSize>=(uint64_t)address+size;
}
static void input_refusal(void) { InterlockedIncrement(&gap_count); }
__attribute__((used,noinline)) static void input_enter(DWORD* regs, const BYTE* fx, DWORD encoded) {
  DWORD error = error_get(), kind = encoded / INPUT_VTABLES, index = encoded % INPUT_VTABLES;
  DWORD* stack = (DWORD*)(uintptr_t)(regs[3] + 4);
  struct lane* t;
  struct input_device* device = NULL;
  struct input_record* r;
  struct event* e;
  LONG seq, ordinal;
  unsigned i;
  if (!observation_enter()) { dirty_fx(); error_set(error); return; }
  t = get_lane();
  if (!t || t->depth == DEPTH) { drop(); goto ignored; }
  seq = InterlockedIncrement(&next_sequence);
  ordinal = InterlockedIncrement(&input_entered);
  if (seq <= 0 || ordinal <= 0) { drop(); goto ignored; }
  r = &input_records[((DWORD)ordinal-1) % INPUT_CAPACITY]; e = &r->call;
  if (InterlockedCompareExchange(&e->state,1,0) != 0) { drop(); goto ignored; }
  /* A reused slot is exclusively ours after its successful state transition. */
  memset((BYTE*)r + sizeof e->state,0,sizeof *r - sizeof e->state);
  e->sequence = (DWORD)seq; e->entry_phase = (DWORD)InterlockedIncrement(&next_phase);
  if ((LONG)e->entry_phase <= 0) drop();
  e->native_thread = (DWORD)t->id; e->lane = (DWORD)(t-lanes); e->lane_read = ++t->reads;
  e->kind = INPUT_STATE + kind; e->caller = stack[0]; e->error_before = error;
  r->object_raw=stack[1];
  memcpy(&e->cw_before,fx,2); memcpy(&e->mxcsr_before,fx+24,4);
  for (i=0;i<input_device_count;++i)
    if (input_devices[i].live && (DWORD)(uintptr_t)input_devices[i].object == stack[1] &&
        input_devices[i].vtable_index == index) { device = &input_devices[i]; break; }
  if (!device) { e->gap = 10; goto refuse; }
  r->device_id=device->id; r->incarnation=device->incarnation; r->device_kind=device->kind;
  if (kind == 0) {
    r->size=stack[2]; e->arg=stack[3];
    if (r->size != (device->kind==PARITY_INPUT_KEYBOARD ? 256u : 16u)) { e->gap=11; goto refuse; }
  } else if (kind == 2) {
    r->size=stack[2]; e->arg=stack[3]; r->count_pointer=stack[4]; r->flags=stack[5];
    if (r->size!=20 || r->flags || device->kind!=PARITY_INPUT_KEYBOARD ||
        !input_buffer(r->count_pointer,4)) { e->gap=12; goto refuse; }
    r->count_in=*(DWORD*)(uintptr_t)r->count_pointer;
    if (!r->count_in || r->count_in>16) { e->gap=12; goto refuse; }
  } else if (kind == 3) {
    r->size=16; e->arg=stack[2]; r->count_pointer=stack[3];
    if(!input_com_vtables[index] || !input_buffer(r->count_pointer,4)) { e->gap=18; goto refuse; }
    r->count_in=*(DWORD*)(uintptr_t)r->count_pointer;
  } else if (kind > 5 || (kind >= 4 && !input_com_vtables[index])) { e->gap=13; goto refuse; }
  r->buffer_bytes=kind==2?r->size*r->count_in:r->size;
  if (kind == 0 || kind == 2 || kind == 3) {
    if (!(kind==3 ? input_readable(e->arg,r->buffer_bytes) : input_buffer(e->arg,r->buffer_bytes))) { e->gap=14; goto refuse; }
    memcpy(r->pre,(void*)(uintptr_t)e->arg,r->buffer_bytes); r->pre_valid=1;
  }
  t->frame[t->depth].caller=stack[0]; t->frame[t->depth++].event=e;
  stack[0]=(DWORD)(uintptr_t)return_stub;
  dirty_fx(); error_set(error); return;
refuse:
  input_refusal(); InterlockedExchange(&e->state,2);
ignored:
  observation_leave(); dirty_fx(); error_set(error);
}

static void input_complete(struct event* e) {
  struct input_record* r=(struct input_record*)e;
  unsigned i;
  if(e->kind==INPUT_QUERY) {
    if(!input_buffer(r->count_pointer,4)) {e->gap=19;input_refusal();}
    else {
      r->count_out=*(DWORD*)(uintptr_t)r->count_pointer;
      memcpy(r->post,&r->count_out,4);r->post_valid=1;r->post_size=4;
      if((LONG)e->result>=0) {
        void* alias=(void*)(uintptr_t)r->count_out;
        DWORD origin=INPUT_VTABLES,target=INPUT_VTABLES;
        struct input_device* existing=NULL;
        for(i=0;i<input_device_count;++i) {
          struct input_device* d=&input_devices[i];
          if(d->live && d->object==(void*)(uintptr_t)r->object_raw && d->id==r->device_id &&
             d->incarnation==r->incarnation) origin=d->vtable_index;
          if(d->live && d->object==alias) existing=d;
        }
        /* No new vtable is discovered or published in this hot callback. */
        if(alias && input_buffer(r->count_out,4))
          for(i=0;i<input_vtable_count;++i) if(*(void***)alias==input_vtables[i]) {target=i;break;}
        if(!alias || origin==INPUT_VTABLES || target==INPUT_VTABLES || !input_com_vtables[target] ||
           (target!=origin && !input_alias_layout[target] && !existing) ||
           (existing && (existing->id!=r->device_id || existing->incarnation!=r->incarnation)) ||
           (!existing && input_device_count==INPUT_DEVICES)) {e->gap=20;input_refusal();}
        else {
          if(!existing) {
            for(i=0;i<input_device_count;++i) if(input_devices[i].live && input_devices[i].id==r->device_id && input_devices[i].incarnation==r->incarnation) break;
            existing=&input_devices[input_device_count++];*existing=input_devices[i];existing->object=alias;
            existing->vtable_index=target;
          }
          r->related_raw=(DWORD)(uintptr_t)alias;
        }
      }
    }
  } else if(e->kind==INPUT_RELEASE && e->result==0) {
    /* The target may have freed the object. Use only saved identity fields. */
    for(i=0;i<input_device_count;++i) {
      struct input_device* d=&input_devices[i];
      if(d->live && d->id==r->device_id && d->incarnation==r->incarnation) {d->live=0;++r->retired_interfaces;}
    }
  } else if (e->kind == INPUT_STATE || e->kind == INPUT_DATA) {
    if (!input_buffer(e->arg,r->buffer_bytes)) { e->gap=15; input_refusal(); }
    else { memcpy(r->post,(void*)(uintptr_t)e->arg,r->buffer_bytes); r->post_valid=1;r->post_size=r->buffer_bytes; }
    if (e->kind == INPUT_DATA) {
      if (!input_buffer(r->count_pointer,4)) { e->gap=16; input_refusal(); }
      else {
        r->count_out=*(DWORD*)(uintptr_t)r->count_pointer;
        if (r->count_out>r->count_in) { e->gap=17; input_refusal(); }
      }
    }
  }
  InterlockedIncrement(&input_returned);
}

/* Each patched vtable gets a dedicated saved-target cell. Even an unregistered
 * alias sharing that vtable forwards to the correct target with a visible gap;
 * no global temporary target, object-pointer lookup failure or extra API read. */
#define INPUT_STUB(NAME, ENCODED, BYTE_OFFSET) \
__attribute__((naked)) static void NAME(void) { \
  __asm__ volatile("pushfl\n\tpushal\n\tcld\n\tmovl %esp,%ebx\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tmovl %esp,%eax\n\tsubl $4,%esp\n\tpushl $" #ENCODED "\n\tpushl %eax\n\tpushl %ebx\n\tcall _input_enter\n\taddl $16,%esp\n\t" \
  "fxrstor (%esp)\n\tmovl %ebx,%esp\n\tpopal\n\tpopfl\n\tjmp *_input_targets+" #BYTE_OFFSET "\n\t"); \
}
#define INPUT_SET(N,S,A,D,SO,AO,DO) INPUT_STUB(input_state_##N,S,SO) INPUT_STUB(input_acquire_##N,A,AO) INPUT_STUB(input_data_##N,D,DO)
INPUT_SET(0,0,8,16,0,32,64)
INPUT_SET(1,1,9,17,4,36,68)
INPUT_SET(2,2,10,18,8,40,72)
INPUT_SET(3,3,11,19,12,44,76)
INPUT_SET(4,4,12,20,16,48,80)
INPUT_SET(5,5,13,21,20,52,84)
INPUT_SET(6,6,14,22,24,56,88)
INPUT_SET(7,7,15,23,28,60,92)
#define COM_SET(N,Q,A,R,QO,AO,RO) INPUT_STUB(input_query_##N,Q,QO) INPUT_STUB(input_addref_##N,A,AO) INPUT_STUB(input_release_##N,R,RO)
COM_SET(0,24,32,40,96,128,160)
COM_SET(1,25,33,41,100,132,164)
COM_SET(2,26,34,42,104,136,168)
COM_SET(3,27,35,43,108,140,172)
COM_SET(4,28,36,44,112,144,176)
COM_SET(5,29,37,45,116,148,180)
COM_SET(6,30,38,46,120,152,184)
COM_SET(7,31,39,47,124,156,188)
#undef COM_SET
#undef INPUT_SET
#undef INPUT_STUB

/* Cold publication shared by explicit registration and the separate v2 layout
 * preparation API. No live callback may race this operation. */
static unsigned input_publish_vtable(void** vtable,int com) {
  static void* hooks[6][INPUT_VTABLES] = {
    {input_state_0,input_state_1,input_state_2,input_state_3,input_state_4,input_state_5,input_state_6,input_state_7},
    {input_acquire_0,input_acquire_1,input_acquire_2,input_acquire_3,input_acquire_4,input_acquire_5,input_acquire_6,input_acquire_7},
    {input_data_0,input_data_1,input_data_2,input_data_3,input_data_4,input_data_5,input_data_6,input_data_7},
    {input_query_0,input_query_1,input_query_2,input_query_3,input_query_4,input_query_5,input_query_6,input_query_7},
    {input_addref_0,input_addref_1,input_addref_2,input_addref_3,input_addref_4,input_addref_5,input_addref_6,input_addref_7},
    {input_release_0,input_release_1,input_release_2,input_release_3,input_release_4,input_release_5,input_release_6,input_release_7}};
  static const unsigned slots[6]={9,7,10,0,1,2};
  DWORD ignored,old;
  unsigned j,index;
  for(index=0;index<input_vtable_count;++index) if(input_vtables[index]==vtable) break;
  if(index==input_vtable_count) {
    MEMORY_BASIC_INFORMATION info;
    if(index==INPUT_VTABLES || VirtualQuery(vtable,&info,sizeof info)!=sizeof info ||
       info.State!=MEM_COMMIT || (info.Protect & (PAGE_NOACCESS|PAGE_GUARD)) ||
       (uintptr_t)vtable < (uintptr_t)info.BaseAddress ||
       (uint64_t)(uintptr_t)vtable + 44 > (uint64_t)(uintptr_t)info.BaseAddress+info.RegionSize) return INPUT_VTABLES;
    for(j=0;j<(com?6u:3u);++j) if(!vtable[slots[j]]) return INPUT_VTABLES;
    if(!VirtualProtect(vtable,44,PAGE_READWRITE,&old)) return INPUT_VTABLES;
    input_vtables[index]=vtable;
    input_com_vtables[index]=com;
    for(j=0;j<(com?6u:3u);++j) input_targets[j][index]=vtable[slots[j]];
    for(j=0;j<(com?6u:3u);++j) InterlockedExchange((volatile LONG*)&vtable[slots[j]],(LONG)(uintptr_t)hooks[j][index]);
    ++input_vtable_count;
    if(!VirtualProtect(vtable,44,old,&ignored)) return INPUT_VTABLES;
  } else {
    if(com && !input_com_vtables[index]) return INPUT_VTABLES;
    for(j=0;j<(input_com_vtables[index]?6u:3u);++j) if(vtable[slots[j]]!=hooks[j][index]) return INPUT_VTABLES;
  }
  return index;
}
static int input_register_mode(void* object,DWORD id,DWORD incarnation,DWORD kind,int com) {
  DWORD error=error_get();unsigned i,index;struct input_device* d;int result=0;
  if (!installed || !accepting || !object || !id || !incarnation ||
      InterlockedCompareExchange(&active,0,0) ||
      (kind!=PARITY_INPUT_KEYBOARD && kind!=PARITY_INPUT_MOUSE) || input_device_count==INPUT_DEVICES) goto done;
  for(i=0;i<input_device_count;++i) {
    d=&input_devices[i];
    if ((d->live && (d->object==object || d->id==id)) || (d->id==id && d->incarnation>=incarnation)) goto done;
  }
  if(!writable((DWORD)(uintptr_t)object,4)) goto done;
  index=input_publish_vtable(*(void***)object,com);if(index==INPUT_VTABLES) goto done;
  d=&input_devices[input_device_count++];
  d->object=object;d->id=id;d->incarnation=incarnation;d->kind=kind;d->vtable_index=index;d->live=1;
  result=1;
done:
  if(!result) input_refusal();
  error_set(error); return result;
}
static int __cdecl input_register(void* object,DWORD id,DWORD incarnation,DWORD kind) {
  return input_register_mode(object,id,incarnation,kind,0);
}
static int __cdecl input_register_com(void* object,DWORD id,DWORD incarnation,DWORD kind) {
  return input_register_mode(object,id,incarnation,kind,1);
}
static int __cdecl input_prepare_interface(void* object,const GUID* iid) {
  static const GUID ansi={0x54d41080,0xdc15,0x4833,{0xa4,0x1b,0x74,0x8f,0x73,0xa3,0x81,0x79}};
  static const GUID unicode={0x54d41081,0xdc15,0x4833,{0xa4,0x1b,0x74,0x8f,0x73,0xa3,0x81,0x79}};
  DWORD error=error_get();unsigned index;int result=0;
  if(!installed || !accepting || InterlockedCompareExchange(&active,0,0) ||
     !input_readable((DWORD)(uintptr_t)iid,sizeof *iid) ||
     (memcmp(iid,&ansi,sizeof ansi) && memcmp(iid,&unicode,sizeof unicode)) ||
     !writable((DWORD)(uintptr_t)object,4)) goto done;
  index=input_publish_vtable(*(void***)object,1);if(index==INPUT_VTABLES) goto done;
  input_alias_layout[index]=1;result=1;
done:
  if(!result) input_refusal();
  error_set(error);return result;
}
static int __cdecl input_bind_interface(void* base,void* alias,DWORD id,DWORD incarnation) {
  DWORD error=error_get();unsigned i,index;struct input_device* origin=NULL;int result=0;
  if(!installed || !accepting || InterlockedCompareExchange(&active,0,0) ||
     !writable((DWORD)(uintptr_t)alias,4))goto done;
  for(i=0;i<input_device_count;++i) {
    struct input_device* d=&input_devices[i];
    if(d->live && d->object==base && d->id==id && d->incarnation==incarnation)origin=d;
    if(d->live && d->object==alias) {
      if(d->id==id && d->incarnation==incarnation &&
         input_vtables[d->vtable_index]==*(void***)alias &&
         input_publish_vtable(*(void***)alias,1)==d->vtable_index) result=1;
      else goto done;
    }
  }
  if(!origin || !input_com_vtables[origin->vtable_index]){result=0;goto done;}
  if(result)goto done;
  if(input_device_count==INPUT_DEVICES)goto done;
  for(index=0;index<input_vtable_count;++index) if(input_vtables[index]==*(void***)alias)break;
  if(index==input_vtable_count || !input_com_vtables[index] ||
     (index!=origin->vtable_index && !input_alias_layout[index]))goto done;
  if(input_publish_vtable(*(void***)alias,1)!=index)goto done;
  input_devices[input_device_count]=*origin;
  input_devices[input_device_count].object=alias;input_devices[input_device_count].vtable_index=index;
  ++input_device_count;result=1;
done:
  if(!result)input_refusal();
  error_set(error);return result;
}
static int __cdecl input_retire(void* object,DWORD id,DWORD incarnation) {
  DWORD error=error_get(); unsigned i; int result=0;
  /* Explicit retirement remains a caller-declared boundary. In COM mode it
   * retires all observed aliases of that identity, as zero Release does. */
  if(InterlockedCompareExchange(&active,0,0)) goto done;
  for(i=0;i<input_device_count;++i) {
    struct input_device* d=&input_devices[i];
    if(d->live && d->object==object && d->id==id && d->incarnation==incarnation) {
      unsigned j;
      d->live=0;result=1;
      if(input_com_vtables[d->vtable_index]) for(j=0;j<input_device_count;++j)
        if(input_devices[j].id==id && input_devices[j].incarnation==incarnation) input_devices[j].live=0;
      break;
    }
  }
done:
  if(!result) input_refusal();
  error_set(error);return result;
}
__declspec(dllexport) const struct parity_input_observer_v1* __cdecl parity_input_observer(DWORD version,DWORD size) {
  static const struct parity_input_observer_v1 api={PARITY_INPUT_VERSION,sizeof api,PARITY_INPUT_IDENTITY,
                                                  &next_phase,input_register,input_retire};
  return version==api.version && size==api.size && installed && accepting ? &api : NULL;
}
__declspec(dllexport) const struct parity_input_com_observer_v1* __cdecl parity_input_com_observer(DWORD version,DWORD size) {
  static const struct parity_input_com_observer_v1 api={PARITY_INPUT_COM_VERSION,sizeof api,PARITY_INPUT_COM_IDENTITY,
                                                      &next_phase,input_register_com,input_retire};
  return version==api.version && size==api.size && installed && accepting ? &api : NULL;
}
__declspec(dllexport) const struct parity_input_com_observer_v2* __cdecl parity_input_com_observer2(DWORD version,DWORD size) {
  static const struct parity_input_com_observer_v2 api={PARITY_INPUT_COM_VERSION2,sizeof api,PARITY_INPUT_COM_IDENTITY2,
                                                       &next_phase,input_register_com,input_prepare_interface,input_bind_interface,input_retire};
  return version==api.version && size==api.size && installed && accepting ? &api : NULL;
}
static int input_flush(FILE* f) {
  unsigned i,j;
  for(i=0;i<INPUT_CAPACITY;++i) {
    struct input_record* r=&input_records[i];struct event* e=&r->call;
    if(InterlockedCompareExchange(&e->state,3,2)!=2) continue;
    fprintf(f,"{\"event\":\"input_return\",\"scope\":\"" INPUT_CAPTURE_SCOPE "\",\"seq\":%lu,\"return_seq\":%lu,\"entry_phase\":%lu,\"return_phase\":%lu,\"lane\":%lu,\"lane_read\":%lu,\"thread\":%lu,\"method\":%lu,\"device_id\":%lu,\"incarnation\":%lu,\"device_kind\":%lu,\"caller_raw\":%lu,\"size\":%lu,\"count_in\":%lu,\"count_out\":%lu,\"flags\":%lu,\"result\":%lu,\"gap\":%lu,\"pre_valid\":%lu,\"post_valid\":%lu,\"last_error\":[%lu,%lu],\"cw\":[%u,%u],\"mxcsr\":[%lu,%lu],\"pre\":\"",
      e->sequence,e->returned,e->entry_phase,e->return_phase,e->lane,e->lane_read,e->native_thread,e->kind-INPUT_STATE,
      r->device_id,r->incarnation,r->device_kind,e->caller,r->size,r->count_in,r->count_out,r->flags,e->result,e->gap,r->pre_valid,r->post_valid,
      e->error_before,e->error_after,e->cw_before,e->cw_after,e->mxcsr_before,e->mxcsr_after);
    if(r->pre_valid) for(j=0;j<r->buffer_bytes;++j) fprintf(f,"%02x",r->pre[j]);
    fputs("\",\"post\":\"",f);
    if(r->post_valid) for(j=0;j<r->post_size;++j) fprintf(f,"%02x",r->post[j]);
    fprintf(f,"\",\"object_raw\":%lu,\"related_raw\":%lu,\"retired_interfaces\":%lu,\"buffer_bytes\":%lu}\n",r->object_raw,r->related_raw,r->retired_interfaces,r->buffer_bytes);
    if(ferror(f)||fflush(f)) {InterlockedExchange(&e->state,2);return 0;}
    InterlockedExchange(&e->state,0);
  }
  return 1;
}
