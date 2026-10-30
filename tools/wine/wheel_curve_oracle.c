#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../tests/wheel_curve_cases.h"
static void run(unsigned char *image) {
 typedef float (__attribute__((thiscall)) *Curve)(float *, float, float);
 Curve positive=(Curve)(image+0x3ba80), signed_curve=(Curve)(image+0x3bb30);
 for(unsigned c=0;c<3;c++) for(unsigned s=0;s<6;s++) for(unsigned l=0;l<4;l++) {
  float coefficients[11]; memcpy(coefficients,curve_cases[c],sizeof(coefficients));
  unsigned short old,cw=0x007f; __asm__ volatile("fnstcw %0":"=m"(old)); __asm__ volatile("fldcw %0"::"m"(cw));
  float a=positive(coefficients,curve_slips[s],curve_loads[l]);
  float b=signed_curve(coefficients,curve_slips[s],curve_loads[l]);
  __asm__ volatile("fldcw %0"::"m"(old));
  unsigned x,y; memcpy(&x,&a,4); memcpy(&y,&b,4);
  printf("case=%u slip=%u load=%u %08x %08x\n",c,s,l,x,y);
 }
}
int main(int argc,char **argv) {
 if(argc!=2)return 2; HMODULE m=LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES);if(!m)return 3;unsigned char *image=(void*)m;IMAGE_NT_HEADERS32 *pe=(void*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.ImageBase!=0x400000)return 4;
 FILE *f=fopen(argv[1],"rb");if(!f)return 5;IMAGE_SECTION_HEADER *s=IMAGE_FIRST_SECTION(pe);unsigned verified=0;
 for(unsigned i=0;i<pe->FileHeader.NumberOfSections;i++) if(s[i].Characteristics & IMAGE_SCN_CNT_CODE) {if(fseek(f,s[i].PointerToRawData,SEEK_SET))return 6;for(unsigned j=0;j<s[i].SizeOfRawData;j++)if(fgetc(f)!=image[s[i].VirtualAddress+j])return 7;verified+=s[i].SizeOfRawData;} fclose(f);
 DWORD old;if(!VirtualProtect(image,pe->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&old))return 8; IMAGE_DATA_DIRECTORY r=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];unsigned pos=0,count=0;while(pos<r.Size){IMAGE_BASE_RELOCATION *b=(void*)(image+r.VirtualAddress+pos);if(b->SizeOfBlock<8||b->SizeOfBlock>r.Size-pos)return 9;WORD *e=(void*)(b+1);for(unsigned j=0;j<(b->SizeOfBlock-8)/2;j++)if((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW){uint32_t *p=(void*)(image+b->VirtualAddress+(e[j]&4095));*p+=(uintptr_t)image-0x400000;count++;}pos+=b->SizeOfBlock;}
 printf("verified_code_bytes=%u relocations=%u\n",verified,count);run(image);return 0;
}
