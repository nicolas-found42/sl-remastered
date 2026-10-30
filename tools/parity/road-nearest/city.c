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
 unsigned ranges[][2]={{0x17ba20,0x1a0},{0x17b220,0xa0},{0x179960,0x1a0},{0x17d970,0xe0},{0x17f1a0,0x1c0},{0x1d6780,0x80},{0x14f580,0x40},{0x181a00,0x170}};
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
 unsigned n0=*(uint32_t*)(buf+4),n1=*(uint32_t*)(buf+8);unsigned char *p0=calloc(n0,0x80),*p1=calloc(n1,0x80);
 for(unsigned group=0;group<2;group++){unsigned n=group?n1:n0;unsigned char*base=group?p1:p0;for(unsigned i=0;i<n;i++)((void (__attribute__((thiscall)) *)(void*,void*,void*))(im+0x17ba20))(base+i*0x80,&cur,map);}
 unsigned char graph[0x80]={0}; unsigned char* pages0[1]={p0},*pages1[1]={p1};
 *(void**)(graph+0x10)=pages0;*(unsigned*)(graph+0x14)=n0;*(unsigned*)(graph+0x1c)=n0;*(unsigned*)(graph+0x20)=0x80;
 *(void**)(graph+0x28)=pages1;*(unsigned*)(graph+0x2c)=n1;*(unsigned*)(graph+0x34)=n1;*(unsigned*)(graph+0x38)=0x80;
 float q[][5]={{0,0,0,0,0},{0,0,0,100,0},{20,3,-400,20,0},{0,1000,0,0,1},{0,0,0,0,1},{10,-2,20,-10,1}};
 for(int i=0;i<6;i++){unsigned char* found=((unsigned char* (__attribute__((thiscall)) *)(void*,float,float,float,float,char))(im+0x181a00))(graph,q[i][0],q[i][1],q[i][2],q[i][3],(char)q[i][4]);int idx=-1;if(found>=p0&&found<p0+n0*0x80)idx=(found-p0)/0x80;else if(found>=p1&&found<p1+n1*0x80)idx=n0+(found-p1)/0x80;printf("case=%d index=%d\n",i,idx);}
 return 0;}
