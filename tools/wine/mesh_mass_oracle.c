#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float v[3]; } Vec;
typedef struct { int unused, i[3], pad; float n[3]; } Face;
static void words(const float *v,int n) { for(int i=0;i<n;i++){uint32_t x;memcpy(&x,v+i,4);printf(" %08lx",(unsigned long)x);} }
static void patch(unsigned char *p,void *target) { p[0]=0xe9; int32_t d=(unsigned char*)target-(p+5); memcpy(p+1,&d,4); }
static void *alloc_stock(int n) { return n>0 ? malloc(n):NULL; }
static void free_stock(void *p) { free(p); }
static void run(unsigned char *image,int id) {
 Vec tetra[] = {{{0,0,0}},{{1,0,0}},{{0,1,0}},{{0,0,1}}};
 Face tf[]={{0,{0,2,1},0,{0,0,-1}},{0,{0,1,3},0,{0,-1,0}},{0,{0,3,2},0,{-1,0,0}},{0,{1,2,3},0,{1,1,1}}};
 Vec box[] = {{{2,3,4}},{{4,3,4}},{{4,7,4}},{{2,7,4}},{{2,3,10}},{{4,3,10}},{{4,7,10}},{{2,7,10}}};
 Face bf[]={{0,{0,2,1},0,{0,0,-1}},{0,{0,3,2},0,{0,0,-1}},{0,{4,5,6},0,{0,0,1}},{0,{4,6,7},0,{0,0,1}},{0,{0,1,5},0,{0,-1,0}},{0,{0,5,4},0,{0,-1,0}},{0,{3,7,6},0,{0,1,0}},{0,{3,6,2},0,{0,1,0}},{0,{0,4,7},0,{-1,0,0}},{0,{0,7,3},0,{-1,0,0}},{0,{1,2,6},0,{1,0,0}},{0,{1,6,5},0,{1,0,0}}};
 Vec *v=id?box:tetra; Face *f=id?bf:tf; int nv=id?8:4,nf=id?12:4; unsigned char shape[0xac]={0},body[0xa0]={0};
 *(int*)(shape+0x9c)=nv; *(Vec**)(shape+0xa0)=v; *(int*)(shape+0xa4)=nf; *(Face**)(shape+0xa8)=f; *(void**)(body+0x78)=shape; float center[3]={0}; Vec input[8]; memcpy(input,v,nv*sizeof(Vec));
 unsigned short old,cw=0x007f; __asm__ volatile("fnstcw %0":"=m"(old)); __asm__ volatile("fldcw %0"::"m"(cw)); ((void(*)(void*,float*))(image+0xaa330))(body,center); __asm__ volatile("fldcw %0"::"m"(old));
 printf("case=%d cw=007f vertices",id); words((float*)input,nv*3); printf(" faces"); for(int i=0;i<nf;i++){printf(" %d,%d,%d",f[i].i[0],f[i].i[1],f[i].i[2]);words(f[i].n,3);} printf(" volume"); words((float*)(shape+0x4c),1); printf(" center");words(center,3);printf(" inverse"); words((float*)(body+0x18),12); printf(" integrals"); for(int i=0;i<10;i++)printf(" %016llx",(unsigned long long)*(uint64_t*)(image+0x244208+i*8)); puts("");
}
static int corpus(unsigned char *image,const char *path) {
 FILE *in=fopen(path,"rb");if(!in)return 10;uint32_t count;if(fread(&count,4,1,in)!=1)return 11;
 for(uint32_t id=0;id<count;id++) {uint32_t nv,nf;float inv;
  if(fread(&nv,4,1,in)!=1||fread(&nf,4,1,in)!=1||fread(&inv,4,1,in)!=1||nv>1000000||nf>1000000)return 12;
  Vec *v=calloc(nv,sizeof(*v));Face *f=calloc(nf,sizeof(*f));if(!v||!f)return 13;
  if(fread(v,sizeof(*v),nv,in)!=nv)return 14;
  for(uint32_t i=0;i<nf;i++)if(fread(f[i].i,4,3,in)!=3||fread(f[i].n,4,3,in)!=3)return 15;
  unsigned char shape[0xac]={0},body[0xa0]={0};float center[3]={0};
  *(int*)(shape+0x9c)=nv;*(Vec**)(shape+0xa0)=v;*(int*)(shape+0xa4)=nf;*(Face**)(shape+0xa8)=f;*(void**)(body+0x78)=shape;*(float*)(body+0x14)=inv;
  unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));((void(*)(void*,float*))(image+0xaa330))(body,center);((void(__attribute__((thiscall)) *)(void*,float))(image+0x14e7e0))(body+0x18,inv);__asm__ volatile("fldcw %0"::"m"(old));
  printf("case=%u cw=007f volume",id+2);words((float*)(shape+0x4c),1);printf(" center");words(center,3);printf(" inverse");words((float*)(body+0x18),12);puts("");free(v);free(f);
 }if(fgetc(in)!=EOF)return 16;fclose(in);return 0;
}
int main(int argc,char **argv) {
 if(argc!=2 && argc!=3)return 2; HMODULE m=LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES); if(!m)return 3; unsigned char *image=(unsigned char*)m; IMAGE_NT_HEADERS32 *pe=(void*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew); if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.ImageBase!=0x400000)return 4;
 FILE *file=fopen(argv[1],"rb");if(!file)return 5; IMAGE_SECTION_HEADER *s=IMAGE_FIRST_SECTION(pe); unsigned ranges[][2]={{0xa9710,0xe50},{0x14e3b0,0x200},{0x14e7e0,0x60}};
 for(unsigned k=0;k<3;k++){IMAGE_SECTION_HEADER *sec=NULL;for(unsigned j=0;j<pe->FileHeader.NumberOfSections;j++)if(ranges[k][0]>=s[j].VirtualAddress&&ranges[k][0]+ranges[k][1]<=s[j].VirtualAddress+s[j].SizeOfRawData)sec=&s[j];if(!sec||fseek(file,sec->PointerToRawData+ranges[k][0]-sec->VirtualAddress,SEEK_SET))return 6;for(unsigned j=0;j<ranges[k][1];j++)if(fgetc(file)!=image[ranges[k][0]+j])return 7;} fclose(file);
 DWORD old; if(!VirtualProtect(image,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&old))return 8; IMAGE_DATA_DIRECTORY r=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]; unsigned pos=0,count=0; while(pos<r.Size){IMAGE_BASE_RELOCATION *b=(void*)(image+r.VirtualAddress+pos);if(b->SizeOfBlock<8||b->SizeOfBlock>r.Size-pos)return 9;WORD *e=(WORD*)(b+1);for(unsigned j=0;j<(b->SizeOfBlock-8)/2;j++)if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW){uint32_t *p=(void*)(image+b->VirtualAddress+(e[j]&4095));*p+=(uintptr_t)image-0x400000;count++;}pos+=b->SizeOfBlock;}
 patch(image+0x14f560,alloc_stock);patch(image+0x14f5b0,free_stock);printf("stock_code_verified=1 relocations=%u allocation_hooks=2\n",count);run(image,0);run(image,1);return argc==3 ? corpus(image,argv[2]) : 0;
}
