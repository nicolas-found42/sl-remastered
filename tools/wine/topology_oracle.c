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
static void patch(unsigned char *p,void *target){p[0]=0xe9;int32_t d=(unsigned char*)target-(p+5);memcpy(p+1,&d,4);}
static void *alloc_stock(int n){return n>0?calloc(1,n):NULL;}
static void *realloc_stock(void *p,int n){return realloc(p,n);}
static void free_stock(void *p){free(p);}
static void words(const float *v,int n){for(int i=0;i<n;i++){uint32_t x;memcpy(&x,v+i,4);printf(" %08lx",(unsigned long)x);}}
static void run(unsigned char *image,int id){
 Core c={0}; Vec verts[]={{0,0,0},{1,0,0},{0,1,0},{0,0,1}}, a={1,2,3},b={4,6,8}; int faces[][8]={{3,0,2,1},{3,0,1,3},{3,0,3,2},{3,1,2,3}};
 unsigned short old,cw=0x007f; __asm__ volatile("fnstcw %0":"=m"(old)); __asm__ volatile("fldcw %0"::"m"(cw));
 if(id==0)((void(__attribute__((thiscall)) *)(Core*,float,float,float))(image+0xac1c0))(&c,1,2,3);
 if(id==1)((int(__attribute__((thiscall)) *)(Core*,int,Vec*,int,void*))(image+0xac150))(&c,4,verts,4,faces);
 if(id==2)((void(__attribute__((thiscall)) *)(Core*,int))(image+0xac370))(&c,0);
 if(id==3)((void(__attribute__((thiscall)) *)(Core*,Vec*,Vec*))(image+0xac440))(&c,&a,&b);
 __asm__ volatile("fldcw %0"::"m"(old));
 printf("case=%d vertices=%d features=%d\n",id,c.nv,c.nf);
 for(int i=0;i<c.nv;i++){printf("v %d",i);words((float*)&c.v[i],3);puts("");}
 for(int i=0;i<c.nf;i++){Feature *f=&c.f[i];printf("f %d %d %d",i,f->kind,(int)(f->anchor-c.v));words(f->n,3);printf(" %d\n",f->count);for(int j=0;j<f->count;j++){Adj *a=&f->adj[j];printf("a %d %d %d %d",i,j,(int)(a->neighbor-c.f),(int)(a->anchor-c.v));words(a->n,3);puts("");}}
}
int main(int argc,char **argv) {
 if(argc!=2 && argc!=3)return 2; HMODULE m=LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES); if(!m)return 3; unsigned char *image=(unsigned char*)m; IMAGE_NT_HEADERS32 *pe=(void*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew); if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.ImageBase!=0x400000)return 4;
 FILE *file=fopen(argv[1],"rb");if(!file)return 5; IMAGE_SECTION_HEADER *s=IMAGE_FIRST_SECTION(pe); unsigned ranges[][2]={{0xaa560,0x200},{0xab8d0,0x1d00},{0x14e3b0,0x200}};
 for(unsigned k=0;k<3;k++){IMAGE_SECTION_HEADER *sec=NULL;for(unsigned j=0;j<pe->FileHeader.NumberOfSections;j++)if(ranges[k][0]>=s[j].VirtualAddress&&ranges[k][0]+ranges[k][1]<=s[j].VirtualAddress+s[j].SizeOfRawData)sec=&s[j];if(!sec||fseek(file,sec->PointerToRawData+ranges[k][0]-sec->VirtualAddress,SEEK_SET))return 6;for(unsigned j=0;j<ranges[k][1];j++)if(fgetc(file)!=image[ranges[k][0]+j])return 7;} fclose(file);
 DWORD old; if(!VirtualProtect(image,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&old))return 8; IMAGE_DATA_DIRECTORY r=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]; unsigned pos=0,count=0; while(pos<r.Size){IMAGE_BASE_RELOCATION *b=(void*)(image+r.VirtualAddress+pos);if(b->SizeOfBlock<8||b->SizeOfBlock>r.Size-pos)return 9;WORD *e=(WORD*)(b+1);for(unsigned j=0;j<(b->SizeOfBlock-8)/2;j++)if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW){uint32_t *p=(void*)(image+b->VirtualAddress+(e[j]&4095));*p+=(uintptr_t)image-0x400000;count++;}pos+=b->SizeOfBlock;}
 patch(image+0x14f560,alloc_stock);patch(image+0x14f5b0,free_stock);patch(image+0x14f580,realloc_stock);printf("stock_code_verified=1 relocations=%u allocation_hooks=3\n",count);((void (*)(void))(image+0xaa560))();for(int i=0;i<4;i++)run(image,i);return 0;
}
