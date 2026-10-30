#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void *alloc_stock(int n) { return calloc(1,n); }
static void patch(unsigned char *p, void *target) {p[0]=0xe9;int32_t d=(unsigned char*)target-(p+5);memcpy(p+1,&d,4);}
static void run(unsigned char *image, unsigned id, unsigned char *data, unsigned size) {
 unsigned char object[0x54]={0};
 ((void*(__attribute__((thiscall)) *)(void*))(image+0x19abb3))(object);
 unsigned result=((unsigned(__attribute__((thiscall)) *)(void*,void*,unsigned))(image+0x19c7e8))(object,data,size);
 printf("case=%u accepted=%d",id,result==0);
 if(result==0) {
  unsigned char *level=object;
  for(unsigned n=0;level && n<32;n++,level=*(void**)(level+0x4c))
   printf(" %u,%u,%u,%u,%u,%u",*(unsigned*)level,*(unsigned*)(level+12),*(unsigned*)(level+16),
    (unsigned)(*(unsigned char**)(level+4)-data),*(unsigned*)(level+0x30),*(unsigned*)(level+0x34));
 }
 puts("");
}
int main(int argc,char **argv) {
 if(argc!=3)return 2; HMODULE m=LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES);if(!m)return 3;unsigned char *image=(void*)m;IMAGE_NT_HEADERS32 *pe=(void*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.ImageBase!=0x400000)return 4;
 FILE *f=fopen(argv[1],"rb");if(!f)return 5;IMAGE_SECTION_HEADER *s=IMAGE_FIRST_SECTION(pe);unsigned verified=0;
 for(unsigned i=0;i<pe->FileHeader.NumberOfSections;i++) if(s[i].Characteristics & IMAGE_SCN_CNT_CODE) {if(fseek(f,s[i].PointerToRawData,SEEK_SET))return 6;for(unsigned j=0;j<s[i].SizeOfRawData;j++)if(fgetc(f)!=image[s[i].VirtualAddress+j])return 7;verified+=s[i].SizeOfRawData;} fclose(f);
 DWORD old;if(!VirtualProtect(image,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&old))return 8; IMAGE_DATA_DIRECTORY r=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];unsigned pos=0,count=0;while(pos<r.Size){IMAGE_BASE_RELOCATION *b=(void*)(image+r.VirtualAddress+pos);if(b->SizeOfBlock<8||b->SizeOfBlock>r.Size-pos)return 9;WORD *e=(void*)(b+1);for(unsigned j=0;j<(b->SizeOfBlock-8)/2;j++)if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW){uint32_t *p=(void*)(image+b->VirtualAddress+(e[j]&4095));*p+=(uintptr_t)image-0x400000;count++;}pos+=b->SizeOfBlock;}
 patch(image+0x1840,alloc_stock);
 FILE *input=fopen(argv[2],"rb");if(!input)return 10;unsigned fixtureCount;if(fread(&fixtureCount,4,1,input)!=1)return 11;
 for(unsigned i=0;i<fixtureCount;i++){unsigned size;if(fread(&size,4,1,input)!=1)return 12;unsigned char *data=malloc(size);if(!data||fread(data,1,size,input)!=size)return 13;run(image,i,data,size);free(data);}
 fclose(input);return 0;
}
