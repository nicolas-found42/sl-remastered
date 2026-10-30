#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void *alloc_stock(int n) { return calloc(1,n); }
static void patch(unsigned char *p, void *target) {p[0]=0xe9;int32_t d=(unsigned char*)target-(p+5);memcpy(p+1,&d,4);}
static void run(unsigned char *image, int id) {
 float target[12]={1,0,0,0,0,1,0,0,0,0,1,0};
 float ts[12]={1,0,0,0,0,1,0,0,0,0,1,0};
 float ps[12]={1,0,0,0,0,1,0,0,0,0,1,0};
 const float z[12]={0,-1,0,0,1,0,0,0,0,0,1,0};
 const float x[12]={1,0,0,0,0,0,-1,0,0,1,0,0};
 if(id==1) {memcpy(target,z,sizeof target); memcpy(ts,x,sizeof ts); memcpy(ps,z,sizeof ps);}
 if(id==2) {
  const float a[12]={0.6f,-0.8f,0,0,0.8f,0.6f,0,0,0,0,1,0};
  const float b[12]={1,0,0,0,0,0.6f,-0.8f,0,0,0.8f,0.6f,0};
  const float c[12]={0.8f,0,-0.6f,0,0,1,0,0,0.6f,0,0.8f,0};
  memcpy(target,a,sizeof a); memcpy(ts,b,sizeof b); memcpy(ps,c,sizeof c);
 }
 float targetpos[3]={3,-4,7}, targetslot[3]={2,3,-1}, partslot[3]={-2,1,4}, tmp[3],trans[12];
 if (id==2) { float p[3]={1.25f,-3.75f,2.125f}, t[3]={0.3125f,-1.375f,2.625f}, s[3]={-0.875f,1.625f,0.4375f}; memcpy(targetpos,p,sizeof p); memcpy(targetslot,t,sizeof t); memcpy(partslot,s,sizeof s); }
 unsigned short old,cw=0x007f; __asm__ volatile("fnstcw %0":"=m"(old)); __asm__ volatile("fldcw %0"::"m"(cw));
 void (__attribute__((thiscall)) *mult)(float*,float*)=(void*)(image+0x14ebc0);
 void (__attribute__((thiscall)) *transpose)(float*,float*)=(void*)(image+0x14e4a0);
 void (__attribute__((thiscall)) *vector)(float*,float*,float*)=(void*)(image+0x36880);
 vector(tmp,target,targetslot);
 for(int i=0;i<3;i++) targetpos[i]+=tmp[i];
 mult(target,ts); transpose(trans,ps); mult(target,trans); vector(tmp,target,partslot);
 printf("case=%d",id); for(int i=0;i<3;i++) {float v=targetpos[i]-tmp[i]; uint32_t u; memcpy(&u,&v,4); printf(" %08x",u);}
 for(int i=0;i<12;i++) if(i%4!=3) {uint32_t u; memcpy(&u,&target[i],4); printf(" %08x",u);} puts("");
 __asm__ volatile("fldcw %0"::"m"(old));
}
int main(int argc,char **argv) {
 if(argc!=2)return 2; HMODULE m=LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES);if(!m)return 3;unsigned char *image=(void*)m;IMAGE_NT_HEADERS32 *pe=(void*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.ImageBase!=0x400000)return 4;
 FILE *f=fopen(argv[1],"rb");if(!f)return 5;IMAGE_SECTION_HEADER *s=IMAGE_FIRST_SECTION(pe);unsigned verified=0;
 for(unsigned i=0;i<pe->FileHeader.NumberOfSections;i++) if(s[i].Characteristics & IMAGE_SCN_CNT_CODE) {if(fseek(f,s[i].PointerToRawData,SEEK_SET))return 6;for(unsigned j=0;j<s[i].SizeOfRawData;j++)if(fgetc(f)!=image[s[i].VirtualAddress+j])return 7;verified+=s[i].SizeOfRawData;} fclose(f);
 DWORD old;if(!VirtualProtect(image,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&old))return 8; IMAGE_DATA_DIRECTORY r=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];unsigned pos=0,count=0;while(pos<r.Size){IMAGE_BASE_RELOCATION *b=(void*)(image+r.VirtualAddress+pos);if(b->SizeOfBlock<8||b->SizeOfBlock>r.Size-pos)return 9;WORD *e=(void*)(b+1);for(unsigned j=0;j<(b->SizeOfBlock-8)/2;j++)if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW){uint32_t *p=(void*)(image+b->VirtualAddress+(e[j]&4095));*p+=(uintptr_t)image-0x400000;count++;}pos+=b->SizeOfBlock;}
 patch(image+0x14f560,alloc_stock);printf("verified_code_bytes=%u relocations=%u\n",verified,count);for(int id=0;id<3;id++)run(image,id);return 0;
}
