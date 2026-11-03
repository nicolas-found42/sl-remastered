/* Newer shipped Dyno core oracle. No game entry or world/profile patches.
 * Source SHA and source-bound REA packet are checked by the admitted driver.
 * Only two declared bounded allocator entry substitutions are permitted. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char* image;
static uint32_t image_size;
static void require(int ok,const char* why){if(!ok){fprintf(stderr,"gap:%s\n",why);ExitProcess(90);}}
static uint32_t get32(const void* p,unsigned o){uint32_t v;memcpy(&v,(const char*)p+o,4);return v;}
static void put32(void* p,unsigned o,uint32_t v){memcpy((char*)p+o,&v,4);}
static void putf(void* p,unsigned word,float v){memcpy((char*)p+word*4,&v,4);}
static void hexbytes(const unsigned char* p,unsigned n){putchar('"');for(unsigned i=0;i<n;i++)printf("%02x",p[i]);putchar('"');}
enum {MAX_ALLOCATIONS=2048,MAX_ALLOCATION=1024*1024,MAX_AGGREGATE=16*1024*1024};
struct Allocation {void* pointer;uint32_t size,id,live;};
static struct Allocation allocations[MAX_ALLOCATIONS];
static uint32_t registry_count,live_count,live_bytes,peak_bytes,allocation_calls,reallocation_calls,free_calls,zero_allocations,null_frees;
static uint32_t last_incarnation;
static int allocation_index(const void* pointer){for(unsigned i=0;i<registry_count;++i)if(allocations[i].live && allocations[i].pointer==pointer)return (int)i;return -1;}
static int allocation_contains(const void* pointer,uint32_t bytes){
  uintptr_t p=(uintptr_t)pointer;
  for(unsigned i=0;i<registry_count;++i)if(allocations[i].live){
    uintptr_t base=(uintptr_t)allocations[i].pointer;
    if(p>=base && bytes<=allocations[i].size && p-base<=allocations[i].size-bytes)return 1;
  }
  return 0;
}
static void bump(uint32_t* counter,const char* reason){require(*counter<UINT32_MAX,reason);++*counter;}
static void* owned_allocate(int requested){
  bump(&allocation_calls,"allocation-counter-overflow");
  /* Original0054f560 explicitly returnsNULL for nonpositive input. */
  if(requested<=0){bump(&zero_allocations,"zero-counter-overflow");return NULL;}
  uint32_t size=(uint32_t)requested;
  require(size<=MAX_ALLOCATION && live_bytes<=MAX_AGGREGATE-size,"allocation-budget");
  unsigned slot=0;while(slot<registry_count && allocations[slot].live)++slot;
  require(slot<MAX_ALLOCATIONS,"allocation-registry-capacity");
  bump(&last_incarnation,"allocation-incarnation-overflow");
  void* pointer=calloc(1,size);require(pointer!=NULL,"owned-allocation-failed");
  allocations[slot]=(struct Allocation){pointer,size,last_incarnation,1};
  if(slot==registry_count)++registry_count;
  ++live_count;live_bytes+=size;if(live_bytes>peak_bytes)peak_bytes=live_bytes;
  return pointer;
}
static void* owned_reallocate(void* pointer,int requested){
  bump(&reallocation_calls,"reallocation-counter-overflow");
  /* Original0054f580 forwards zero/nonpositive requests into its allocator;
     that allocator's zero-size semantics are not established here. */
  require(requested>0,"unqualified-nonpositive-reallocation");
  if(!pointer)return owned_allocate(requested);
  int index=allocation_index(pointer);require(index>=0,"unknown-reallocation");
  uint32_t size=(uint32_t)requested,old=allocations[index].size;
  require(size<=MAX_ALLOCATION && live_bytes-old<=MAX_AGGREGATE-size,"reallocation-budget");
  void* replacement=realloc(pointer,size);require(replacement!=NULL,"owned-reallocation-failed");
  /* The substitution deliberately initializes newly exposed allocation bytes. */
  if(size>old)memset((unsigned char*)replacement+old,0,size-old);
  allocations[index].pointer=replacement;allocations[index].size=size;
  live_bytes=live_bytes-old+size;if(live_bytes>peak_bytes)peak_bytes=live_bytes;
  return replacement;
}
static void owned_free(void* pointer){
  bump(&free_calls,"free-counter-overflow");
  if(!pointer){bump(&null_frees,"null-free-counter-overflow");return;}
  int index=allocation_index(pointer);require(index>=0,"unknown-or-double-free");
  allocations[index].live=0;--live_count;live_bytes-=allocations[index].size;free(pointer);
}
static void verify_original_code(const char* path,IMAGE_NT_HEADERS32* pe){
  FILE* file=fopen(path,"rb");require(file!=NULL,"source-open");
  require(fseek(file,0,SEEK_END)==0,"source-size-seek");long length=ftell(file);require(length>0,"source-size");
  IMAGE_SECTION_HEADER* sections=IMAGE_FIRST_SECTION(pe);uint32_t verified=0;
  for(unsigned i=0;i<pe->FileHeader.NumberOfSections;++i){
    IMAGE_SECTION_HEADER* s=&sections[i];
    require(s->VirtualAddress<=pe->OptionalHeader.SizeOfImage && s->SizeOfRawData<=pe->OptionalHeader.SizeOfImage-s->VirtualAddress,"mapped-section-range");
    require(s->PointerToRawData<=(uint32_t)length && s->SizeOfRawData<=(uint32_t)length-s->PointerToRawData,"source-section-range");
    if(!(s->Characteristics&IMAGE_SCN_CNT_CODE))continue;
    require(fseek(file,(long)s->PointerToRawData,SEEK_SET)==0,"source-code-seek");
    for(uint32_t j=0;j<s->SizeOfRawData;++j){int byte=fgetc(file);require(byte>=0 && byte==image[s->VirtualAddress+j],"original-code-byte-mismatch");}
    verified+=s->SizeOfRawData;
  }
  require(fclose(file)==0 && verified>0,"source-code-complete");
  printf("{\"type\":\"code_identity\",\"verified_original_code_bytes\":%lu,\"source_entry_executed\":false}\n",(unsigned long)verified);
}
static void relocate(IMAGE_NT_HEADERS32* pe){
  IMAGE_DATA_DIRECTORY d=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  uint32_t size=pe->OptionalHeader.SizeOfImage;
  require(d.VirtualAddress<=size && d.Size<=size-d.VirtualAddress && d.Size>0,"relocation-directory");
  unsigned char* seen=calloc(size,1);require(seen!=NULL,"relocation-audit-allocation");
  uint32_t count=0,absolute=0;
  for(uint32_t pos=0;pos<d.Size;){
    require(d.Size-pos>=8,"relocation-header");
    IMAGE_BASE_RELOCATION* block=(void*)(image+d.VirtualAddress+pos);
    require(block->SizeOfBlock>=8 && block->SizeOfBlock<=d.Size-pos && (block->SizeOfBlock&1u)==0,"relocation-block");
    WORD* entries=(void*)(block+1);
    for(unsigned i=0;i<(block->SizeOfBlock-8)/2;++i){
      unsigned type=entries[i]>>12;
      if(type==IMAGE_REL_BASED_ABSOLUTE){++absolute;continue;}
      require(type==IMAGE_REL_BASED_HIGHLOW,"unsupported-relocation-type");
      require(block->VirtualAddress<=size-4 && (entries[i]&4095u)<=size-4-block->VirtualAddress,"relocation-target-range");
      uint32_t rva=block->VirtualAddress+(entries[i]&4095u);
      require(!seen[rva] && !seen[rva+1] && !seen[rva+2] && !seen[rva+3],"overlapping-relocation");memset(seen+rva,1,4);
      put32(image,rva,get32(image,rva)+(uint32_t)(uintptr_t)image-0x400000u);++count;
    }
    pos+=block->SizeOfBlock;
  }
  free(seen);
  printf("{\"type\":\"relocations\",\"highlow\":%lu,\"absolute_padding\":%lu,\"mapped_base\":%lu}\n",(unsigned long)count,(unsigned long)absolute,(unsigned long)(uintptr_t)image);
}
static void patch(unsigned rva,const unsigned char* expected,unsigned count,void* target,const char* name){
  require(count==5 || count==6,"patch-length");require(memcmp(image+rva,expected,count)==0,"patch-preimage");
  printf("{\"type\":\"patch\",\"name\":\"%s\",\"rva\":%lu,\"before\":",name,(unsigned long)rva);hexbytes(image+rva,count);
  memset(image+rva,0x90,count);
  if(target){image[rva]=0xe9;uint32_t displacement=(uint32_t)(uintptr_t)target-((uint32_t)(uintptr_t)image+rva+5u);put32(image,rva+1,displacement);}
  printf(",\"after\":");hexbytes(image+rva,count);puts("}");
}

