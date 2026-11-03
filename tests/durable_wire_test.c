/* Native serializer/transport fixture; generated snapshots, not target ABI proof. */
#include "durable_wire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void write_file(const char* root,const char* name,const void* bytes,size_t count) {
  char path[4096];int n=snprintf(path,sizeof path,"%s/%s",root,name);assert(n>0 && (size_t)n<sizeof path);
  FILE* f=fopen(path,"wbx");assert(f);assert(fwrite(bytes,1,count,f)==count);assert(!fflush(f));assert(!fclose(f));
}
int main(int argc,char** argv) {
  assert(argc==2);const uint32_t nonce[4]={1,2,3,4};
  struct dr_view v={calloc(1,dr_required_bytes(8)),dr_required_bytes(8)};assert(v.memory);
  struct dw_header h={0},bad;unsigned char fx[512],data[368];uint32_t phase=0xdeadbeef;
  for(unsigned i=0;i<512;++i)fx[i]=(unsigned char)(i*17u);
  fx[0]=0x7f;fx[1]=3;fx[24]=0x80;fx[25]=0x1f;fx[26]=fx[27]=0;
  /* Encode the metadata explicitly in little endian, independently of structs. */
  uint32_t meta[12]={7,8,2,1,20,16,16,0,320,0,0,3};
  for(unsigned i=0;i<12;++i)for(unsigned j=0;j<4;++j)data[i*4+j]=(unsigned char)(meta[i]>>(8*j));
  for(unsigned i=0;i<320;++i)data[48+i]=(unsigned char)i;
  h.magic=DW_MAGIC;h.version=DW_VERSION;h.kind=DW_ENTRY;h.header_bytes=DW_HEADER_BYTES;
  h.total_bytes=sizeof h+sizeof fx+sizeof data;h.api=DW_INPUT_DATA;
  h.process_id=11;h.thread_id=22;h.lane_id=h.lane_generation=h.identity=1;h.caller_raw=0x12345678;
  h.arg[0]=0x1000;h.arg[1]=20;h.arg[2]=0x2000;h.arg[3]=0x3000;
  h.cw=0x37f;h.mxcsr=0x1f80;h.fx_bytes=512;h.data_bytes=368;h.data_role=DW_PREIMAGE;
  for(unsigned i=0;i<8;++i)h.gpr[i]=0xabc000u+i;
  h.eax=h.gpr[7];h.eflags=0x202;
  assert(dr_init(v,8,nonce)==DR_OK);
  bad=h;bad.eax^=1;assert(dw_publish(v,&bad,fx,data,&phase)==DR_INVALID);
  bad=h;bad.data_bytes=369;assert(dw_publish(v,&bad,fx,data,&phase)==DR_INVALID);
  bad=h;bad.lane_generation=0;assert(dw_publish(v,&bad,fx,data,&phase)==DR_INVALID);
  bad=h;bad.data_role=DW_POSTIMAGE;assert(dw_publish(v,&bad,fx,data,&phase)==DR_INVALID);
  bad=h;bad.cw^=1;assert(dw_publish(v,&bad,fx,data,&phase)==DR_INVALID);
  assert(phase==0xdeadbeef && ((struct dr_header*)v.memory)->next_ticket==1);
  data[24]=17;assert(dw_publish(v,&h,fx,data,&phase)==DR_INVALID);data[24]=16;
  data[44]=0;assert(dw_publish(v,&h,fx,data,&phase)==DR_INVALID);data[44]=3;
  struct dw_header saved=h;unsigned char saved_fx[512],saved_data[368];memcpy(saved_fx,fx,512);memcpy(saved_data,data,368);
  assert(dw_publish(v,&h,fx,data,&phase)==DR_OK && phase==1);
  assert(!memcmp(&saved,&h,sizeof h) && !memcmp(saved_fx,fx,512) && !memcmp(saved_data,data,368));
  h.kind=DW_REPLY;h.call_entry=phase;h.data_role=DW_POSTIMAGE;h.eax=h.gpr[7]=0;
  data[24]=17;assert(dw_publish(v,&h,fx,data,&phase)==DR_INVALID);data[24]=2;
  for(unsigned i=0;i<40;++i)data[48+i]^=0xff; /* Unchanged caller tail is retained. */
  assert(dw_publish(v,&h,fx,data,&phase)==DR_OK && phase==2);
  struct dr_record records[2];
  for(unsigned i=0;i<2;++i) {
    assert(dr_take(v,&records[i])==DR_OK);
    /* Ack follows a checked real write/flush/close; retain each raw record. */
    write_file(argv[1],i?"reply.bin":"entry.bin",&records[i],sizeof records[i]);
    assert(dr_ack(v,&records[i],DR_ARCHIVE_WRITTEN)==DR_OK);
  }
  write_file(argv[1],"journal.bin",records,sizeof records);
  write_file(argv[1],"mapping.bin",v.memory,v.bytes);
  assert(dr_init(v,8,nonce)==DR_OK);h.call_entry=123;phase=0xdeadbeef;
  assert(dw_publish(v,&h,fx,data,&phase)==DR_INVALID && phase==0xdeadbeef);
  struct dr_summary summary;assert(dr_frozen_summary(v,&summary)==DR_OK);
  assert(summary.next_ticket==2 && summary.ack_ticket==1 && summary.reserved==1);
  write_file(argv[1],"hole-mapping.bin",v.memory,v.bytes);
  free(v.memory);puts("native wire publication: pair + refusal + retained reservation checks passed");return 0;
}
