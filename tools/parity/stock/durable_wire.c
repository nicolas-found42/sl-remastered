#include "durable_wire.h"
#include <string.h>
/* Fixed little-endian wire, deliberately independent of host struct padding. */
static uint32_t u32(const unsigned char* p) {
  return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put(unsigned char* p,uint32_t v) {
  p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);
  p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);
}
static int input_shape(const struct dw_header* h,const unsigned char* data) {
  uint32_t m[12],bytes,validity;unsigned i;int entry=h->kind==DW_ENTRY;
  if(h->kind==DW_GAP || h->api<DW_INPUT_STATE)return 1;
  if(h->data_bytes<DW_INPUT_META_BYTES || !data || !h->arg[0])return 0;
  for(i=0;i<12;++i)m[i]=u32(data+4*i);
  if(m[0]!=DW_INPUT_SCHEMA || !m[1] || !m[2] || (m[3]!=1 && m[3]!=2) ||
     m[8]>320 || h->data_bytes!=DW_INPUT_META_BYTES+m[8])return 0;
  bytes=0;validity=0;
  if(h->api==DW_INPUT_STATE) {
    bytes=m[3]==1?256u:16u;validity=1;
    if(m[4]!=bytes || h->arg[1]!=bytes || !h->arg[2] || m[5] || m[6] || m[7])return 0;
  } else if(h->api==DW_INPUT_DATA) {
    bytes=20*m[5];validity=3;
    if(m[3]!=1 || m[4]!=20 || !m[5] || m[5]>16 || m[7] ||
       h->arg[1]!=20 || !h->arg[2] || !h->arg[3] || h->arg[4] ||
       (entry?m[6]!=m[5]:m[6]>m[5]))return 0;
  } else if(h->api==DW_INPUT_QUERY) {
    bytes=entry?16u:4u;validity=3;
    if(m[4]!=16 || m[7] || !h->arg[1] || !h->arg[2] ||
       (entry && m[6]!=m[5]))return 0;
  } else if(m[4] || m[5] || m[6] || m[7])return 0;
  if(m[8]!=bytes || m[11]!=validity)return 0;
  if(h->api==DW_INPUT_QUERY && !entry) {
    if(u32(data+DW_INPUT_META_BYTES)!=m[6])return 0;
    if((int32_t)h->eax>=0) {if(!m[6] || m[9]!=m[6])return 0;}
    else if(m[9])return 0;
  } else if(m[9])return 0;
  if(h->api==DW_INPUT_RELEASE && !entry && !h->eax) {if(!m[10])return 0;}
  else if(m[10])return 0;
  return 1;
}
int dw_publish(struct dr_view view,const struct dw_header* input,
               const unsigned char* fx,const unsigned char* data,uint32_t* phase) {
  struct dw_header h;struct dr_reservation reservation;
  unsigned char payload[DW_HEADER_BYTES+DW_FX_BYTES+DW_DATA_MAX];
  uint32_t words[36];unsigned i;int status;
  if(!input || !phase)return DR_INVALID;
  h=*input;
  if(h.magic!=DW_MAGIC || h.version!=DW_VERSION || h.header_bytes!=DW_HEADER_BYTES ||
     h.total_bytes!=DW_HEADER_BYTES+h.fx_bytes+h.data_bytes || h.phase ||
     h.api>DW_INPUT_RELEASE || h.cw>0xffff || h.eax!=h.gpr[7] ||
     (h.fx_bytes!=0 && h.fx_bytes!=DW_FX_BYTES) || h.data_bytes>DW_DATA_MAX ||
     (h.fx_bytes && !fx) || (h.data_bytes && !data))return DR_INVALID;
  if(h.kind!=DW_ENTRY && h.kind!=DW_REPLY && h.kind!=DW_GAP)return DR_INVALID;
  if(h.kind!=DW_GAP && h.fx_bytes!=DW_FX_BYTES)return DR_INVALID;
  if(h.fx_bytes && (((uint32_t)fx[0]|((uint32_t)fx[1]<<8))!=h.cw || u32(fx+24)!=h.mxcsr))return DR_INVALID;
  if(h.kind==DW_ENTRY) {if(h.call_entry || h.gap)return DR_INVALID;}
  else if(h.kind==DW_REPLY) {if(!h.call_entry || h.call_entry>DR_MAX_TICKET || h.gap)return DR_INVALID;}
  else if(!h.gap || h.call_entry>DR_MAX_TICKET)return DR_INVALID;
  if(h.kind!=DW_GAP || h.identity!=DW_UNQUALIFIED) {
    if(h.identity!=DW_OWNED_MAIN || !h.process_id || !h.thread_id || !h.lane_generation)return DR_INVALID;
  } else if(h.lane_generation || h.call_entry)return DR_INVALID;
  if(!h.data_bytes) {if(h.data_role!=DW_NO_DATA)return DR_INVALID;}
  else if(h.kind==DW_ENTRY || (h.kind==DW_GAP && !h.call_entry)) {
    if(h.data_role!=DW_PREIMAGE)return DR_INVALID;
  } else if(h.data_role!=DW_POSTIMAGE)return DR_INVALID;
  if(!input_shape(&h,data))return DR_INVALID;
  status=dr_reserve(view,&reservation);if(status!=DR_OK)return status;
  h.phase=reservation.ticket;
  if(h.kind==DW_ENTRY)h.call_entry=h.phase;
  else if(h.call_entry>=h.phase)return DR_INVALID; /* Hole remains evidence. */
  words[0]=h.magic;words[1]=h.version;words[2]=h.kind;words[3]=h.header_bytes;words[4]=h.total_bytes;
  words[5]=h.phase;words[6]=h.call_entry;words[7]=h.api;words[8]=h.process_id;words[9]=h.thread_id;
  words[10]=h.lane_id;words[11]=h.lane_generation;words[12]=h.identity;words[13]=h.caller_raw;
  for(i=0;i<5;++i)words[14+i]=h.arg[i];
  words[19]=h.eax;words[20]=h.last_error;words[21]=h.cw;words[22]=h.mxcsr;
  words[23]=h.fx_bytes;words[24]=h.data_bytes;words[25]=h.data_role;words[26]=h.gap;
  for(i=0;i<8;++i)words[27+i]=h.gpr[i];
  words[35]=h.eflags;
  for(i=0;i<36;++i)put(payload+i*4,words[i]);
  if(h.fx_bytes)memcpy(payload+DW_HEADER_BYTES,fx,h.fx_bytes);
  if(h.data_bytes)memcpy(payload+DW_HEADER_BYTES+h.fx_bytes,data,h.data_bytes);
  status=dr_commit(view,reservation,h.kind,payload,h.total_bytes);
  if(status==DR_OK)*phase=h.phase;
  return status;
}
