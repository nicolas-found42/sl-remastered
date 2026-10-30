#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void (__attribute__((thiscall)) *convert)(void *, const void *);
static const float matrices[][12] = {
 {1,0,0,0,0,1,0,0,0,0,1,0},
 {1,0,0,0,0,-1,0,0,0,0,-1,0},
 {-1,0,0,0,0,1,0,0,0,0,-1,0},
 {-1,0,0,0,0,-1,0,0,0,0,1,0},
 {0,-1,0,0,1,0,0,0,0,0,1,0},
 {0.1f,0.2f,0.3f,0,0.4f,0.5f,0.6f,0,0.7f,0.8f,0.9f,0},
 {-0.9f,0.2f,0.3f,0,0.4f,-0.8f,0.6f,0,0.7f,0.8f,-0.7f,0},
 {0.8253356f,-0.5646425f,0,0,0.5646425f,0.8253356f,0,0,0,0,1,0},
 {0.36f,-0.48f,0.8f,0,0.8f,0.60f,0,0,-0.48f,0.64f,0.60f,0},
 {-0.5f,0.8660254f,0,0,-0.8660254f,-0.5f,0,0,0,0,1,0},
 {0.0f,1,0,0,0,0,-1,0,-1,0,0,0}
};
static void words(const float *v, int n) { for (int i=0;i<n;i++) { uint32_t u; memcpy(&u,&v[i],4); printf(" %08lx",(unsigned long)u); } }
int main(int argc, char **argv) {
 if (argc != 2) return 2;
 HMODULE module=LoadLibraryExA(argv[1],NULL,DONT_RESOLVE_DLL_REFERENCES);
 if (!module) { fprintf(stderr,"load error %lu\n",GetLastError()); return 3; }
 unsigned char *image=(unsigned char*)module;
 IMAGE_NT_HEADERS32 *pe=(IMAGE_NT_HEADERS32*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);
 if (pe->Signature!=IMAGE_NT_SIGNATURE || pe->OptionalHeader.ImageBase!=0x400000) return 4;
 const unsigned offsets[]={0x14ee15,0x14ee3e,0x14ee6a,0x14ff4c,0x14ff59,0x14ff64,0x14ff6d,0x14ffe0,0x14ffea,0x14fff4,0x150001};
 const unsigned targets[]={0x1f08f0,0x1f08f0,0x1f08f0,0x1e73cc,0x1f08f0,0x1f09d0,0x1f09d0,0x1f08f0,0x1f09d0,0x1e73cc,0x1f09d0};
 FILE *stock=fopen(argv[1],"rb"); if (!stock) return 9;
 IMAGE_SECTION_HEADER *section=IMAGE_FIRST_SECTION(pe);
 const unsigned starts[]={0x14eda0,0x14ff20}, lengths[]={0xd7,0x145};
 for (unsigned k=0;k<2;k++) {
  IMAGE_SECTION_HEADER *source=NULL;
  for (unsigned j=0;j<pe->FileHeader.NumberOfSections;j++) if (starts[k]>=section[j].VirtualAddress && starts[k]+lengths[k]<=section[j].VirtualAddress+section[j].SizeOfRawData) source=&section[j];
  if (!source || fseek(stock,source->PointerToRawData+starts[k]-source->VirtualAddress,SEEK_SET)) return 10;
  for (unsigned j=0;j<lengths[k];j++) { int byte=fgetc(stock); if (byte<0 || image[starts[k]+j]!=(unsigned char)byte) { fprintf(stderr,"stock code mismatch RVA=%08x\n",starts[k]+j); return 11; } }
 }
 fclose(stock);
 DWORD old_protect;
 if (!VirtualProtect(image+0x14e000,0x3000,PAGE_EXECUTE_READWRITE,&old_protect)) return 5;
 IMAGE_DATA_DIRECTORY reloc=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 for (unsigned i=0;i<sizeof offsets/sizeof offsets[0];i++) {
  uint32_t address; memcpy(&address,image+offsets[i],4);
  if (address!=0x400000+targets[i]) { fprintf(stderr,"original operand mismatch offset=%08x got=%08x\n",offsets[i],address); return 5; }
  int found=0; unsigned pos=0;
  while (pos<reloc.Size) { IMAGE_BASE_RELOCATION *block=(IMAGE_BASE_RELOCATION*)(image+reloc.VirtualAddress+pos); if (block->SizeOfBlock<sizeof *block || block->SizeOfBlock>reloc.Size-pos) return 7;
   unsigned count=(block->SizeOfBlock-sizeof *block)/2; WORD *entry=(WORD*)(block+1);
   for (unsigned j=0;j<count;j++) if ((entry[j]>>12)==IMAGE_REL_BASED_HIGHLOW && block->VirtualAddress+(entry[j]&0xfff)==offsets[i]) found=1;
   pos+=block->SizeOfBlock;
  }
  if (!found) { fprintf(stderr,"missing relocation offset=%08x\n",offsets[i]); return 8; }
  address=(uintptr_t)(image+targets[i]); memcpy(image+offsets[i],&address,4);
 }
 unsigned constants[]={0,0,0}; memcpy(&constants[0],image+0x1e73cc,4); memcpy(&constants[1],image+0x1f08f0,4); memcpy(&constants[2],image+0x1f09d0,4);
 if (constants[0]!=0 || constants[1]!=0x3f800000 || constants[2]!=0x3f000000) { fprintf(stderr,"constant mismatch %08x %08x %08x\n",constants[0],constants[1],constants[2]); return 6; }
 convert mtoq=(convert)(image+0x14ff20), qtom=(convert)(image+0x14eda0);
 printf("mapped_base=%08lx original_code_verified=1 relocation_operands_applied=%u constants=%08x,%08x,%08x\n",(unsigned long)(uintptr_t)image,(unsigned)(sizeof offsets/sizeof offsets[0]),constants[0],constants[1],constants[2]);
 unsigned short initial; __asm__ volatile("fnstcw %0" : "=m"(initial));
 printf("initial_cw=%04x\n",initial);
 for (int mode=0;mode<3;mode++) {
  unsigned short cw=(initial & ~0x300) | (mode==0 ? 0 : mode==1 ? 0x200 : 0x300);
  for (unsigned i=0;i<sizeof matrices/sizeof matrices[0];i++) {
   float q[4]={0}, m[12]={0};
   __asm__ volatile("fldcw %0" : : "m"(cw));
   mtoq(q,matrices[i]); qtom(m,q);
   __asm__ volatile("fldcw %0" : : "m"(initial));
   printf("case=%u cw=%04x input",i,cw); words(matrices[i],12); printf(" quat"); words(q,4); printf(" matrix"); words(m,12); puts("");
  }
 }
 return 0;
}
