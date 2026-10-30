#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {float x,y,z;} Vec;
typedef struct Feature Feature;
typedef struct {Feature *neighbor; Vec *anchor; float n[3];} Adj;
struct Feature {int kind; Vec *anchor; float n[3]; int count,capacity; Adj *adj;};
typedef struct {int nv; Vec *v; int nf,capacity; Feature *f; int flags;} Core;
static void put(void*p,int o,float x){memcpy((char*)p+o,&x,4);} static void ptr(void*p,int o,void*v){memcpy((char*)p+o,&v,4);}
static void patch(unsigned char*p,void*t){p[0]=0xe9;int32_t d=(unsigned char*)t-(p+5);memcpy(p+1,&d,4);}
static void *alloc_stock(int n){return n>0?calloc(1,n):NULL;} static void free_stock(void*p){free(p);} static void *realloc_stock(void*p,int n){return realloc(p,n);}
static int __attribute__((thiscall)) valid(void*p){(void)p;return 1;}
static void word(float v){uint32_t x;memcpy(&x,&v,4);printf(" %08x",x);}
int main(int argc,char**argv){
 if(argc!=2)return 2; unsigned char*im=(void*)LoadLibraryExA(argv[1],0,DONT_RESOLVE_DLL_REFERENCES);if(!im)return 3;
 IMAGE_NT_HEADERS32*pe=(void*)(im+((IMAGE_DOS_HEADER*)im)->e_lfanew);FILE*f=fopen(argv[1],"rb");if(!f)return 4;
 IMAGE_SECTION_HEADER*sections=IMAGE_FIRST_SECTION(pe);const unsigned ranges[][2]={{0xa2d10,0x880},{0xae9c0,0xf40},{0xaf970,0x970},{0xa8410,0xb0},{0x150270,0x115},{0x14eda0,0xd5},{0xac370,0xd0},{0xaa560,0x200},{0x14e3b0,0x200}};
 for(unsigned k=0;k<sizeof ranges/sizeof ranges[0];k++){IMAGE_SECTION_HEADER*s=NULL;for(unsigned j=0;j<pe->FileHeader.NumberOfSections;j++)if(ranges[k][0]>=sections[j].VirtualAddress && ranges[k][0]+ranges[k][1]<=sections[j].VirtualAddress+sections[j].SizeOfRawData)s=&sections[j];if(!s||fseek(f,s->PointerToRawData+ranges[k][0]-s->VirtualAddress,SEEK_SET))return 5;for(unsigned j=0;j<ranges[k][1];j++)if(fgetc(f)!=im[ranges[k][0]+j])return 6;}fclose(f);
 DWORD prot;VirtualProtect(im,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&prot);IMAGE_DATA_DIRECTORY dir=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 for(unsigned pos=0;pos<dir.Size;){IMAGE_BASE_RELOCATION*r=(void*)(im+dir.VirtualAddress+pos);WORD*e=(void*)(r+1);for(unsigned j=0;j<(r->SizeOfBlock-8)/2;j++)if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW){uint32_t*q=(void*)(im+r->VirtualAddress+(e[j]&4095));*q+=(uintptr_t)im-0x400000;}pos+=r->SizeOfBlock;}
 patch(im+0x14f560,alloc_stock);patch(im+0x14f5b0,free_stock);patch(im+0x14f580,realloc_stock);((void(*)(void))(im+0xaa560))();
 for(int k=0;k<21;k++){
  struct {char pad[32];Core core;} shape[2]={{0}};unsigned char body[2][0x220]={{0}},proto[2][0xa0]={{0}},contact[0xac]={0},sentinel[12]={0},world[0x200]={0};void*vt[7]={0};vt[6]=valid;ptr(contact,0,vt);ptr(contact,4,sentinel);
  for(int i=0;i<2;i++){ptr(body[i],0x5c,proto[i]);ptr(proto[i],0x78,&shape[i]);for(int st=0;st<2;st++){for(int j=0;j<3;j++)put(body[i],0x64+st*0xd4+0x40+j*20,1);*(uint32_t*)(body[i]+0xa0+st*0xd4)=3; put(body[i],0x64+st*0xd4+0xa0,1);}}
  put(shape[0].pad,0x10,k==4?0:0.5f);put(shape[1].pad,0x10,0.5f);put(body[0],0x138,k==4?0.53f:1.03f);
  if(k==1||k==3){put(body[0],0x138+0x40,0);put(body[0],0x138+0x44,-1);put(body[0],0x138+0x50,1);put(body[0],0x138+0x54,0);}
  if(k>=2){put(body[0],0x138,0);put(body[0],0x138+4,k==4?0.53f:1.03f);} if(k==5){put(shape[0].pad,0x10,0);put(shape[1].pad,0x10,0);put(body[0],0x138+4,0.03f);}
  if(k>=6) { *(uint32_t*)(body[0]+0x58)=1; *(uint32_t*)(body[1]+0x58)=1; put(body[0],0x64,1.2f);put(body[0],0x138,k==6?1.01f:k==7?0.9f:k==8?0.5f:1.0f);put(body[0],0x138+4,0);*(uint32_t*)(world+0x1ec)=5; if(k==10)put(body[0],0x64,0.8f); if(k==11){*(uint32_t*)(proto[1]+0x9c)=4; *(uint32_t*)(body[1]+0x58)=0;} if(k==13){int base=0x138;put(body[0],base+0x40,1-2*0.6f*0.6f);put(body[0],base+0x44,-2*0.6f*0.8f);put(body[0],base+0x50,2*0.6f*0.8f);put(body[0],base+0x54,1-2*0.6f*0.6f);put(body[0],base+0x9c,0.6f);put(body[0],base+0xa0,0.8f);} if(k==12) for(int st=0;st<2;st++){int base=0x64+st*0xd4;put(body[0],base+0x40,-1);put(body[0],base+0x54,-1);put(body[0],base+0x9c,1);put(body[0],base+0xa0,0);} }
  if(k>=14 && k<=16){int id=k==15?1:0; int base=0x138; put(body[id],base+0x94,k==14?0.6f:0);put(body[id],base+0x98,k==15?0.6f:0);put(body[id],base+0x9c,k==16?-0.6f:0);put(body[id],base+0xa0,k==16?-0.8f:0.8f);((void(__attribute__((thiscall)) *)(void*,void*))(im+0x14eda0))(body[id]+base+0x40,body[id]+base+0x94); }
  if(k==17){*(uint32_t*)(proto[1]+0x9c)=4; *(uint32_t*)(body[1]+0x58)=0; put(body[1],0x64,-0.25f);put(body[1],0x138,-0.25f);put(body[0],0x64,0.95f);put(body[0],0x138,0.65f);}
  if(k>=18){put(body[0],0x64,0.2f);put(body[0],0x138,0.2f);put(body[0],0x138+4,0.2f);put(body[0],0x138+8,0.2f);}
  if(k==19){put(body[0],0x64+4,0.2f);put(body[0],0x64+8,0.2f);put(contact,0x8c,2);}
  if(k==20){put(body[0],0x138+0x40,-1);put(body[0],0x138+0x54,-1);put(body[0],0x138+0x9c,1);put(body[0],0x138+0xa0,0);}
  if(k>=18)put(contact,0x80,7);
  ptr(contact,0x30,body[0]);ptr(contact,0x38,body[1]);ptr(world,0x118,contact);*(int*)(contact+0x14)=6;
  unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
  ((void(__attribute__((thiscall)) *)(Core*,int))(im+0xac370))(&shape[0].core,0);
  if(k>=18) ((void(__attribute__((thiscall)) *)(Core*,float,float,float))(im+0xac1c0))(&shape[1].core,1,2,3);
  else ((void(__attribute__((thiscall)) *)(Core*,int))(im+0xac370))(&shape[1].core,0);
  ptr(contact,0x4c,shape[0].core.f); ptr(contact,0x50,k>=18?&shape[1].core.f[shape[1].core.nf-1]:shape[1].core.f);
  *(int*)(im+0x24464c)=0;((void(__attribute__((fastcall)) *)(void*))(im+0xa2d10))(world);__asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
  printf("%d",k);for(int off=0x5c;off<=0x8c;off+=4){float v;memcpy(&v,contact+off,4);word(v);}printf(" %u %u %u %u\n",*(unsigned*)(body[0]+0x174),*(unsigned*)(body[1]+0x174),*(unsigned*)(world+0x28),*(unsigned*)(world+0x1ec));
 }
 for(int k=0;k<8;k++){
  unsigned char in[2][0xd4]={{0}},out[0xd4]={0};
  float t=(k%4==0?0.5f:k%4==1?0.75f:k%4==2?0.625f:0.5625f);
  put(in[0],0x94+4*(k/4?0:2),k/4?0.6f:-0.6f);put(in[0],0xa0,k/4?0.8f:-0.8f);put(in[1],0xa0,1);
  unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
  ((void(*)(void*,void*,void*,float))(im+0xa8410))(in[0],in[1],out,t);
  __asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
  printf("sample %d",k);for(int i=0;i<4;i++){float x;memcpy(&x,out+0x94+i*4,4);word(x);}for(int i=0;i<3;i++)for(int j=0;j<3;j++){float x;memcpy(&x,out+0x40+i*16+j*4,4);word(x);}puts("");
 }
 return 0;
}
