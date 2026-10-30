#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define MIX(v) do { uint32_t x=(v); printf("%08x ", x); h=(h^x)*16777619u; } while(0)
static void * __attribute__((cdecl)) rr(void *p, unsigned n) { return realloc(p,n); }
static void patch(unsigned char* p, void* f) { p[0]=0xe9; *(int32_t*)(p+1)=(int32_t)((uintptr_t)f-(uintptr_t)p-5); }
int main(int argc,char**argv){
 if(argc!=3)return 2;
 unsigned char* im=(void*)LoadLibraryExA(argv[1],0,DONT_RESOLVE_DLL_REFERENCES); if(!im)return 3;
 IMAGE_NT_HEADERS32 *pe=(void*)(im+((IMAGE_DOS_HEADER*)im)->e_lfanew); DWORD prot; VirtualProtect(im,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&prot);
 FILE *exe=fopen(argv[1],"rb"); if(!exe)return 5;
 IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(pe);
 unsigned ranges[][2]={{0x17ba20,0x1a0},{0x17b220,0xa0},{0x179960,0x1a0},{0x17d970,0xe0},{0x17f1a0,0x1c0},{0x1d6780,0x80},{0x14f580,0x40}};
 for(unsigned k=0;k<sizeof(ranges)/sizeof(ranges[0]);k++){
  IMAGE_SECTION_HEADER *sec=0;
  for(unsigned j=0;j<pe->FileHeader.NumberOfSections;j++)
   if(ranges[k][0]>=sections[j].VirtualAddress && ranges[k][0]+ranges[k][1]<=sections[j].VirtualAddress+sections[j].SizeOfRawData)sec=&sections[j];
  if(!sec || fseek(exe,sec->PointerToRawData+ranges[k][0]-sec->VirtualAddress,SEEK_SET))return 6;
  for(unsigned j=0;j<ranges[k][1];j++)if(fgetc(exe)!=im[ranges[k][0]+j])return 7;
 }
 fclose(exe);
 IMAGE_DATA_DIRECTORY dir=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 for(unsigned p=0;p<dir.Size;){IMAGE_BASE_RELOCATION*r=(void*)(im+dir.VirtualAddress+p);WORD*e=(void*)(r+1);for(unsigned j=0;j<(r->SizeOfBlock-8)/2;j++)if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW){uint32_t*q=(void*)(im+r->VirtualAddress+(e[j]&4095));*q+=(uintptr_t)im-0x400000;}p+=r->SizeOfBlock;}
 patch(im+0x14f580,rr);
 FILE*f=fopen(argv[2],"rb");if(!f)return 4;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);unsigned char*buf=malloc(n);fread(buf,1,n,f);fclose(f);
 uint32_t map[3000];for(int i=0;i<3000;i++)map[i]=i;unsigned char*cur=buf+20;
 unsigned short cw=0x007f; __asm__ volatile("fldcw %0"::"m"(cw));
 for(int group=0;group<4;group++){unsigned count=*(uint32_t*)(buf+4+group*4),segments=0;uint32_t h=2166136261u;
 for(unsigned j=0;j<count;j++){unsigned char node[0x100]={0};unsigned addr=group<2?0x17ba20:group==2?0x17b220:0x179960;
 ((void (__attribute__((thiscall)) *)(void*,void*,void*))(im+addr))(node,&cur,map);
 if(group<2){unsigned n=*(uint32_t*)(node+0x20);segments+=n;unsigned char*base=*(unsigned char**)(node+0x1c);for(unsigned k=0;k<n;k++){unsigned char*p=base+k*0x60;for(int i=0;i<4;i++)MIX(p[i]);for(int i=0;i<2;i++)MIX(*(uint32_t*)(p+0x58+i*4));for(int i=0;i<14;i++)MIX(*(uint32_t*)(p+4+i*4));MIX(p[0x3c]);}free(*(void**)(node+0x14));free(base);}
 else {unsigned offset=group==2?0x28:0x3c;unsigned n=*(uint32_t*)(node+offset);segments+=n;unsigned char*base=*(unsigned char**)(node+offset+4);
 if(group==2){for(int i=0;i<3;i++)MIX(*(uint32_t*)(node+4+i*4));for(int i=0;i<6;i++)MIX(*(uint32_t*)(node+0x10+i*4));MIX(*(uint32_t*)(node+0x64));}
 else {for(int i=0;i<2;i++)MIX(*(uint32_t*)(node+0x10+i*4));MIX(node[0x18]);for(int i=0;i<8;i++)MIX(*(uint32_t*)(node+0x1c+i*4));for(int i=0;i<4;i++)MIX(*(uint32_t*)(node+0x78+i*4));for(int i=0;i<4;i++)MIX(*(uint32_t*)(node+0x88+i*4));for(int i=0;i<3;i++)MIX(*(uint32_t*)(node+0x98+i*4));}
 for(unsigned k=0;k<n;k++){unsigned char*p=base+k*0x54;for(int i=0;i<14;i++)MIX(*(uint32_t*)(p+i*4));MIX(p[0x38]);}free(base);}
 }
 printf("group=%d records=%u segments=%u cursor=%lu hash=%08x\n",group,count,segments,(unsigned long)(cur-buf),h);}
 return 0;}
