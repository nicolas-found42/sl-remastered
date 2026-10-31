#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef WALK_TRACE_SMOKE
int walk_trace_install(HMODULE module, const char *image_path);
int walk_trace_flush(const char *path);
#endif

typedef struct {float x,y,z;} Vec;
typedef struct Feature Feature;
typedef struct {Feature *neighbor; Vec *anchor; float n[3];} Adj;
struct Feature {int kind; Vec *anchor; float n[3]; int count,capacity; Adj *adj;};
typedef struct {int nv; Vec *v; int nf,capacity; Feature *f; int flags;} Core;
typedef float (__cdecl *Walk)(void*,void*,void*,void*,Feature**,Feature**,float*,float*,float*);
static unsigned char *stock_image;
static unsigned query_index;
static float __cdecl cached(void *p,void *q,void *a,void *b,Feature **fa,Feature **fb,float *x,float *y,float *n) {
 Core *ca=(Core*)((char*)a+32), *cb=(Core*)((char*)b+32);
 ((Walk)(stock_image+0xae9c0))(p,q,a,b,fa,fb,x,y,n);
 if (!*fa) *fa=ca->f;
 if (!*fb) *fb=cb->f;
 Feature *initial_a=*fa, *initial_b=*fb;
 float result=((Walk)(stock_image+0xaf970))(p,q,a,b,fa,fb,x,y,n);
 initial_a=*fa; initial_b=*fb;
 float old; memcpy(&old,p,4); float moved=old+0.125f; memcpy(p,&moved,4);
 Feature *wa=*fa,*wb=*fb; float cx[3],cy[3],cn[3],wx[3],wy[3],wn[3];
 float cd=((Walk)(stock_image+0xaf970))(p,q,a,b,fa,fb,cx,cy,cn);
 float wd=((Walk)(stock_image+0xae9c0))(p,q,a,b,&wa,&wb,wx,wy,wn);
 unsigned cbits,wbits; memcpy(&cbits,&cd,4);memcpy(&wbits,&wd,4);
 if (query_index==16) fprintf(stderr,"detail initial %td/%d %td/%d cached %td/%d %td/%d walk %td/%d %td/%d points_cached %.9g %.9g %.9g %.9g %.9g %.9g normal %.9g %.9g %.9g points_walk %.9g %.9g %.9g %.9g %.9g %.9g normal %.9g %.9g %.9g\n",initial_a-ca->f,initial_a->kind,initial_b-cb->f,initial_b->kind,*fa-ca->f,(*fa)->kind,*fb-cb->f,(*fb)->kind,wa-ca->f,wa->kind,wb-cb->f,wb->kind,cx[0],cx[1],cx[2],cy[0],cy[1],cy[2],cn[0],cn[1],cn[2],wx[0],wx[1],wx[2],wy[0],wy[1],wy[2],wn[0],wn[1],wn[2]);
 if (query_index<96) {
  Feature *f=ca->nv==1?initial_b:initial_a; Core *c=ca->nv==1?cb:ca;
  fprintf(stderr,"facecase %u %td %d %td",query_index,f-c->f,f->kind,(ca->nv==1?*fb:*fa)-c->f);
  float values[]={cd,cx[0],cx[1],cx[2],cy[0],cy[1],cy[2],cn[0],cn[1],cn[2]};
  for(int i=0;i<10;i++){unsigned bits;memcpy(&bits,&values[i],4);fprintf(stderr," %08x",bits);} fprintf(stderr,"\n");
 }
 fprintf(stderr,"moving %u %08x %08x %d %d\n",query_index++,cbits,wbits,*fa==wa,*fb==wb);
 memcpy(p,&old,4); return result;
}
static void patch(unsigned char *p,void *target){p[0]=0xe9;int32_t d=(unsigned char*)target-(p+5);memcpy(p+1,&d,4);}
static void *alloc_stock(int n){return n>0?calloc(1,n):NULL;}
static void *realloc_stock(void *p,int n){return realloc(p,n);}
static void free_stock(void *p){free(p);}

