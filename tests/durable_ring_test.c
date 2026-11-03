/* Host CPU/process protocol qualification only. Not Wine mapping/ABI evidence. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include "durable_ring.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int death_mode;
void dr_test_reserved(uint32_t ticket) {if(ticket==1 && death_mode==1)_exit(41);}
void dr_test_writing(uint32_t ticket) {if(ticket==1 && death_mode==2)_exit(42);}
void dr_test_reading(uint32_t ticket) {if(ticket==1 && death_mode==4)_exit(44);}
static const uint32_t nonce[4]={1,2,3,4};
static struct dr_view create(uint32_t count) {
  struct dr_view v;v.bytes=dr_required_bytes(count);
  v.memory=mmap(NULL,v.bytes,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
  assert(v.memory!=MAP_FAILED);assert(dr_init(v,count,nonce)==DR_OK);return v;
}
static struct dr_summary summary(struct dr_view v) {
  struct dr_summary s;assert(dr_frozen_summary(v,&s)==DR_OK);return s;
}
static struct dr_reservation reserve(struct dr_view v) {
  struct dr_reservation r;assert(dr_reserve(v,&r)==DR_OK);return r;
}
static void commit(struct dr_view v,struct dr_reservation r,uint32_t value) {
  assert(dr_commit(v,r,9,&value,sizeof value)==DR_OK);
}
static struct dr_record take(struct dr_view v) {
  struct dr_record r;assert(dr_take(v,&r)==DR_OK);return r;
}
static void destroy(struct dr_view v) {assert(munmap(v.memory,v.bytes)==0);}
static void basic(void) {
  struct dr_view v=create(1);uint32_t i;
  for(i=1;i<=100;++i) {
    struct dr_reservation r=reserve(v);struct dr_record out;
    assert(r.ticket==i && r.generation==i-1);commit(v,r,i);out=take(v);
    assert(out.ticket==i && out.generation==i-1 && out.kind==9 && out.length==4);
    assert(!memcmp(out.payload,&i,4));assert(dr_ack(v,&out,DR_ARCHIVE_WRITTEN)==DR_OK);
  }
  struct dr_summary s=summary(v);assert(s.next_ticket==101 && s.ack_ticket==101 && !s.flags);
  destroy(v);
}
static void overflow(void) {
  struct dr_view v=create(2);struct dr_reservation a=reserve(v),b=reserve(v),extra;
  struct dr_record out;commit(v,b,2);commit(v,a,1);
  assert(dr_reserve(v,&extra)==DR_FULL);
  assert(summary(v).flags&DR_OVERFLOW);
  out=take(v);assert(out.ticket==1);assert(dr_ack(v,&out,DR_ARCHIVE_WRITTEN)==DR_OK);
  assert(dr_reserve(v,&extra)==DR_FAILED); /* Overflow cannot heal into a complete proof. */
  out=take(v);assert(out.ticket==2);assert(dr_ack(v,&out,DR_ARCHIVE_WRITTEN)==DR_OK);
  assert(summary(v).flags&DR_OVERFLOW);destroy(v);
}
static void stale(void) {
  struct dr_view v=create(1);struct dr_reservation a=reserve(v),b;
  struct dr_record old,current,bad,frozen;
  commit(v,a,1);old=take(v);assert(dr_ack(v,&old,DR_ARCHIVE_WRITTEN)==DR_OK);
  b=reserve(v);commit(v,b,2);current=take(v);
  assert(dr_ack(v,&old,DR_ARCHIVE_WRITTEN)==DR_FAILED);
  bad=current;bad.generation=0;
  assert(dr_ack(v,&bad,DR_ARCHIVE_WRITTEN)==DR_FAILED);
  bad=current;bad.payload[0]^=1;
  assert(dr_ack(v,&bad,DR_ARCHIVE_WRITTEN)==DR_FAILED);
  assert(dr_frozen_record(v,2,&frozen)==DR_OK && !memcmp(&frozen,&current,sizeof current));
  assert(summary(v).reading==1 && summary(v).ack_ticket==2);
  assert(dr_commit(v,a,9,"x",1)==DR_FAILED); /* Old producer cannot alter the reused slot. */
  assert(dr_ack(v,&current,DR_ARCHIVE_WRITTEN)==DR_OK);
  assert(summary(v).flags&DR_BAD_ACK);destroy(v);
}
static void archive_failure(void) {
  struct dr_view v=create(1);struct dr_reservation a=reserve(v);struct dr_record r,retained;
  int fds[2];commit(v,a,7);r=take(v);assert(pipe(fds)==0);assert(close(fds[0])==0);
  assert(signal(SIGPIPE,SIG_IGN)!=SIG_ERR);errno=0;
  ssize_t written=write(fds[1],&r,sizeof r);
  assert(written==-1 && errno==EPIPE);assert(close(fds[1])==0);
  assert(dr_ack(v,&r,written==(ssize_t)sizeof r?DR_ARCHIVE_WRITTEN:DR_ARCHIVE_FAILED)==DR_FAILED);
  assert(summary(v).flags&DR_ARCHIVE_FAILURE);
  assert(summary(v).reading==1 && summary(v).ack_ticket==1);
  assert(dr_frozen_record(v,1,&retained)==DR_OK && !memcmp(&r,&retained,sizeof r));
  assert(dr_ack(v,&r,DR_ARCHIVE_WRITTEN)==DR_OK);
  assert(summary(v).flags&DR_ARCHIVE_FAILURE); /* A later retry never erases loss. */
  assert(dr_archive_failed(v)==DR_FAILED);destroy(v);
}
static void death(int mode) {
  struct dr_view v=create(4);pid_t child;int status;struct dr_record later,out;
  death_mode=mode;child=fork();assert(child>=0);
  if(!child) {struct dr_reservation r=reserve(v);if(mode==3)_exit(43);commit(v,r,1);_exit(44);}
  assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==40+mode);
  death_mode=0;struct dr_reservation b=reserve(v);assert(b.ticket==2);commit(v,b,2);
  assert(dr_take(v,&out)==DR_WAIT); /* Never skip the dead earlier reservation. */
  struct dr_summary s=summary(v);
  assert(s.next_ticket==3 && s.ack_ticket==1 && s.committed==1);
  assert((mode==1 && s.incomplete==1) || (mode==2 && s.writing==1) || (mode==3 && s.reserved==1));
  assert(dr_frozen_record(v,1,&out)==DR_WAIT);
  assert(dr_frozen_record(v,2,&later)==DR_OK && later.ticket==2);
  destroy(v);
}
static void layout(void) {
  struct dr_view v=create(2);uint32_t wrong[4]={1,2,3,5};struct dr_reservation r;
  assert(dr_required_bytes(0)==0 && dr_required_bytes(DR_MAX_SLOTS+1)==0);
  assert(dr_validate(v,nonce)==DR_OK && dr_validate(v,wrong)==DR_INVALID);
  struct dr_view short_view=v;short_view.bytes--;
  assert(dr_validate(short_view,nonce)==DR_INVALID);
  struct dr_header* h=v.memory;h->slot_count=UINT32_MAX;
  assert(dr_reserve(v,&r)==DR_INVALID);h->slot_count=2;
  h->next_ticket=h->ack_ticket=DR_MAX_TICKET+1u;
  assert(dr_reserve(v,&r)==DR_FAILED);assert(summary(v).flags&DR_EXHAUSTED);
  destroy(v);
}
static void collector_death(void) {
  struct dr_view v=create(1);struct dr_reservation r=reserve(v);struct dr_record out;
  commit(v,r,123);death_mode=4;pid_t child=fork();int status;assert(child>=0);
  if(!child) {dr_take(v,&out);_exit(49);}
  assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==44);
  death_mode=0;struct dr_summary s=summary(v);
  assert(s.reading==1 && s.collector_lock==1 && s.ack_ticket==1 && s.next_ticket==2);
  assert(dr_take(v,&out)==DR_BUSY); /* Never steal a dead collector's receipt or invent its archive. */
  assert(dr_frozen_record(v,1,&out)==DR_OK && out.length==4);
  destroy(v);
}