static void install_patches(void){
 const unsigned char pre[]={0x55,0x8b,0xec,0x8b,0x45,0x08};
 patch(0x167a20,pre,6,owned_allocate,"bounded-allocate");
 patch(0x167950,pre,6,owned_free,"bounded-free");
 require(FlushInstructionCache(GetCurrentProcess(),image,image_size),"publish");
}
static void words(const void* p,unsigned n){putchar('[');for(unsigned i=0;i<n;i++)printf("%s%lu",i?",":"",(unsigned long)get32(p,i*4));putchar(']');}
static void curve(void* model,unsigned offset,unsigned count){void* p=(void*)(uintptr_t)get32(model,offset);if(!p){printf("[]");return;}require(allocation_contains(p,count*4),"curve-range");words(p,count);}
static void call0(void* m,unsigned rva){((void(__attribute__((thiscall))*)(void*))(image+rva))(m);}
static void trace_row(const void* m,const char* phase,unsigned step){
 printf("{\"type\":\"dyno_trace\",\"phase\":\"%s\",\"bank\":0,\"cycle\":0,\"step\":%u,\"cycle_count\":%lu,\"words\":",phase,step,(unsigned long)get32(m,93*4));words(m,116);puts("}");
}
static float getf(const void* m,unsigned word){float v;memcpy(&v,(const char*)m+word*4,4);return v;}
static uint32_t pow_scalar(uint32_t input,uint32_t exponent){
 uint32_t output;void* target=image+0x171143;
 __asm__ volatile("movss %1,%%xmm0;movss %2,%%xmm1;call *%3;movss %%xmm0,%0"
  :"=m"(output):"m"(input),"m"(exponent),"m"(target)
  :"eax","ecx","edx","ebx","esi","edi","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","cc","memory");
 return output;
}
static void first_sample(const void* source){
 unsigned char m[0x1ec],whole_cycle[0x1ec];memcpy(m,source,sizeof(m));
 for(unsigned word=106;word<=115;word++)putf(m,word,0);
 put32(m,0x1d0,437);put32(m,0x1d4,16);put32(m,0x1d8,0);
 putf(m,75,0);putf(m,72,0);putf(m,34,437);putf(m,73,120.f/437.f);
 putf(m,74,437.f*.10471975803375244f);putf(m,92,getf(m,74)*getf(m,0));
 call0(m,0xf4ca0);trace_row(m,"prepare",0);putf(m,33,getf(m,73)*.0043290043249726295f);
 call0(m,0xf4fc0);trace_row(m,"cycle_init",0);memcpy(whole_cycle,m,sizeof(m));
 putf(m,90,0);putf(m,91,0);putf(m,70,0);putf(m,71,0);put32(m,93*4,0);
 unsigned step=0;while(!get32(m,93*4)&&step<232){
  float previous_volume=getf(m,77),previous_temperature=getf(m,81);
  ((void(__attribute__((thiscall))*)(void*,int))(image+0xf50e0))(m,1);trace_row(m,"step",++step);
  if(step==164){
   float inverse=1.f/getf(m,77),ratio=inverse*previous_volume;uint32_t input;memcpy(&input,&ratio,4);
   require(get32(image,0x193704)==0x3e99999a,"pow-temperature-exponent");
   uint32_t value=pow_scalar(input,0x3e99999a),product;float power;memcpy(&power,&value,4);float multiplied=power*previous_temperature;memcpy(&product,&multiplied,4);
   printf("{\"type\":\"math_witness\",\"step\":164,\"input\":%lu,\"exponent\":%lu,\"result\":%lu,\"previous_temperature\":%lu,\"product\":%lu}\n",(unsigned long)input,(unsigned long)0x3e99999a,(unsigned long)value,(unsigned long)get32(&previous_temperature,0),(unsigned long)product);
  }
 }
 require(get32(m,93*4)==1,"first-cycle-bound");
 const unsigned char ret4[]={0xc2,4,0};require(!memcmp(image+0xf4632,ret4,3),"run-cycle-ret4");
 uint32_t before,after;__asm__ volatile("movl %%esp,%0":"=m"(before)::"memory");
 ((void(__attribute__((thiscall))*)(void*,int))(image+0xf4560))(whole_cycle,0);
 __asm__ volatile("movl %%esp,%0":"=m"(after)::"memory");
 printf("{\"type\":\"run_cycle_abi\",\"esp_before\":%lu,\"esp_after\":%lu}\n",(unsigned long)before,(unsigned long)after);
 require(before==after,"run-cycle-stack-balance");trace_row(whole_cycle,"cycle_result",0);
}
static void calculate(void* m,unsigned id){
 call0(m,0xf49e0);if(id==0)trace_row(m,"reset",0);
 const unsigned fields[]={5,1,0,4,17,19,18,20,37,38,39,40,7,22,51,52,53,54,55,56,58,57,59,60,15,61,62,63,13};
 float values[]={4,.041f,.045f,.00005f,.005f,.8f,.005f,.8f,.001f,.25f,.75f,.99f,.003f,7000,.48f,-.05f,2000,5000,16,85000,20000,0,0,10,1000000,14,0,0,293.15f};
 if(id==8){const float clear[]={0,.005f,.005f,100000,1,1,1,1,.001f,.002f,.003f,.004f,100000,10000,.5f,0,0,10000,0,0,0,0,0,0,1000000,14,0,0,293.15f};memcpy(values,clear,sizeof(values));}
 if(id==2){values[22]=.6f;values[23]=.4f;}if(id==3)values[28]*=.85f;
 if(id==5){values[8]=.95f;values[9]=.2f;values[10]=.7f;values[11]=.1f;}
 if(id==6){values[26]=.001f;values[27]=.02f;}
 for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)putf(m,fields[i],values[i]);
 putf(m,41,values[8]);putf(m,42,values[9]-values[8]);putf(m,46,values[10]);putf(m,47,values[11]-values[10]);
 if(id==0)trace_row(m,"inputs",0);
 call0(m,0xf5980);call0(m,0xf59f0);if(id!=8)putf(m,35,7000);put32(m,0x1d8,0);
 if(id==0){trace_row(m,"geometry",0);first_sample(m);}
 int table_step=id==8?(int)(getf(m,35)/16.f):437;
 ((void(__attribute__((thiscall))*)(void*,int))(image+0xf4640))(m,table_step);
 unsigned count=get32(m,0x1d4);require(count==16&&get32(m,0x1d0)==(uint32_t)table_step,"table-shape");
 printf("{\"case\":%u,\"step\":%d,\"statistics\":[",id,table_step);
 const unsigned stats[]={25,29,107,112,108,113};for(unsigned i=0;i<6;i++)printf("%s%lu",i?",":"",(unsigned long)get32(m,stats[i]*4));
 printf("],\"torque\":");curve(m,0x1dc,count);printf(",\"power\":");curve(m,0x1e4,count);
 putf(m,13,293.15f);
 if(id==4){putf(m,15,1200000);putf(m,13,293.15f*1.1f);call0(m,0xf5980);call0(m,0xf59f0);putf(m,35,7000);put32(m,0x1d8,1);((void(__attribute__((thiscall))*)(void*,int))(image+0xf4640))(m,437);}
 printf(",\"nitro_torque\":");curve(m,0x1e0,count);printf(",\"nitro_power\":");curve(m,0x1e8,count);printf(",\"state\":");words(m,116);puts("}");
}
static void volume_controls(const void* source){
 for(unsigned id=0;id<3;id++){
  unsigned char copied[0x1ec];memcpy(copied,source,sizeof(copied));
  putf(copied,75,0);putf(copied,72,0);putf(copied,33,0);put32(copied,93*4,0);
  if(id==0){putf(copied,4,-1);putf(copied,28,-2);putf(copied,77,-3);}
  if(id==1){putf(copied,4,1e-9f);putf(copied,28,1e-9f);putf(copied,77,1e-9f);}
  if(id==2){putf(copied,4,0);putf(copied,28,0);putf(copied,77,0);putf(copied,0,0);put32(copied,33*4,get32(copied,73*4));}
  printf("{\"type\":\"copied_state_boundary\",\"boundary\":%u,\"input\":",id);words(copied,116);
  ((void(__attribute__((thiscall))*)(void*,int))(image+0xf50e0))(copied,1);
  printf(",\"output\":");words(copied,116);puts("}");
 }
}
int main(int argc,char** argv){
 require(argc==3&&!strcmp(argv[2],"0"),"arguments");setvbuf(stdout,NULL,_IONBF,0);
 image=(void*)LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES);require(image!=NULL,"map-original");
 IMAGE_NT_HEADERS32* pe=(void*)(image+get32(image,0x3c));require(pe->Signature==IMAGE_NT_SIGNATURE&&pe->OptionalHeader.SizeOfImage==0x420000,"image-header");verify_original_code(argv[1],pe);
 image_size=pe->OptionalHeader.SizeOfImage;DWORD old;require(VirtualProtect(image,image_size,PAGE_EXECUTE_READWRITE,&old),"private-protect");relocate(pe);install_patches();
 unsigned short saved,cw=0x7f,after;__asm__ volatile("fnstcw %0":"=m"(saved));__asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(cw));
 unsigned char model[0x1ec]={0};call0(model,0xf44a0);calculate(model,0);volume_controls(model);calculate(model,1);call0(model,0xf44e0);
 for(unsigned id=2;id<7;id++){memset(model,0,sizeof(model));call0(model,0xf44a0);calculate(model,id);if(id==4)calculate(model,7);call0(model,0xf44e0);}
 memset(model,0,sizeof(model));call0(model,0xf44a0);calculate(model,8);call0(model,0xf44e0);
 require(live_count==0,"allocation-leak");__asm__ volatile("fnstcw %0":"=m"(after));require(after==cw,"control-word");__asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(saved));
 puts("{\"type\":\"scope\",\"game_entry_executed\":false,\"allocator_patches\":2}");return 0;
}