typedef float (__cdecl *Closest)(void *,void *,void *,void *,float *,float *,float *);
static void ptr(void *p,int o,void *v) { memcpy((char*)p+o,&v,4); }
static void put(void *p,int o,float v) { memcpy((char*)p+o,&v,4); }
int main(int argc,char **argv) {
 if(argc!=2 && argc!=3) return 2;
 unsigned char *im=(void*)LoadLibraryExA(argv[1],0,DONT_RESOLVE_DLL_REFERENCES);
 if(!im) return 3; stock_image=im;
  IMAGE_NT_HEADERS32 *pe = (void *)(im + ((IMAGE_DOS_HEADER *)im)->e_lfanew);
  if (pe->Signature != IMAGE_NT_SIGNATURE || pe->OptionalHeader.ImageBase != 0x400000) return 4;
  FILE *file = fopen(argv[1], "rb");
  if (!file) return 5;
  IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(pe);
  const unsigned ranges[][2] = {{0xace20,0x260},{0x36880,0x180},{0xb13b0,0x200},{0x5db90,0xc0},{0xa5e80,0x100},{0xaa560,0x200},{0xab8d0,0x1d00},{0xacd00,0x2700},{0xaf400,0x1300},{0x14e3b0,0x200}};
  for (unsigned k = 0; k < sizeof ranges / sizeof ranges[0]; ++k) {
    IMAGE_SECTION_HEADER *section = NULL;
    for (unsigned j = 0; j < pe->FileHeader.NumberOfSections; ++j)
      if (ranges[k][0] >= sections[j].VirtualAddress &&
          ranges[k][0] + ranges[k][1] <= sections[j].VirtualAddress + sections[j].SizeOfRawData)
        section = &sections[j];
    if (!section || fseek(file, section->PointerToRawData + ranges[k][0] - section->VirtualAddress, SEEK_SET)) return 6;
    for (unsigned j = 0; j < ranges[k][1]; ++j)
      if (fgetc(file) != im[ranges[k][0] + j]) return 7;
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
#ifdef WALK_TRACE_SMOKE
  if (getenv("SLRR_TEST_HOOK")) {
    const char *bad = getenv("SLRR_TEST_BAD_IMAGE");
    if (!bad || walk_trace_install((HMODULE)im, bad)) return 97;
    im[0xae9c0] ^= 1;
    if (walk_trace_install((HMODULE)im, argv[1])) return 96;
    im[0xae9c0] ^= 1;
    if (!walk_trace_install((HMODULE)im, argv[1])) return 98;
  }
#endif
  patch(im+0x14f560,alloc_stock); patch(im+0x14f5b0,free_stock); patch(im+0x14f580,realloc_stock);
  ((void (*)(void))(im+0xaa560))();
  for(int k=0;k<12;k++) {
    unsigned char pose[2][0xd4]={{0}}, point[32]={0}, edge[32]={0}, vertex[32]={0}, links[40]={0};
    float origin[3]={0}, start[3]={-2,0,0}, end[3]={2,0,0}, pa[3],pb[3],normal[3];
    for(int j=0;j<2;j++) for(int i=0;i<3;i++) put(pose[j],0x40+i*16+i*4,1);
    put(pose[0],0,k%3==1?5:1); put(pose[0],4,k%3==2?0:3);
    if(k%6>=3) { put(pose[1],0x40,0); put(pose[1],0x44,-1); put(pose[1],0x50,1); put(pose[1],0x54,0); }
    ptr(point,4,origin); ptr(edge,4,start); put(edge,8,1); ptr(vertex,4,end); ptr(edge,0x1c,links); ptr(links,0x14,vertex);
    unsigned short previous, cw = 0x007f;
    __asm__ volatile("fnstcw %0":"=m"(previous));
    __asm__ volatile("fldcw %0"::"m"(cw));
    float d=((Closest)(im+0xace20))(pose[0],pose[1],point,edge,pa,pb,normal);
    __asm__ volatile("fldcw %0"::"m"(previous));
    if(k>=6) { for(int i=0;i<3;i++) { float t=pa[i]; pa[i]=pb[i]; pb[i]=t; normal[i]=-normal[i]; } }
    printf("%d",k); float *values[]={&d,pa,pa+1,pa+2,pb,pb+1,pb+2,normal,normal+1,normal+2}; for(int i=0;i<10;i++) { uint32_t bits; memcpy(&bits,values[i],4); printf(" %08x",bits); } puts("");
  }
 for(int k=0;k<96;k++) {
   struct { char pad[32]; Core core; } shapes[2]={{0}};
   unsigned char poses[2][0xd4]={{0}}; Feature *fp=NULL,*fm=NULL;
   Vec verts[]={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
   int faces[][8]={{3,0,2,1},{3,0,1,3},{3,0,3,2},{3,1,2,3}};
   float cases[][3]={{3,0,0},{3,3,0},{3,3,3},{0,0,0},{0.2f,0.2f,0.2f},{-1,-1,-1}};
   float a[3]={0},b[3]={0},n[3]={0};
   for(int j=0;j<2;j++) for(int i=0;i<3;i++) put(poses[j],0x40+i*16+i*4,1);
   for(int i=0;i<3;i++) put(poses[0],i*4,cases[k%6][i]);
   if((k/6)%2) {put(poses[1],0x40,0.6f);put(poses[1],0x44,-0.8f);put(poses[1],0x50,0.8f);put(poses[1],0x54,0.6f);}
   unsigned short old,cw=0x007f; __asm__ volatile("fnstcw %0":"=m"(old)); __asm__ volatile("fldcw %0"::"m"(cw));
   ((void(__attribute__((thiscall)) *)(Core*,int))(im+0xac370))(&shapes[0].core,0);
   if(k%24<12) ((void(__attribute__((thiscall)) *)(Core*,float,float,float))(im+0xac1c0))(&shapes[1].core,1,2,3);
   else ((int(__attribute__((thiscall)) *)(Core*,int,Vec*,int,void*))(im+0xac150))(&shapes[1].core,4,verts,4,faces);
   if(k>=48) fm=&shapes[1].core.f[shapes[1].core.nf-1];
   *(int *)(im+0x24464c)=0;
   float d;
   if(k%48<24) d=cached(poses[0],poses[1],&shapes[0],&shapes[1],&fp,&fm,a,b,n);
   else d=cached(poses[1],poses[0],&shapes[1],&shapes[0],&fm,&fp,a,b,n);
   __asm__ volatile("fninit");
   __asm__ volatile("fldcw %0"::"m"(old));
   printf("walk %d %d",k,(int)(fm-shapes[1].core.f)); float values[]={d,a[0],a[1],a[2],b[0],b[1],b[2],n[0],n[1],n[2]}; for(int i=0;i<10;i++){uint32_t bits;memcpy(&bits,&values[i],4);printf(" %08x",bits);} puts("");
 }
 for(int k=0;k<6;k++) {
   struct {char pad[32];Core core;} shapes[2]={{0}};
   unsigned char poses[2][0xd4]={{0}}; Feature *fa=NULL,*fb=NULL;
   float cases[][3]={{3,4,0},{0,0,0},{0.13f,-1.21f,2.78f}};
   for(int j=0;j<2;j++)for(int n=0;n<3;n++)put(poses[j],0x40+n*20,1);
   for(int n=0;n<3;n++)put(poses[0],n*4,cases[k%3][n]);
   unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
   for(int j=0;j<2;j++)((void(__attribute__((thiscall)) *)(Core*,int))(im+0xac370))(&shapes[j].core,0);
   *(int*)(im+0x24464c)=0;
   float a[3]={0},b[3]={0},n[3]={0};
   float d=k<3 ? cached(poses[0],poses[1],&shapes[0],&shapes[1],&fa,&fb,a,b,n)
               : cached(poses[1],poses[0],&shapes[1],&shapes[0],&fb,&fa,a,b,n);
   __asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
   printf("points %d",k);float values[]={d,a[0],a[1],a[2],b[0],b[1],b[2],n[0],n[1],n[2]};
   for(int j=0;j<10;j++){uint32_t x;memcpy(&x,&values[j],4);printf(" %08x",x);}puts("");
 }
 for(int k=0;k<48;k++) {
   struct {char pad[32];Core core;} shapes[2]={{0}};
   unsigned char poses[2][0xd4]={{0}}; Feature *fa=NULL,*fb=NULL;
   Vec first={-2,0,0},last={2,0,0};
   float cases[][3]={{0,0,1},{3,0,1},{5,3,1},{0,3,1},{1,1,0},{3,3,2}};
   for(int j=0;j<2;j++)for(int n=0;n<3;n++)put(poses[j],0x40+n*20,1);
   for(int n=0;n<3;n++)put(poses[1],n*4,cases[k%6][n]);
   put(poses[1],0x40,0.6f);put(poses[1],0x44,-0.8f);put(poses[1],0x50,0.8f);put(poses[1],0x54,0.6f);
   unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
   for(int j=0;j<2;j++)((void(__attribute__((thiscall)) *)(Core*,Vec*,Vec*))(im+0xac440))(&shapes[j].core,&first,&last);
   if(k>=24){fa=&shapes[0].core.f[2];fb=&shapes[1].core.f[2];}
   *(int*)(im+0x24464c)=0;
   float a[3]={0},b[3]={0},n[3]={0};
   float d=k%24<12 ? cached(poses[0],poses[1],&shapes[0],&shapes[1],&fa,&fb,a,b,n)
                : cached(poses[1],poses[0],&shapes[1],&shapes[0],&fb,&fa,a,b,n);
   __asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
   printf("segments %d %d %d",k,(int)(fa-shapes[0].core.f),(int)(fb-shapes[1].core.f));
   float values[]={d,a[0],a[1],a[2],b[0],b[1],b[2],n[0],n[1],n[2]};
   for(int j=0;j<10;j++){uint32_t x;memcpy(&x,&values[j],4);printf(" %08x",x);}puts("");
 }
 for(int k=0;k<128;k++) {
   struct {char pad[32];Core core;} shapes[2]={{0}};
   unsigned char poses[2][0xd4]={{0}}; Feature *fa=NULL,*fb=NULL;
   Vec first={-0.1f,0,0},last={0.1f,0,0};
   Vec verts[]={{0,0,0},{1,0,0},{0,1,0},{0,0,1}}; int faces[][8]={{3,0,2,1},{3,0,1,3},{3,0,3,2},{3,1,2,3}};
   float cases[][3]={{0.3f,0.2f,5},{0.3f,0.2f,-5},{0.3f,4,0.2f},{0.3f,-4,0.2f},{3,0.2f,0.2f},{-3,0.2f,0.2f},{0.3f,0.2f,0.2f},{0.3f,0.2f,3}};
   for(int j=0;j<2;j++)for(int n=0;n<3;n++)put(poses[j],0x40+n*20,1);
   for(int n=0;n<3;n++)put(poses[0],n*4,cases[k%8][n]);
   if((k/8)%2){put(poses[1],0x40,0.6f);put(poses[1],0x44,-0.8f);put(poses[1],0x50,0.8f);put(poses[1],0x54,0.6f);}
   unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
   ((void(__attribute__((thiscall)) *)(Core*,Vec*,Vec*))(im+0xac440))(&shapes[0].core,&first,&last);
   if(k%64<32) ((void(__attribute__((thiscall)) *)(Core*,float,float,float))(im+0xac1c0))(&shapes[1].core,1,2,3);
   else ((int(__attribute__((thiscall)) *)(Core*,int,Vec*,int,void*))(im+0xac150))(&shapes[1].core,4,verts,4,faces);
   if(k>=64){fa=&shapes[0].core.f[2];fb=&shapes[1].core.f[shapes[1].core.nf-1];}
   *(int*)(im+0x24464c)=0;
   float a[3]={0},b[3]={0},n[3]={0};
   float d=k%32<16 ? cached(poses[0],poses[1],&shapes[0],&shapes[1],&fa,&fb,a,b,n)
                : cached(poses[1],poses[0],&shapes[1],&shapes[0],&fb,&fa,a,b,n);
   __asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
   printf("segmentbox %d %d %d",k,(int)(fa-shapes[0].core.f),(int)(fb-shapes[1].core.f));
   float values[]={d,a[0],a[1],a[2],b[0],b[1],b[2],n[0],n[1],n[2]};
   for(int j=0;j<10;j++){uint32_t x;memcpy(&x,&values[j],4);printf(" %08x",x);}puts("");
 }
 for(int k=0;k<8;k++) {
   struct {char pad[32];Core core;} shapes[2]={{0}};unsigned char poses[2][0xd4]={{0}};
   Feature *fa,*fb;Vec first={-2,0,0},last={2,0,0};
   for(int j=0;j<2;j++)for(int n=0;n<3;n++)put(poses[j],0x40+n*20,1);
   put(poses[0],0,k%2?1.5f:1.0f);
   unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
   ((void(__attribute__((thiscall)) *)(Core*,Vec*,Vec*))(im+0xac440))(&shapes[0].core,&first,&last);
   ((void(__attribute__((thiscall)) *)(Core*,float,float,float))(im+0xac1c0))(&shapes[1].core,1,2,3);
   fa=&shapes[0].core.f[2];fb=&shapes[1].core.f[24]; *(int*)(im+0x24464c)=0;
   float a[3]={0},b[3]={0},n[3]={0};
   float d=k<4 ? cached(poses[0],poses[1],&shapes[0],&shapes[1],&fa,&fb,a,b,n)
               : cached(poses[1],poses[0],&shapes[1],&shapes[0],&fb,&fa,a,b,n);
   __asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
   printf("cross %d %d %d",k,(int)(fa-shapes[0].core.f),(int)(fb-shapes[1].core.f));
   float values[]={d,a[0],a[1],a[2],b[0],b[1],b[2],n[0],n[1],n[2]};
   for(int j=0;j<10;j++){uint32_t x;memcpy(&x,&values[j],4);printf(" %08x",x);}puts("");
 }
 for(int k=0;k<16;k++) {
   struct {char pad[32];Core core;} shapes[2]={{0}};unsigned char poses[2][0xd4]={{0}};
   Feature *fa,*fb;Vec first={-2,0,0},last={2,0,0};
   float cases[][3]={{0,1,0},{1,2,0},{3,1,0},{5,1,0},{0,0,0},{-1,1,0},{-3,1,0},{-5,1,0}};
   for(int j=0;j<2;j++)for(int n=0;n<3;n++)put(poses[j],0x40+n*20,1);
   for(int n=0;n<3;n++)put(poses[1],n*4,cases[k%8][n]);
   unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
   for(int j=0;j<2;j++)((void(__attribute__((thiscall)) *)(Core*,Vec*,Vec*))(im+0xac440))(&shapes[j].core,&first,&last);
   fa=&shapes[0].core.f[2];fb=&shapes[1].core.f[2]; *(int*)(im+0x24464c)=0;
   float a[3]={0},b[3]={0},n[3]={0};
   float d=k<8 ? cached(poses[0],poses[1],&shapes[0],&shapes[1],&fa,&fb,a,b,n)
               : cached(poses[1],poses[0],&shapes[1],&shapes[0],&fb,&fa,a,b,n);
   __asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
   printf("parallel %d %d %d",k,(int)(fa-shapes[0].core.f),(int)(fb-shapes[1].core.f));
   float values[]={d,a[0],a[1],a[2],b[0],b[1],b[2],n[0],n[1],n[2]};
   for(int j=0;j<10;j++){uint32_t x;memcpy(&x,&values[j],4);printf(" %08x",x);}puts("");
 }
 for(int k=0;k<108;k++) {
   struct {char pad[32];Core core;} shapes[2]={{0}};unsigned char poses[2][0xd4]={{0}};
   Feature *fa=NULL,*fb=NULL;Vec first={-2,0,0},last={2,0,0};
   for(int j=0;j<2;j++)for(int n=0;n<3;n++)put(poses[j],0x40+n*20,1);
   int v=k%27;for(int n=0;n<3;n++){put(poses[0],n*4,(float)((v%3-1)*3));v/=3;}
   if((k/27)%2){put(poses[1],0x40,0.6f);put(poses[1],0x44,-0.8f);put(poses[1],0x50,0.8f);put(poses[1],0x54,0.6f);}
   unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
   ((void(__attribute__((thiscall)) *)(Core*,Vec*,Vec*))(im+0xac440))(&shapes[0].core,&first,&last);
   ((void(__attribute__((thiscall)) *)(Core*,float,float,float))(im+0xac1c0))(&shapes[1].core,1,2,3);
   if(k>=54){fa=&shapes[0].core.f[2];fb=&shapes[1].core.f[24];} *(int*)(im+0x24464c)=0;
   float a[3]={0},b[3]={0},n[3]={0};
   float d=k%54<27 ? cached(poses[0],poses[1],&shapes[0],&shapes[1],&fa,&fb,a,b,n)
                  : cached(poses[1],poses[0],&shapes[1],&shapes[0],&fb,&fa,a,b,n);
   __asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
   printf("grid %d %d %d",k,(int)(fa-shapes[0].core.f),(int)(fb-shapes[1].core.f));
   float values[]={d,a[0],a[1],a[2],b[0],b[1],b[2],n[0],n[1],n[2]};
   for(int j=0;j<10;j++){uint32_t x;memcpy(&x,&values[j],4);printf(" %08x",x);}puts("");
 }
 for(int k=0;k<12;k++) {
   struct {char pad[32];Core core;} shapes[2]={{0}};unsigned char poses[2][0xd4]={{0}}; Feature *fa=NULL,*fb=NULL;
   for(int j=0;j<2;j++)for(int n=0;n<3;n++)put(poses[j],0x40+n*20,1);
   put(poses[1],0,4+(k%3));put(poses[1],4,5);put(poses[1],8,7);
   unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
   for(int j=0;j<2;j++)((void(__attribute__((thiscall)) *)(Core*,float,float,float))(im+0xac1c0))(&shapes[j].core,1,2,3);
   for(int i=0;i<shapes[0].core.nf;i++)if(shapes[0].core.f[i].kind==1&&shapes[0].core.f[i].anchor==&shapes[0].core.v[7])fa=&shapes[0].core.f[i];
   for(int i=0;i<shapes[1].core.nf;i++)if(shapes[1].core.f[i].kind==1&&shapes[1].core.f[i].anchor==&shapes[1].core.v[0])fb=&shapes[1].core.f[i];
   if(!fa||!fb)return 11; *(int*)(im+0x24464c)=0;
   float a[3]={0},b[3]={0},n[3]={0};
   float d=k<6 ? cached(poses[0],poses[1],&shapes[0],&shapes[1],&fa,&fb,a,b,n)
               : cached(poses[1],poses[0],&shapes[1],&shapes[0],&fb,&fa,a,b,n);
   __asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
   printf("poly %d %d %d",k,(int)(fa-shapes[0].core.f),(int)(fb-shapes[1].core.f));
   float values[]={d,a[0],a[1],a[2],b[0],b[1],b[2],n[0],n[1],n[2]};
   for(int j=0;j<10;j++){uint32_t x;memcpy(&x,&values[j],4);printf(" %08x",x);}puts("");
 }
 for(int k=0;k<96;k++) {
   struct {char pad[32];Core core;} shapes[2]={{0}};unsigned char poses[2][0xd4]={{0}}; Feature *fa=NULL,*fb=NULL;
   Vec verts[]={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};int faces[][8]={{3,0,2,1},{3,0,1,3},{3,0,3,2},{3,1,2,3}};
   float cases[][3]={{4,5,7},{3,0,0},{0,4,0},{0,0,5},{0,0,0},{1,1,1},{2,2,2},{0.5f,0.5f,0.5f}};
   for(int j=0;j<2;j++)for(int n=0;n<3;n++)put(poses[j],0x40+n*20,1);
   for(int n=0;n<3;n++)put(poses[1],n*4,cases[k%8][n]);
   if((k/8)%2){put(poses[1],0x40,0.6f);put(poses[1],0x44,-0.8f);put(poses[1],0x50,0.8f);put(poses[1],0x54,0.6f);}
   unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
   for(int j=0;j<2;j++)if((k/32)==2||(j==1&&(k/32)==1))((int(__attribute__((thiscall)) *)(Core*,int,Vec*,int,void*))(im+0xac150))(&shapes[j].core,4,verts,4,faces);
   else ((void(__attribute__((thiscall)) *)(Core*,float,float,float))(im+0xac1c0))(&shapes[j].core,1,2,3);
   if((k/16)%2){fa=&shapes[0].core.f[shapes[0].core.nf-1];fb=&shapes[1].core.f[shapes[1].core.nf-1];}
   *(int*)(im+0x24464c)=0;float a[3]={0},b[3]={0},n[3]={0};
   float d=(k/8)%2==0 ? cached(poses[0],poses[1],&shapes[0],&shapes[1],&fa,&fb,a,b,n)
                      : cached(poses[1],poses[0],&shapes[1],&shapes[0],&fb,&fa,a,b,n);
   __asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
   printf("polymix %d %d %d",k,(int)(fa-shapes[0].core.f),(int)(fb-shapes[1].core.f));
   float values[]={d,a[0],a[1],a[2],b[0],b[1],b[2],n[0],n[1],n[2]};
   for(int j=0;j<10;j++){uint32_t x;memcpy(&x,&values[j],4);printf(" %08x",x);}puts("");
 }
 if(argc==3) {
   FILE *fixture=fopen(argv[2],"rb");if(!fixture)return 12;uint32_t count;
   if(fread(&count,4,1,fixture)!=1||count==0)return 13;
   for(uint32_t id=0;id<count;id++){
     uint32_t nv,nf;float mass;
     if(fread(&nv,4,1,fixture)!=1||fread(&nf,4,1,fixture)!=1||fread(&mass,4,1,fixture)!=1||nv>10000||nf>10000)return 13;
     Vec *verts=calloc(nv,sizeof(Vec));int (*faces)[8]=calloc(nf,sizeof(*faces));
     if(fread(verts,sizeof(Vec),nv,fixture)!=nv)return 14;
     for(uint32_t i=0;i<nf;i++){faces[i][0]=3;if(fread(&faces[i][1],4,3,fixture)!=3||fseek(fixture,12,SEEK_CUR))return 15;}
     struct {char pad[32];Core core;} shapes[2]={{0}};
     unsigned short old,cw=0x007f;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fldcw %0"::"m"(cw));
     unsigned char created=((unsigned char(__attribute__((thiscall)) *)(Core*,int,Vec*,int,void*))(im+0xac150))(&shapes[0].core,nv,verts,nf,faces);
     if(created!=0){__asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));printf("car %u geometry-failure\n",id);((void(__attribute__((fastcall)) *)(Core*))(im+0xac6e0))(&shapes[0].core);free(verts);free(faces);continue;}
     ((void(__attribute__((thiscall)) *)(Core*,float,float,float))(im+0xac1c0))(&shapes[1].core,1,2,3);
     for(int k=0;k<16;k++) {
       unsigned char poses[2][0xd4]={{0}};Feature *fa=NULL,*fb=NULL;
       for(int j=0;j<2;j++)for(int n=0;n<3;n++)put(poses[j],0x40+n*20,1);
       put(poses[1],0,k%4==0?6:k%4==1?0:k%4==2?1:3);put(poses[1],4,k%4==2?1:0);put(poses[1],8,k%4==3?2:0);
       if((k/4)%2){put(poses[1],0x40,0.6f);put(poses[1],0x44,-0.8f);put(poses[1],0x50,0.8f);put(poses[1],0x54,0.6f);}
       if(k>=8){fa=&shapes[0].core.f[shapes[0].core.nf-1];fb=&shapes[1].core.f[shapes[1].core.nf-1];}
       *(int*)(im+0x24464c)=0;float a[3]={0},b[3]={0},n[3]={0};
       float d=(k/4)%2==0 ? cached(poses[0],poses[1],&shapes[0],&shapes[1],&fa,&fb,a,b,n)
                          : cached(poses[1],poses[0],&shapes[1],&shapes[0],&fb,&fa,a,b,n);
       __asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
       printf("car %u %d %d %d",id,k,(int)(fa-shapes[0].core.f),(int)(fb-shapes[1].core.f));
       float values[]={d,a[0],a[1],a[2],b[0],b[1],b[2],n[0],n[1],n[2]};
       for(int j=0;j<10;j++){uint32_t x;memcpy(&x,&values[j],4);printf(" %08x",x);}puts("");
       __asm__ volatile("fldcw %0"::"m"(cw));
     }
     __asm__ volatile("fninit");__asm__ volatile("fldcw %0"::"m"(old));
     for(int j=0;j<2;j++)((void(__attribute__((fastcall)) *)(Core*))(im+0xac6e0))(&shapes[j].core);
     free(verts);free(faces);
   } if(fgetc(fixture)!=EOF)return 16;fclose(fixture);
 }
#ifdef WALK_TRACE_SMOKE
 if (getenv("SLRR_TEST_HOOK") && (!getenv("SLRR_TEST_TRACE_PATH") || walk_trace_flush(getenv("SLRR_TEST_TRACE_PATH")) < 0)) return 99;
#endif
 return 0;
}