enum {PRODUCERS=4,PER_PRODUCER=500,CAPACITY=64};
struct concurrent {
  struct dr_view view;pthread_mutex_t mutex;pthread_cond_t available;
  unsigned credits;const char* path;
};
struct producer {struct concurrent* context;uint32_t id;};
static void credit(struct concurrent* c,int take_credit) {
  assert(pthread_mutex_lock(&c->mutex)==0);
  if(take_credit) {while(!c->credits)assert(pthread_cond_wait(&c->available,&c->mutex)==0);--c->credits;}
  else {++c->credits;assert(pthread_cond_signal(&c->available)==0);}
  assert(pthread_mutex_unlock(&c->mutex)==0);
}
static uint32_t pattern(uint32_t ticket,uint32_t id,uint32_t index,uint32_t offset) {
  return ticket^(id*UINT32_C(0x1234567))^(index*UINT32_C(0x7654321))^offset;
}
static void* produce(void* arg) {
  struct producer* a=arg;struct concurrent* c=a->context;uint32_t i,j,payload[DR_PAYLOAD/4];
  for(i=0;i<PER_PRODUCER;++i) {
    credit(c,1);struct dr_reservation r=reserve(c->view);
    payload[0]=r.ticket;payload[1]=a->id;payload[2]=i;
    for(j=3;j<DR_PAYLOAD/4;++j)payload[j]=pattern(r.ticket,a->id,i,j);
    if((r.ticket&7u)==0)sched_yield();
    assert(dr_commit(c->view,r,17,payload,sizeof payload)==DR_OK);
  }
  return NULL;
}
static void* collect(void* arg) {
  struct concurrent* c=arg;uint32_t expected=1;unsigned char seen[PRODUCERS][PER_PRODUCER]={{0}};
  FILE* f=fopen(c->path,"wbx");assert(f);
  while(expected<=PRODUCERS*PER_PRODUCER) {
    struct dr_record r;int result=dr_take(c->view,&r);
    if(result==DR_WAIT || result==DR_BUSY) {sched_yield();continue;}
    assert(result==DR_OK && r.ticket==expected && r.kind==17 && r.length==DR_PAYLOAD);
    uint32_t words[DR_PAYLOAD/4];memcpy(words,r.payload,sizeof words);
    assert(words[0]==expected && words[1]<PRODUCERS && words[2]<PER_PRODUCER);
    assert(!seen[words[1]][words[2]]++);
    for(uint32_t j=3;j<DR_PAYLOAD/4;++j)assert(words[j]==pattern(expected,words[1],words[2],j));
    int archived=fwrite(&r,sizeof r,1,f)==1 && fflush(f)==0;
    assert(dr_ack(c->view,&r,archived?DR_ARCHIVE_WRITTEN:DR_ARCHIVE_FAILED)==DR_OK);
    ++expected;credit(c,0);
  }
  if(fclose(f)) {dr_archive_failed(c->view);assert(0);}
  for(unsigned i=0;i<PRODUCERS;++i)for(unsigned j=0;j<PER_PRODUCER;++j)assert(seen[i][j]==1);
  return NULL;
}
static void concurrent(const char* path) {
  struct concurrent c={.view=create(CAPACITY),.mutex=PTHREAD_MUTEX_INITIALIZER,
    .available=PTHREAD_COND_INITIALIZER,.credits=CAPACITY,.path=path};
  struct producer args[PRODUCERS];pthread_t producers[PRODUCERS],collector;
  assert(pthread_create(&collector,NULL,collect,&c)==0);
  for(unsigned i=0;i<PRODUCERS;++i) {args[i]=(struct producer){&c,i};assert(pthread_create(&producers[i],NULL,produce,&args[i])==0);}
  for(unsigned i=0;i<PRODUCERS;++i)assert(pthread_join(producers[i],NULL)==0);
  assert(pthread_join(collector,NULL)==0);
  struct dr_summary s=summary(c.view);
  assert(!s.flags && !s.collector_lock && s.next_ticket==PRODUCERS*PER_PRODUCER+1 && s.ack_ticket==s.next_ticket);
  assert(!s.reserved && !s.writing && !s.committed && !s.reading && !s.incomplete);
  assert(c.credits==CAPACITY);assert(pthread_mutex_destroy(&c.mutex)==0);assert(pthread_cond_destroy(&c.available)==0);
  destroy(c.view);
}
int main(int argc,char** argv) {
  assert(argc>=2);
  if(!strcmp(argv[1],"basic"))basic();
  else if(!strcmp(argv[1],"overflow"))overflow();
  else if(!strcmp(argv[1],"stale"))stale();
  else if(!strcmp(argv[1],"archive-failure"))archive_failure();
  else if(!strcmp(argv[1],"reserve-death"))death(1);
  else if(!strcmp(argv[1],"writing-death"))death(2);
  else if(!strcmp(argv[1],"reserved-death"))death(3);
  else if(!strcmp(argv[1],"layout"))layout();
  else if(!strcmp(argv[1],"collector-death"))collector_death();
  else if(!strcmp(argv[1],"concurrent")) {assert(argc==3);concurrent(argv[2]);}
  else return 2;
  printf("{\"case\":\"%s\",\"passed\":true,\"scope\":\"host shared protocol only\"}\n",argv[1]);
  return 0;
}
