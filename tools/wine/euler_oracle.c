#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef void (__attribute__((thiscall)) *convert)(float *, const float *);
#include <euler_cases.h>
int main(int argc,char **argv) {
 if(argc!=2) return 2;
 HMODULE module=LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES); if(!module) return 3;
 unsigned char *image=(unsigned char *)module;
 IMAGE_NT_HEADERS32 *pe=(IMAGE_NT_HEADERS32 *)(image+((IMAGE_DOS_HEADER *)image)->e_lfanew);
 if(pe->Signature!=IMAGE_NT_SIGNATURE || pe->OptionalHeader.ImageBase!=0x400000) return 4;
 FILE *f=fopen(argv[1],"rb"); if(!f) return 5;
 IMAGE_SECTION_HEADER *s=IMAGE_FIRST_SECTION(pe), *section=NULL;
 for(unsigned i=0;i<pe->FileHeader.NumberOfSections;i++) if(0x14ecd0>=s[i].VirtualAddress && 0x14ed91<=s[i].VirtualAddress+s[i].SizeOfRawData) section=&s[i];
 if(!section || fseek(f,section->PointerToRawData+0x14ecd0-section->VirtualAddress,SEEK_SET)) return 6;
 for(unsigned i=0;i<0xc1;i++) { int b=fgetc(f); if(b<0 || image[0x14ecd0+i]!=(unsigned char)b) return 7; } fclose(f);
 convert run=(convert)(image+0x14ecd0); unsigned short old,cw=0x007f; __asm__ volatile("fnstcw %0":"=m"(old));
 for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) { float m[12]={0}; __asm__ volatile("fldcw %0"::"m"(cw)); run(m,cases[i]); __asm__ volatile("fldcw %0"::"m"(old)); printf("case=%u",i); for(int j=0;j<12;j++) if(j%4!=3) { uint32_t u; memcpy(&u,&m[j],4); printf(" %08lx",(unsigned long)u); } puts(""); }
 return 0;
}
