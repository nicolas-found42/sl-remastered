#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef void (__attribute__((thiscall)) *Fn)(void *);
static void put(void *p, int o, float v) { memcpy((char *)p + o, &v, 4); }
static void ptr(void *p, int o, void *v) { memcpy((char *)p + o, &v, 4); }
static void word(float v) { uint32_t u; memcpy(&u, &v, 4); printf(" %08x", u); }
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  unsigned char *im = (void *)LoadLibraryExA(argv[1], 0, DONT_RESOLVE_DLL_REFERENCES);
  if (!im) return 3;
  IMAGE_NT_HEADERS32 *pe = (void *)(im + ((IMAGE_DOS_HEADER *)im)->e_lfanew);
  FILE *file=fopen(argv[1],"rb"); if (!file) return 5;
  IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(pe);
  unsigned ranges[][2]={{0x910a0,0xcb0},{0xa6520,0x170},{0xa6ff0,0x240},{0x436880-0x400000,0x100},{0x477560-0x400000,0x100}};
  for (unsigned k=0;k<sizeof(ranges)/sizeof(ranges[0]);k++) {
    IMAGE_SECTION_HEADER *sec=NULL;
    for (unsigned j=0;j<pe->FileHeader.NumberOfSections;j++)
      if (ranges[k][0]>=sections[j].VirtualAddress && ranges[k][0]+ranges[k][1]<=sections[j].VirtualAddress+sections[j].SizeOfRawData) sec=&sections[j];
    if (!sec || fseek(file,sec->PointerToRawData+ranges[k][0]-sec->VirtualAddress,SEEK_SET)) return 6;
    for (unsigned j=0;j<ranges[k][1];j++) if (fgetc(file)!=im[ranges[k][0]+j]) return 7;
  }
  fclose(file);
  DWORD prot; VirtualProtect(im, pe->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE, &prot);
  IMAGE_DATA_DIRECTORY dir = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  for (unsigned pos=0; pos<dir.Size;) {
    IMAGE_BASE_RELOCATION *r=(void *)(im+dir.VirtualAddress+pos); WORD *e=(void *)(r+1);
    for (unsigned j=0;j<(r->SizeOfBlock-8)/2;j++) if ((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW) {
      uint32_t *q=(void *)(im+r->VirtualAddress+(e[j]&4095)); *q+=(uintptr_t)im-0x400000;
    }
    pos+=r->SizeOfBlock;
  }
  for (int k=0;k<10;k++) {
    unsigned char a[0x220]={0}, b[0x220]={0}, root[0x220]={0}, mid[0x220]={0}, pose[0x50]={0}, pose2[0x50]={0}, pa[0xa0]={0}, pb[0xa0]={0}, u[0x100]={0}, world[0x200]={0};
    if (k==8) { ptr(im,0x217650,world); put(world,0xc,0.1f); put(pa,0x14,1); put(pb,0x14,1); }
    ptr(a,0x5c,pa); ptr(b,0x5c,pb); ptr(u,0x7c,a); ptr(u,0xb8,b);
    for(int i=0;i<3;i++) { put(a,0xa4+i*16+i*4,1); put(b,0xa4+i*16+i*4,1); }
    if (k>=5 && k<8) {
      ptr(root,0x5c,pa); ptr(mid,0x5c,pa); *(int *)(a+0x58)=4; ptr(a,0x1c,pose); ptr(a,0x20,k==6?mid:root);
      for(int i=0;i<3;i++) { put(root,0xa4+i*16+i*4,1); put(mid,0xa4+i*16+i*4,1); put(pose,0x20+i*16+i*4,1); put(pose2,0x20+i*16+i*4,1); }
      put(pose,4,1); put(root,0x74,0.1f); put(root,0xdc,0.2f);
      if (k==6) { *(int *)(mid+0x58)=4; ptr(mid,0x1c,pose2); ptr(mid,0x20,root); put(pose2,4,1); }
      if (k==7) { put(pose,0x20,0); put(pose,0x24,-1); put(pose,0x30,1); put(pose,0x34,0); }
    }
    put(u,0x34,0.9f); put(u,0x38,0.9f); put(u,0x44,1000); put(u,0x48,200); put(u,0x4c,140);
    if(k==9) { put(u,0x84,2); put(u,0x88,0.5f); put(u,0x8c,1); }
    if(k==8) { put(u,0x3c,0.2f); put(b,0x74,0.1f); }
    put(u,0x9c,k==2?1:k==8?0.01f:0.25f); put(u,0xd8,0.6f); put(u,0xb0,k==3?-1:1);
    put(b,0x74,k==1||k==8?0.1f:k==4?-0.1f:0);
    ((Fn)(im+0x910a0))(0); ((Fn)(im+0x910c0))(0); ((Fn)(im+0x910e0))(0);
    unsigned short old,cw=0x007f; __asm__ volatile("fnstcw %0":"=m"(old)); __asm__ volatile("fldcw %0"::"m"(cw));
    int ret=((int (__attribute__((thiscall)) *)(void *))(im+0x91100))(u);
    __asm__ volatile("fldcw %0"::"m"(old));
    printf("case=%d %d",k,ret);
    int offsets[]={0x9c,0xd8,0xdc,0xe0,0xe4,0xc0,0xc4,0xc8,0xcc,0xd0,0xd4,0xac,0xb0,0xb4};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){float v;memcpy(&v,u+offsets[i],4);word(v);}
    for(int i=0;i<3;i++){float v;memcpy(&v,(k>=5 && k<8?root:a)+0x8c+i*4,4);word(v);}
    for(int i=0;i<3;i++){float v;memcpy(&v,(k>=5 && k<8?root:a)+0xec+i*4,4);word(v);}
    puts("");
  }
  return 0;
}
