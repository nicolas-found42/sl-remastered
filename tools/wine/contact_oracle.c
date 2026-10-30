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
  const unsigned char a[] = {0x83,0xec,0x40,0x55,0x56,0x8b,0xf1};
  const unsigned char b[] = {0x83,0xec,0x58,0x53,0x55,0x56};
  const unsigned char c[] = {0x83,0xec,0x34,0x53,0x56,0x8b,0xf1};
  const unsigned char d[] = {0x8a,0x41,0x58,0x83,0xec,0x18,0xa8,0x04};
  if (memcmp(im+0x925d0,a,sizeof a) || memcmp(im+0x927f0,b,sizeof b) ||
      memcmp(im+0x92ad0,c,sizeof c) || memcmp(im+0xa73c0,d,sizeof d)) return 4;
  IMAGE_NT_HEADERS32 *pe = (void *)(im + ((IMAGE_DOS_HEADER *)im)->e_lfanew);
  DWORD prot; VirtualProtect(im, pe->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE, &prot);
  IMAGE_DATA_DIRECTORY dir = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  for (unsigned pos=0; pos<dir.Size;) {
    IMAGE_BASE_RELOCATION *r=(void *)(im+dir.VirtualAddress+pos); WORD *e=(void *)(r+1);
    for (unsigned j=0;j<(r->SizeOfBlock-8)/2;j++) if ((e[j]>>12)==IMAGE_REL_BASED_HIGHLOW) {
      uint32_t *q=(void *)(im+r->VirtualAddress+(e[j]&4095)); *q+=(uintptr_t)im-0x400000;
    }
    pos+=r->SizeOfBlock;
  }
  for (int k=0;k<5;k++) {
    unsigned char bodies[4][0x220]={{0}}, proto[2][0xa0]={{0}}, poses[2][0x50]={{0}}, row[0xb0]={0};
    float target=0, impulse=0; put(proto[0],0x14,1); *(int *)(proto[1]+0x9c)=4;
    for (int i=0;i<4;i++) {
      ptr(bodies[i],0x5c,proto[i==3]);
      for (int state=0;state<2;state++) for(int j=0;j<3;j++) {
        put(bodies[i],0x64+state*0xd4+0x40+j*16+j*4,1);
        if (i!=3) put(bodies[i],0x64+state*0xd4+0xa4+j*16+j*4,1);
      }
    }
    for (int state=0;state<2;state++) { put(bodies[0],0x64+state*0xd4+0xc,-2); put(bodies[0],0x64+state*0xd4+0x78,1); }
    for (int i=0;i<2;i++) {
      put(poses[i],4,0); put(poses[i],8,1); put(poses[i],0xc,0);
      for (int j=0;j<3;j++) put(poses[i],0x20+j*16+j*4,1);
      *(int *)(bodies[i+1]+0x58)=4; ptr(bodies[i+1],0x1c,poses[i]); ptr(bodies[i+1],0x20,bodies[i]);
    }
    if (k==3) { put(poses[0],0x20,0); put(poses[0],0x24,-1); put(poses[0],0x30,1); put(poses[0],0x34,0); }
    int selected=k==0?0:k==2?2:1;
    *(int *)(row+0x14)=6; ptr(row,0x30,bodies[selected]); ptr(row,0x38,bodies[3]); ptr(row,0x24,&target); ptr(row,0x2c,&impulse);
    put(row,0x60,1); put(row,0x74,k==4?0.6f:1); if (k==4) put(row,0x78,0.8f);
    unsigned short old,cw=0x007f; __asm__ volatile("fnstcw %0":"=m"(old)); __asm__ volatile("fldcw %0"::"m"(cw));
    ((Fn)(im+0x925d0))(row); ((Fn)(im+0x927f0))(row); ((Fn)(im+0x92ad0))(row);
    __asm__ volatile("fldcw %0"::"m"(old));
    printf("case=%d",k); word(target); word(impulse);
    for (int i=0;i<3;i++) { float v; memcpy(&v,bodies[selected]+0x64+0xc+i*4,4); word(v); }
    for (int i=0;i<3;i++) { float v; memcpy(&v,bodies[selected]+0x64+0x70+i*4,4); word(v); }
    puts("");
  }
  return 0;
}
