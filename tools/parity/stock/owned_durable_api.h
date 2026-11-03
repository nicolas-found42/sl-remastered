#ifndef SLRR_OWNED_DURABLE_API_H
#define SLRR_OWNED_DURABLE_API_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#define OWNED_DURABLE_SLOTS 64u
#define OWNED_DURABLE_DATA_COUNT 128u
enum owned_durable_kind { OWNED_ATTACH=1,OWNED_DETACH=2,OWNED_DATA=10,OWNED_EXIT_MARKER=11,OWNED_LATE=12 };
struct owned_durable_event {uint32_t index,thread,stage,view_raw;};
#ifdef OWNED_DURABLE_OWNER
#define OWNED_API __declspec(dllexport)
#else
#define OWNED_API __declspec(dllimport)
#endif
OWNED_API int __cdecl owned_durable_offer(uint32_t kind,uint32_t index);
OWNED_API int __cdecl owned_durable_space(void);
OWNED_API int __cdecl owned_durable_pending(void);
OWNED_API int __cdecl owned_durable_hold_writing(void);
static inline int owned_parse_nonce(const char* text,uint32_t out[4]) {
  unsigned i,j;
  for(i=0;i<4;++i) {
    uint32_t value=0;
    for(j=0;j<8;++j) {
      unsigned char c=(unsigned char)text[8*i+j];uint32_t digit;
      if(c>='0' && c<='9')digit=c-'0';
      else if(c>='a' && c<='f')digit=c-'a'+10;
      else if(c>='A' && c<='F')digit=c-'A'+10;
      else return 0;
      value=(value<<4)|digit;
    }
    out[i]=value;
  }
  return text[32]==0;
}
#endif
