#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void ident(void *v) { float *m=v; m[0]=m[5]=m[10]=1; }
static void run(unsigned char *image, int nested) {
 unsigned char world[0x200]={0}, body[3][0x218]={{0}}, proto[3][0xa0]={{0}}, rel[2][0x50]={{0}};
 for(int i=0;i<3;i++) {
  *(void**)(body[i]+0x5c)=proto[i]; *(uint32_t*)(body[i]+0x58)=i?5:0x109;
  *(float*)(proto[i]+0x14)=1; *(uint32_t*)(proto[i]+0x9c)=0x200;
  for(int j=0;j<2;j++) {unsigned char *s=body[i]+0x64+j*0xd4; ident(s+0x40); *(float*)(s+0xa0)=1;}
 }
 *(float*)(body[0]+0x64+0xc)=2; *(float*)(body[0]+0x64+0x10)=1;
 *(float*)(body[0]+0x64+0x70)=0.25f; *(float*)(body[0]+0x138+0x70)=0.25f;
 *(void**)(body[0]+0x24)=body[1]; *(void**)(body[1]+0x20)=body[0]; *(void**)(body[1]+0x1c)=rel[0];
 *(float*)(rel[0]+4)=2; *(float*)(rel[0]+8)=1; *(float*)(rel[0]+0x1c)=1; ident(rel[0]+0x20);
 if(nested) { *(uint32_t*)(body[1]+0x58)|=8; *(void**)(body[1]+0x24)=body[2]; *(void**)(body[2]+0x20)=body[1]; *(void**)(body[2]+0x1c)=rel[1];
  *(float*)(rel[0]+0x18)=0.70710677f; *(float*)(rel[0]+0x1c)=0.70710677f;
  float *m=(float*)(rel[0]+0x20);m[0]=0x1p-24f;m[1]=-0x1.fffffep-1f;m[4]=0x1.fffffep-1f;m[5]=0x1p-24f;
  *(float*)(rel[1]+4)=1; *(float*)(rel[1]+8)=3; *(float*)(rel[1]+0x1c)=1; ident(rel[1]+0x20);
 }
 unsigned short old,cw=0x007f; __asm__ volatile("fnstcw %0":"=m"(old)); __asm__ volatile("fldcw %0"::"m"(cw));
 ((void(__attribute__((thiscall)) *)(void*,void*,float))(image+0xa3bc0))(world,body[0],0.25f);
 ((void(__attribute__((thiscall)) *)(void*,void*))(image+0xa4ed0))(world,body[0]);
 __asm__ volatile("fldcw %0"::"m"(old));
 for(int i=0;i<(nested?3:2);i++) {printf("case=%d body=%d current=%lu state",nested,i,(unsigned long)*(uint32_t*)(body[i]+0x20c)); uint32_t *s=(void*)(body[i]+0x64+*(uint32_t*)(body[i]+0x20c)*0xd4);for(int j=0;j<53;j++)printf(" %08lx",(unsigned long)s[j]);puts("");}
}
static void trajectory(unsigned char *image) {
 unsigned char world[0x200]={0}, body[3][0x218]={{0}}, proto[3][0xa0]={{0}}, rel[2][0x50]={{0}};
 *(uint32_t*)(world+0x78)=1; *(float*)(world+0x108)=-9.81f; *(float*)(world+0x1b4)=1.2928f;
 for(int i=0;i<3;i++){*(void**)(body[i]+0x5c)=proto[i];*(uint32_t*)(body[i]+0x58)=i?5:9;*(float*)(proto[i]+0x14)=1;*(uint32_t*)(proto[i]+0x9c)=0x200;for(int j=0;j<2;j++){unsigned char *s=body[i]+0x64+j*0xd4;ident(s+0x40);*(float*)(s+0xa0)=1;}}
 ident(proto[0]+0x18); ident(body[0]+0x64+0xa4); ident(body[0]+0x138+0xa4); *(float*)(body[0]+0x64+0x7c)=0.25f; *(float*)(proto[0]+0x80)=-0.005f; *(float*)(body[0]+0x64+0xc)=2;*(float*)(body[0]+0x64+0x10)=1;
 *(float*)(body[0]+0x64+0x70)=0.25f;*(float*)(body[0]+0x138+0x70)=0.25f;
 *(void**)(body[0]+0x24)=body[1];*(void**)(body[1]+0x20)=body[0];*(void**)(body[1]+0x1c)=rel[0];*(uint32_t*)(body[1]+0x58)|=8;
 *(float*)(rel[0]+4)=2;*(float*)(rel[0]+8)=1;*(float*)(rel[0]+0x18)=0.70710677f;*(float*)(rel[0]+0x1c)=0.70710677f;ident(rel[0]+0x20);
 float *m=(float*)(rel[0]+0x20);m[0]=0x1p-24f;m[1]=-0x1.fffffep-1f;m[4]=0x1.fffffep-1f;m[5]=0x1p-24f;
 *(void**)(body[1]+0x24)=body[2];*(void**)(body[2]+0x20)=body[1];*(void**)(body[2]+0x1c)=rel[1];*(float*)(rel[1]+4)=1;*(float*)(rel[1]+8)=3;*(float*)(rel[1]+0x1c)=1;ident(rel[1]+0x20);
 unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
 for(int n=1;n<=100;n++){
  ((void(__attribute__((thiscall)) *)(void*,void*))(image+0xa3f90))(world,body[0]);
  printf("force step=%d",n);for(int i=0;i<3;i++){uint32_t *v=(void*)(body[0]+0x64+*(uint32_t*)(body[0]+0x20c)*0xd4+0x28+i*4);printf(" %08lx",(unsigned long)*v);}puts("");
  ((void(__attribute__((thiscall)) *)(void*,void*,float))(image+0xa3bc0))(world,body[0],0.01f);
  ((void(__attribute__((thiscall)) *)(void*,void*))(image+0xa4ed0))(world,body[0]);
  for(int i=0;i<3;i++){printf("trajectory step=%d body=%d current=%lu",n,i,(unsigned long)*(uint32_t*)(body[i]+0x20c));uint32_t *v=(void*)(body[i]+0x64+*(uint32_t*)(body[i]+0x20c)*0xd4);for(int j=0;j<53;j++)printf(" %08lx",(unsigned long)v[j]);puts("");}
 }
 __asm__ volatile("fldcw %0"::"m"(old));
}
static void sleepcase(unsigned char *image) {
 unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
 unsigned char world[0x200]={0}, callbacks[16]={0}, active[0x218]={0}, inactive[0x218]={0}, body[3][0x218]={{0}};
 *(void**)(image+0x217650)=world;*(void**)(world+0x84)=active;*(void**)(world+0x88)=inactive;*(void**)(world+0xd4)=callbacks;
 for(int i=0;i<3;i++){*(uint32_t*)(body[i]+0x58)=i?5:9;*(uint32_t*)(body[i]+0x20c)=i&1;for(int j=0;j<2;j++){uint32_t *v=(void*)(body[i]+0x64+j*0xd4);for(int k=0;k<53;k++)v[k]=0x3f000000+i*0x1000+j*0x100+k;v[15]=3;}for(int k=0;k<3;k++)*(uint32_t*)(body[i]+0xc+k*4)=0x3f100000+i*0x100+k;}
 *(uint32_t*)(body[1]+0x58)|=8;*(void**)(body[0]+0x24)=body[1];*(void**)(body[1]+0x20)=body[0];*(void**)(body[1]+0x24)=body[2];*(void**)(body[2]+0x20)=body[1];
 for(int phase=0;phase<5;phase++){
  if(phase==1||phase==2)((void(__attribute__((fastcall)) *)(void*))(image+0xa7a80))(body[0]);
  if(phase==3||phase==4)((void(__attribute__((fastcall)) *)(void*))(image+0xa7990))(body[0]);
  for(int i=0;i<3;i++){printf("sleep phase=%d body=%d flags=%08lx current=%lu",phase,i,(unsigned long)*(uint32_t*)(body[i]+0x58),(unsigned long)*(uint32_t*)(body[i]+0x20c));uint32_t *a=(void*)(body[i]+0xc);for(int j=0;j<3;j++)printf(" %08lx",(unsigned long)a[j]);uint32_t *v=(void*)(body[i]+0x64);for(int j=0;j<106;j++)printf(" %08lx",(unsigned long)v[j]);puts("");}
 }
 memset(body,0,sizeof body);unsigned char proto[3][0xa0]={{0}}, rel[2][0x50]={{0}};
 for(int i=0;i<3;i++){*(void**)(body[i]+0x5c)=proto[i];*(uint32_t*)(body[i]+0x58)=i?5:0x109;*(float*)(proto[i]+0x14)=1;*(uint32_t*)(proto[i]+0x9c)=0x200;for(int j=0;j<2;j++){unsigned char *v=body[i]+0x64+j*0xd4;ident(v+0x40);*(float*)(v+0xa0)=1;}}
 *(uint32_t*)(body[1]+0x58)|=8;*(void**)(body[0]+0x24)=body[1];*(void**)(body[1]+0x20)=body[0];*(void**)(body[1]+0x1c)=rel[0];*(void**)(body[1]+0x24)=body[2];*(void**)(body[2]+0x20)=body[1];*(void**)(body[2]+0x1c)=rel[1];
 for(int i=0;i<2;i++){*(float*)(rel[i]+0x1c)=1;ident(rel[i]+0x20);}
 ((void(__attribute__((thiscall)) *)(void*,void*,float))(image+0xa3bc0))(world,body[0],0.25f);
 ((void(__attribute__((thiscall)) *)(void*,void*))(image+0xa4ed0))(world,body[0]);
 for(int i=0;i<3;i++){printf("sleepstep body=%d flags=%08lx current=%lu",i,(unsigned long)*(uint32_t*)(body[i]+0x58),(unsigned long)*(uint32_t*)(body[i]+0x20c));uint32_t *a=(void*)(body[i]+0xc);for(int j=0;j<3;j++)printf(" %08lx",(unsigned long)a[j]);uint32_t *v=(void*)(body[i]+0x64);for(int j=0;j<106;j++)printf(" %08lx",(unsigned long)v[j]);puts("");}

 __asm__ volatile("fldcw %0"::"m"(old));
}
int main(int argc,char **argv) {
 if(argc!=2)return 2; HMODULE m=LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES);if(!m)return 3;unsigned char *image=(void*)m;IMAGE_NT_HEADERS32 *pe=(void*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.ImageBase!=0x400000)return 4;
 FILE *f=fopen(argv[1],"rb");if(!f)return 5;IMAGE_SECTION_HEADER *s=IMAGE_FIRST_SECTION(pe);unsigned verified=0;
 for(unsigned i=0;i<pe->FileHeader.NumberOfSections;i++) if(s[i].Characteristics & IMAGE_SCN_CNT_CODE) {if(fseek(f,s[i].PointerToRawData,SEEK_SET))return 6;for(unsigned j=0;j<s[i].SizeOfRawData;j++)if(fgetc(f)!=image[s[i].VirtualAddress+j])return 7;verified+=s[i].SizeOfRawData;} fclose(f);
 DWORD old;if(!VirtualProtect(image,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&old))return 8; IMAGE_DATA_DIRECTORY r=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];unsigned pos=0,count=0;while(pos<r.Size){IMAGE_BASE_RELOCATION *b=(void*)(image+r.VirtualAddress+pos);if(b->SizeOfBlock<8||b->SizeOfBlock>r.Size-pos)return 9;WORD *e=(void*)(b+1);for(unsigned j=0;j<(b->SizeOfBlock-8)/2;j++)if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW){uint32_t *p=(void*)(image+b->VirtualAddress+(e[j]&4095));*p+=(uintptr_t)image-0x400000;count++;}pos+=b->SizeOfBlock;}
 *(uint32_t*)(image+0x3a0560)|=1; printf("verified_code_bytes=%u relocations=%u\n",verified,count);run(image,0);run(image,1);trajectory(image);sleepcase(image);return 0;
}
