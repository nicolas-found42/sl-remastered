#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t hash(const unsigned char *p, unsigned n) { uint32_t h=2166136261u; while(n--) h=(h^*p++)*16777619u; return h; }
static void put(void *p,int o,float x) { memcpy((char*)p+o,&x,4); }
static void ptr(void *p,int o,void *v) { memcpy((char*)p+o,&v,4); }
static void word(float x) { uint32_t u; memcpy(&u,&x,4); printf(" %08x",u); }
int main(int argc,char **argv) {
 if(argc!=2) return 2;
 unsigned char *im=(void*)LoadLibraryExA(argv[1],0,DONT_RESOLVE_DLL_REFERENCES); if(!im) return 3;
 if(hash(im+0x96050,0xf90)!=0x0cbe8906u || hash(im+0xa73c0,0x1a0)!=0x3e88f6b2u) return 4;
 IMAGE_NT_HEADERS32 *pe=(void*)(im+((IMAGE_DOS_HEADER*)im)->e_lfanew); DWORD prot;
 VirtualProtect(im,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&prot);
 IMAGE_DATA_DIRECTORY dir=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 for(unsigned pos=0;pos<dir.Size;) { IMAGE_BASE_RELOCATION *r=(void*)(im+dir.VirtualAddress+pos); WORD *e=(void*)(r+1);
  for(unsigned j=0;j<(r->SizeOfBlock-8)/2;j++) if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW) {uint32_t *q=(void*)(im+r->VirtualAddress+(e[j]&4095)); *q+=(uintptr_t)im-0x400000; } pos+=r->SizeOfBlock; }
 for(int k=0;k<21;k++) {
  unsigned char body[4][0x220]={{0}}, proto[4][0xa0]={{0}}, attachment[2][0x50]={{0}}, contact[0xac]={0}, world[0x20]={0};
  for(int i=0;i<4;i++) { ptr(body[i],0x5c,proto[i]); for(int s=0;s<2;s++) { *(uint32_t*)(body[i]+0xa0+s*0xd4)=3; for(int j=0;j<3;j++) put(body[i],0xa4+s*0xd4+j*20,1); } }
  if(k==1) *(uint32_t*)(proto[1]+0x9c)=4;
  if(k==2) *(uint32_t*)(proto[0]+0x9c)=4;
  if(k==3) put(contact,0x8c,0.03f);
  if(k==4) put(body[0],0x7c,0.03f);
  if(k>=6) { put(contact,0x8c,0.03f); put(proto[0],0x14,1); put(proto[1],0x14,k==9?0.5f:1); put(body[0],0x138+0x0c,-2); if(k>=7) { put(body[0],0x138+0x10,1); put(contact,0x94,0.5f); } if(k==8) *(uint32_t*)(proto[1]+0x9c)=4; }
  if(k>=10) {
   for(int i=0;i<2;i++) for(int st=0;st<2;st++) for(int j=0;j<3;j++) put(body[i],0x64+st*0xd4+0xa4+j*20,1);
   put(contact,0x60,1); if(k>=11) put(contact,0x70,0.5f);
   if(k>=12) put(body[0],0x138+0x78,0.5f);
   if(k==13) *(uint32_t*)(proto[1]+0x9c)=4;
   if(k>=14) { for(int st=0;st<2;st++) { int base=0x64+st*0xd4; put(body[0],base+0x40,0); put(body[0],base+0x44,-1); put(body[0],base+0x50,1); put(body[0],base+0x54,0); put(body[0],base+0xa4,0.5f); put(body[0],base+0xb8,2); put(body[0],base+0xcc,0.25f); } }
   if(k==15) { put(contact,0x5c,0.5f); put(contact,0x64,0.5f); put(body[0],0x138+0x70,0.5f); }

  }
  if(k==5) { *(int*)(body[0]+0x20c)=1; *(int*)(body[1]+0x20c)=1; }
  ptr(contact,0x30,body[0]); ptr(contact,0x38,body[1]);
  if(k>=16) {
    for(int i=0;i<2;i++) for(int j=0;j<3;j++) put(attachment[i],0x20+j*20,1);
    put(attachment[0],4,0.5f); put(attachment[0],8,0.25f);
    *(uint32_t*)(body[2]+0x58)=4; ptr(body[2],0x1c,attachment[0]); ptr(body[2],0x20,body[0]); ptr(contact,0x30,body[2]);
    if(k==17 || k==20) *(uint32_t*)(proto[2]+0x9c)=4;
    if(k==18) { *(uint32_t*)(body[3]+0x58)=4; ptr(body[3],0x1c,attachment[1]); ptr(body[3],0x20,body[2]); put(attachment[1],0xc,0.5f); ptr(contact,0x30,body[3]); }
    if(k==19) { put(attachment[0],0x20,-1); put(attachment[0],0x34,-1); }
    if(k==20) { ptr(body[2],0x20,body[1]); ptr(contact,0x30,body[0]); ptr(contact,0x38,body[2]); *(uint32_t*)(proto[1]+0x9c)=4; }
  } *(int*)(contact+0x14)=6; put(contact,0x74,1); ptr(im,0x217650,world); put(world,0xc,0.01f);
  unsigned short old,cw=0x007f; __asm__ volatile("fnstcw %0":"=m"(old)); __asm__ volatile("fldcw %0"::"m"(cw));
  int result=((int (*)(void*))(im+0x96050))(contact); __asm__ volatile("fldcw %0"::"m"(old));
  printf("case=%d result=%d world=%u",k,result,*(unsigned*)(world+0x14));
  for(int i=0;i<4;i++) { int s=*(int*)(body[i]+0x20c); float x; for(int j=0;j<3;j++){ memcpy(&x,body[i]+0x7c+s*0xd4+j*4,4); word(x); } printf(" %u",*(unsigned*)(body[i]+0xa0+(s^1)*0xd4)); } for(int i=0;i<4;i++) for(int j=0;j<3;j++) for(int f=0;f<5;f++) {float x; int offs[]={0x0c,0x28,0x70,0x7c,0x88}; memcpy(&x,body[i]+0x64+offs[f]+j*4,4); word(x);} for(int j=0;j<2;j++){ float x; memcpy(&x,contact+(j?0xa8:0xa0),4); word(x); } puts("");
 }
 return 0;
}
