#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void *alloc_stock(int n) { return calloc(1,n); }
static void patch(unsigned char *p, void *target) {p[0]=0xe9;int32_t d=(unsigned char*)target-(p+5);memcpy(p+1,&d,4);}
static void run(unsigned char *image, int id) {
 float defaults[][2]={{200,0},{1500,0.8f},{4000,1},{5000,0.8f},{7000,0}};
 float custom[][2]={{0,1},{1,2},{1000,0.1f},{3000,-1},{7000,0.5f}};
 float keys[]={-1,0,0.5f,1,199,200,201,1000,1499,1500,1501,3000,4000,4999,5000,6999,7000,10000};
 struct {unsigned count;void *pairs;unsigned count2;void *pairs2;} curve={id==2?0:5,id==1?custom:defaults,5,defaults};
 for(unsigned k=0;k<sizeof(keys)/sizeof(keys[0]);k++) {
  unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
  float primary=((float(*)(float,void*))(image+0x3be70))(keys[k],&curve);
  float secondary=((float(*)(float,void*))(image+0x3bee0))(keys[k],&curve);
  __asm__ volatile("fldcw %0"::"m"(old));
  unsigned x,y;memcpy(&x,&primary,4);memcpy(&y,&secondary,4);printf("case=%d key=%u %08x %08x\n",id,k,x,y);
 }
}
int main(int argc,char **argv) {
 if(argc!=2)return 2; HMODULE m=LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES);if(!m)return 3;unsigned char *image=(void*)m;IMAGE_NT_HEADERS32 *pe=(void*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.ImageBase!=0x400000)return 4;
 FILE *f=fopen(argv[1],"rb");if(!f)return 5;IMAGE_SECTION_HEADER *s=IMAGE_FIRST_SECTION(pe);unsigned verified=0;
 for(unsigned i=0;i<pe->FileHeader.NumberOfSections;i++) if(s[i].Characteristics & IMAGE_SCN_CNT_CODE) {if(fseek(f,s[i].PointerToRawData,SEEK_SET))return 6;for(unsigned j=0;j<s[i].SizeOfRawData;j++)if(fgetc(f)!=image[s[i].VirtualAddress+j])return 7;verified+=s[i].SizeOfRawData;} fclose(f);
 DWORD old;if(!VirtualProtect(image,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&old))return 8; IMAGE_DATA_DIRECTORY r=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];unsigned pos=0,count=0;while(pos<r.Size){IMAGE_BASE_RELOCATION *b=(void*)(image+r.VirtualAddress+pos);if(b->SizeOfBlock<8||b->SizeOfBlock>r.Size-pos)return 9;WORD *e=(void*)(b+1);for(unsigned j=0;j<(b->SizeOfBlock-8)/2;j++)if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW){uint32_t *p=(void*)(image+b->VirtualAddress+(e[j]&4095));*p+=(uintptr_t)image-0x400000;count++;}pos+=b->SizeOfBlock;}
 printf("verified_code_bytes=%u relocations=%u\n",verified,count);for(int id=0;id<3;id++)run(image,id);return 0;
}
