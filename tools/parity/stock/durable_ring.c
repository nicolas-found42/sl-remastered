#include "durable_ring.h"
#include <string.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
_Static_assert(sizeof(LONG)==4,"Win32 atomic width");
static uint32_t load32(uint32_t* p) {return (uint32_t)InterlockedCompareExchange((volatile LONG*)p,0,0);}
static int cas32(uint32_t* p,uint32_t expected,uint32_t desired) {
  return (uint32_t)InterlockedCompareExchange((volatile LONG*)p,(LONG)desired,(LONG)expected)==expected;
}
static void store32(uint32_t* p,uint32_t v) {InterlockedExchange((volatile LONG*)p,(LONG)v);}
static void or32(uint32_t* p,uint32_t v) {InterlockedOr((volatile LONG*)p,(LONG)v);}
#else
_Static_assert(__atomic_always_lock_free(4,0),"shared atomics cannot use process-local locks");
static uint32_t load32(uint32_t* p) {return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static int cas32(uint32_t* p,uint32_t expected,uint32_t desired) {
  return __atomic_compare_exchange_n(p,&expected,desired,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static void store32(uint32_t* p,uint32_t v) {__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static void or32(uint32_t* p,uint32_t v) {__atomic_fetch_or(p,v,__ATOMIC_ACQ_REL);}
#endif
_Static_assert(sizeof(uint32_t)==4,"shared word width");
_Static_assert(sizeof(struct dr_header)==128,"header ABI");
_Static_assert(sizeof(struct dr_slot)==1040,"slot ABI");
_Static_assert(offsetof(struct dr_slot,payload)==16,"payload ABI");
_Static_assert(offsetof(struct dr_header,next_ticket)==40,"counter ABI");
_Static_assert(offsetof(struct dr_header,collector_lock)==52,"lock ABI");

static uint32_t tag(uint32_t ticket,uint32_t state) {return (ticket<<3)|state;}
uint32_t dr_required_bytes(uint32_t slots) {
  if(!slots || slots>DR_MAX_SLOTS)return 0;
  return (uint32_t)sizeof(struct dr_header)+slots*(uint32_t)sizeof(struct dr_slot);
}
static struct dr_header* header(struct dr_view v) {
  struct dr_header* h;
  if(!v.memory || ((uintptr_t)v.memory&3u) || v.bytes<sizeof *h)return NULL;
  h=v.memory;
  if(h->magic!=DR_MAGIC || h->version!=DR_VERSION || h->header_bytes!=sizeof *h ||
     h->slot_bytes!=sizeof(struct dr_slot) || !dr_required_bytes(h->slot_count) ||
     h->total_bytes!=dr_required_bytes(h->slot_count) || h->total_bytes>v.bytes)return NULL;
  return h;
}
static struct dr_slot* slot(struct dr_header* h,uint32_t ticket) {
  return (struct dr_slot*)((unsigned char*)h+sizeof *h)+((ticket-1u)%h->slot_count);
}
static int counters(struct dr_header* h,uint32_t* next,uint32_t* ack) {
  uint32_t attempt;
  for(attempt=0;attempt<64;++attempt) {
    *ack=load32(&h->ack_ticket);*next=load32(&h->next_ticket);
    if(*ack==load32(&h->ack_ticket))break;
  }
  if(attempt==64) {or32(&h->flags,DR_BAD_STATE);return 0;}
  if(!*ack || *ack>*next || *next>DR_MAX_TICKET+1u || *next-*ack>h->slot_count) {
    or32(&h->flags,DR_BAD_STATE);return 0;
  }
  return 1;
}
int dr_init(struct dr_view v,uint32_t slots,const uint32_t nonce[4]) {
  uint32_t size=dr_required_bytes(slots);struct dr_header* h=v.memory;
  if(!size || !h || ((uintptr_t)h&3u) || v.bytes<size || !nonce)return DR_INVALID;
  memset(h,0,size);h->magic=DR_MAGIC;h->version=DR_VERSION;
  h->header_bytes=sizeof *h;h->slot_bytes=sizeof(struct dr_slot);
  h->slot_count=slots;h->total_bytes=size;memcpy(h->nonce,nonce,sizeof h->nonce);
  h->next_ticket=h->ack_ticket=1;return DR_OK;
}
int dr_validate(struct dr_view v,const uint32_t nonce[4]) {
  struct dr_header* h=header(v);
  return h && nonce && !memcmp(h->nonce,nonce,sizeof h->nonce)?DR_OK:DR_INVALID;
}
#ifdef DURABLE_RING_TEST
void dr_test_reserved(uint32_t ticket);
void dr_test_writing(uint32_t ticket);
void dr_test_reading(uint32_t ticket);
#endif
int dr_reserve(struct dr_view v,struct dr_reservation* out) {
  struct dr_header* h=header(v);uint32_t next,ack,attempt;struct dr_slot* s;
  if(!h || !out)return DR_INVALID;
  if(load32(&h->flags))return DR_FAILED;
  for(attempt=0;attempt<64;++attempt) {
    if(!counters(h,&next,&ack))return DR_FAILED;
    if(next>DR_MAX_TICKET) {or32(&h->flags,DR_EXHAUSTED);return DR_FAILED;}
    if(next-ack==h->slot_count) {or32(&h->flags,DR_OVERFLOW);return DR_FULL;}
    if(cas32(&h->next_ticket,next,next+1u))break;
  }
  if(attempt==64) {or32(&h->flags,DR_BAD_STATE);return DR_FAILED;}
#ifdef DURABLE_RING_TEST
  dr_test_reserved(next);
#endif
  s=slot(h,next);
  if(!cas32(&s->tag,tag(next>h->slot_count?next-h->slot_count:0,DR_FREE),tag(next,DR_RESERVED))) {
    or32(&h->flags,DR_BAD_STATE);return DR_FAILED;
  }
  out->ticket=next;out->generation=(next-1u)/h->slot_count;return DR_OK;
}
int dr_commit(struct dr_view v,struct dr_reservation r,uint32_t kind,const void* payload,uint32_t length) {
  struct dr_header* h=header(v);struct dr_slot* s;
  if(!h)return DR_INVALID;
  if(!r.ticket || r.ticket>DR_MAX_TICKET || r.generation!=(r.ticket-1u)/h->slot_count ||
     !kind || length>DR_PAYLOAD || (length && !payload)) {or32(&h->flags,DR_BAD_STATE);return DR_INVALID;}
  s=slot(h,r.ticket);
  if(!cas32(&s->tag,tag(r.ticket,DR_RESERVED),tag(r.ticket,DR_WRITING))) {
    or32(&h->flags,DR_BAD_STATE);return DR_FAILED;
  }
#ifdef DURABLE_RING_TEST
  dr_test_writing(r.ticket);
#endif
  s->kind=kind;s->length=length;s->reserved=0;
  if(length)memcpy(s->payload,payload,length);
  store32(&s->tag,tag(r.ticket,DR_COMMITTED));return DR_OK;
}
static void copy_record(struct dr_header* h,uint32_t ticket,struct dr_slot* s,struct dr_record* out) {
  memset(out,0,sizeof *out);out->ticket=ticket;out->generation=(ticket-1u)/h->slot_count;
  out->kind=s->kind;out->length=s->length;memcpy(out->payload,s->payload,s->length);
}
int dr_take(struct dr_view v,struct dr_record* out) {
  struct dr_header* h=header(v);uint32_t next,ack;struct dr_slot* s;int result=DR_WAIT;
  if(!h || !out)return DR_INVALID;
  if(!cas32(&h->collector_lock,0,1))return DR_BUSY;
  if(!counters(h,&next,&ack)) {result=DR_FAILED;goto done;}
  if(next==ack)goto done;
  s=slot(h,ack);
  if(!cas32(&s->tag,tag(ack,DR_COMMITTED),tag(ack,DR_READING)))goto done;
#ifdef DURABLE_RING_TEST
  dr_test_reading(ack);
#endif
  if(!s->kind || s->length>DR_PAYLOAD) {or32(&h->flags,DR_BAD_STATE);result=DR_FAILED;goto done;}
  copy_record(h,ack,s,out);result=DR_OK;
done:
  store32(&h->collector_lock,0);return result;
}
int dr_ack(struct dr_view v,const struct dr_record* r,int archive_result) {
  struct dr_header* h=header(v);struct dr_slot* s;uint32_t ack;int result=DR_FAILED;
  if(!h || !r)return DR_INVALID;
  if(!cas32(&h->collector_lock,0,1))return DR_BUSY;
  ack=load32(&h->ack_ticket);
  if(!r->ticket || r->ticket>DR_MAX_TICKET || r->ticket!=ack ||
     r->generation!=(r->ticket-1u)/h->slot_count)goto bad_ack;
  s=slot(h,r->ticket);
  if(load32(&s->tag)!=tag(r->ticket,DR_READING) || s->length>DR_PAYLOAD ||
     r->length!=s->length || r->kind!=s->kind || memcmp(r->payload,s->payload,s->length))goto bad_ack;
  if(archive_result!=DR_ARCHIVE_WRITTEN) {
    or32(&h->flags,DR_ARCHIVE_FAILURE);goto done;
  }
  store32(&s->tag,tag(r->ticket,DR_FREE));
  store32(&h->ack_ticket,r->ticket+1u);result=DR_OK;goto done;
bad_ack:
  or32(&h->flags,DR_BAD_ACK);
done:
  store32(&h->collector_lock,0);return result;
}
int dr_archive_failed(struct dr_view v) {
  struct dr_header* h=header(v);
  if(!h)return DR_INVALID;
  or32(&h->flags,DR_ARCHIVE_FAILURE);return DR_FAILED;
}
int dr_frozen_summary(struct dr_view v,struct dr_summary* out) {
  struct dr_header* h=header(v);uint32_t n,a,t;
  if(!h || !out)return DR_INVALID;
  memset(out,0,sizeof *out);
  if(!counters(h,&n,&a))return DR_FAILED;
  out->next_ticket=n;out->ack_ticket=a;out->flags=load32(&h->flags);
  out->collector_lock=load32(&h->collector_lock);
  for(t=a;t<n;++t) {
    uint32_t word=load32(&slot(h,t)->tag),state=word&7u;
    if((word>>3)!=t) {++out->incomplete;continue;}
    switch(state) {
      case DR_RESERVED:++out->reserved;break;
      case DR_WRITING:++out->writing;break;
      case DR_COMMITTED:++out->committed;break;
      case DR_READING:++out->reading;break;
      default:++out->incomplete;break;
    }
  }
  return DR_OK;
}
int dr_frozen_record(struct dr_view v,uint32_t ticket,struct dr_record* out) {
  struct dr_header* h=header(v);uint32_t next,ack,word;struct dr_slot* s;
  if(!h || !out)return DR_INVALID;
  if(!counters(h,&next,&ack))return DR_FAILED;
  if(ticket<ack || ticket>=next)return DR_INVALID;
  s=slot(h,ticket);word=load32(&s->tag);
  if(word!=tag(ticket,DR_COMMITTED) && word!=tag(ticket,DR_READING))return DR_WAIT;
  if(!s->kind || s->length>DR_PAYLOAD)return DR_FAILED;
  copy_record(h,ticket,s,out);return DR_OK;
}
