#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void *alloc_stock(int n) { return calloc(1,n); }
static void patch(unsigned char *p, void *target) {p[0]=0xe9;int32_t d=(unsigned char*)target-(p+5);memcpy(p+1,&d,4);}
static void run(unsigned char *image, int id) {
 const float quats[][4]={{0,0,0,1},{0,0,0.70710677f,0.70710677f},{0.1f,0.2f,0.3f,0.4f},{0.11f,-0.25f,0.31f,0.87f}};
 unsigned char parent[0x220]={0},child[0x220]={0};
 *(void**)(child+0x20)=parent;
 memcpy(parent+0xf8,quats[id],16);memcpy(child+0xf8,quats[3-id],16);
 for(int i=0;i<3;i++){*(float*)(parent+0x64+i*4)=2+i;*(float*)(child+0x64+i*4)=4+i;}
 unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
 ((void(__attribute__((thiscall)) *)(void*,void*))(image+0x14eda0))(parent+0xa4,parent+0xf8);
 ((void(__attribute__((thiscall)) *)(void*,void*))(image+0x14eda0))(child+0xa4,child+0xf8);
 ((void(__attribute__((stdcall)) *)(void*))(image+0x9a840))(child);
 __asm__ volatile("fldcw %0"::"m"(old));
 float *r=*(void**)(child+0x1c);printf("case=%d",id);
 for(int i=1;i<20;i++){if(i==11||i==15||i==19)continue;uint32_t x;memcpy(&x,r+i,4);printf(" %08x",x);}puts("");free(r);
}
int main(int argc,char **argv) {
 if(argc!=2)return 2; HMODULE m=LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES);if(!m)return 3;unsigned char *image=(void*)m;IMAGE_NT_HEADERS32 *pe=(void*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.ImageBase!=0x400000)return 4;
 FILE *f=fopen(argv[1],"rb");if(!f)return 5;IMAGE_SECTION_HEADER *s=IMAGE_FIRST_SECTION(pe);unsigned verified=0;
 for(unsigned i=0;i<pe->FileHeader.NumberOfSections;i++) if(s[i].Characteristics & IMAGE_SCN_CNT_CODE) {if(fseek(f,s[i].PointerToRawData,SEEK_SET))return 6;for(unsigned j=0;j<s[i].SizeOfRawData;j++)if(fgetc(f)!=image[s[i].VirtualAddress+j])return 7;verified+=s[i].SizeOfRawData;} fclose(f);
 DWORD old;if(!VirtualProtect(image,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&old))return 8; IMAGE_DATA_DIRECTORY r=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];unsigned pos=0,count=0;while(pos<r.Size){IMAGE_BASE_RELOCATION *b=(void*)(image+r.VirtualAddress+pos);if(b->SizeOfBlock<8||b->SizeOfBlock>r.Size-pos)return 9;WORD *e=(void*)(b+1);for(unsigned j=0;j<(b->SizeOfBlock-8)/2;j++)if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW){uint32_t *p=(void*)(image+b->VirtualAddress+(e[j]&4095));*p+=(uintptr_t)image-0x400000;count++;}pos+=b->SizeOfBlock;}
 patch(image+0x14f560,alloc_stock);printf("verified_code_bytes=%u relocations=%u\n",verified,count);for(int id=0;id<4;id++)run(image,id);return 0;
}
