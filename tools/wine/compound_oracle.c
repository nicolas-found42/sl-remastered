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

static void ident(unsigned char *p) { float *m=(float*)p; m[0]=m[5]=m[10]=1; }
static void run(unsigned char *image,int id) {
 unsigned char root[0x218]={0}, children[2][0x218]={{0}}, protos[3][0xa0]={{0}}, shapes[3][0xac]={{0}}, poses[2][0x50]={{0}};
 *(void**)(root+0x5c)=protos[0]; *(void**)(root+0x24)=children[1]; *(void**)(children[1]+0x28)=children[0];
 for(int i=0;i<3;i++){ *(void**)(protos[i]+0x78)=shapes[i]; *(int*)(shapes[i]+8)=1; *(float*)(shapes[i]+0xc)=1; }
 for(int i=0;i<2;i++) { *(void**)(children[i]+0x5c)=protos[i+1]; *(void**)(children[i]+0x1c)=poses[i]; *(float*)(protos[i+1]+0x14)=i?1:0.5f; ident(protos[i+1]+0x48); ident(poses[i]+0x20); *(float*)(poses[i]+0x1c)=1; }
 if(id & 1){ float *m=(float*)(poses[1]+0x20); m[0]=0;m[1]=-1;m[4]=1;m[5]=0; float *q=(float*)(protos[2]+0x48); q[0]=2;q[5]=3;q[10]=4; }
 if(id>=2) { *(int*)(shapes[0]+8)=6; for(int i=0;i<2;i++){ ident(children[i]+0xa4); ident(children[i]+0x178); *(float*)(children[i]+0x104)=1; *(float*)(children[i]+0x1d8)=1; *(float*)(children[i]+0x64)=i?3:0; *(float*)(shapes[i+1]+0x10)=1; } }
 if(id==3){uint32_t *m=(void*)(poses[1]+0x20);m[0]=0x33800000;m[1]=0xbf7fffff;m[4]=0x3f7fffff;m[5]=0x33800000;}
 *(float*)(poses[1]+4)=3;
 ident(root+0xa4); ident(root+0x178); *(float*)(root+0x104)=1; *(float*)(root+0x1d8)=1;
 *(uint32_t*)(image+0x240bd0)|=1;
 unsigned short old,cw=0x007f; __asm__ volatile("fnstcw %0":"=m"(old)); __asm__ volatile("fldcw %0"::"m"(cw)); ((void(__attribute__((stdcall)) *)(void*))(image+0x9a050))(root); __asm__ volatile("fldcw %0"::"m"(old));
 printf("case=%d cw=007f mass",id); words((float*)(protos[0]+0x14),1); printf(" inertia"); words((float*)(protos[0]+0x48),12); printf(" center");words((float*)(shapes[0]+0x40),3);printf(" root");words((float*)(root+0x64),3); printf(" child0");words((float*)(poses[0]+4),3);printf(" child1");words((float*)(poses[1]+4),3);printf(" bound");words((float*)(shapes[0]+0xc),1);puts("");
}
int main(int argc,char **argv) {
 if(argc!=2)return 2; HMODULE m=LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES); if(!m)return 3; unsigned char *image=(unsigned char*)m; IMAGE_NT_HEADERS32 *pe=(void*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew); if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.ImageBase!=0x400000)return 4;
 FILE *file=fopen(argv[1],"rb");if(!file)return 5; IMAGE_SECTION_HEADER *s=IMAGE_FIRST_SECTION(pe); unsigned ranges[][2]={{0x98f50,0x1900},{0x14e1b0,0x1e00}};
 for(unsigned k=0;k<2;k++){IMAGE_SECTION_HEADER *sec=NULL;for(unsigned j=0;j<pe->FileHeader.NumberOfSections;j++)if(ranges[k][0]>=s[j].VirtualAddress&&ranges[k][0]+ranges[k][1]<=s[j].VirtualAddress+s[j].SizeOfRawData)sec=&s[j];if(!sec||fseek(file,sec->PointerToRawData+ranges[k][0]-sec->VirtualAddress,SEEK_SET))return 6;for(unsigned j=0;j<ranges[k][1];j++)if(fgetc(file)!=image[ranges[k][0]+j])return 7;} fclose(file);
 DWORD old; if(!VirtualProtect(image,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&old))return 8; IMAGE_DATA_DIRECTORY r=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]; unsigned pos=0,count=0; while(pos<r.Size){IMAGE_BASE_RELOCATION *b=(void*)(image+r.VirtualAddress+pos);if(b->SizeOfBlock<8||b->SizeOfBlock>r.Size-pos)return 9;WORD *e=(WORD*)(b+1);for(unsigned j=0;j<(b->SizeOfBlock-8)/2;j++)if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW){uint32_t *p=(void*)(image+b->VirtualAddress+(e[j]&4095));*p+=(uintptr_t)image-0x400000;count++;}pos+=b->SizeOfBlock;}
 patch(image+0x14f560,alloc_stock);patch(image+0x14f5b0,free_stock);printf("stock_code_verified=1 relocations=%u allocation_hooks=2\n",count);run(image,0);run(image,1);run(image,2);run(image,3);return 0;
}
