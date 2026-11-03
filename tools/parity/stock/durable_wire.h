#ifndef SLRR_EXPERIMENTAL_DURABLE_WIRE_H
#define SLRR_EXPERIMENTAL_DURABLE_WIRE_H
#include <stdint.h>
#include <stddef.h>
/* EXPERIMENTAL OWNED WIRE. Not connected to Stock or original hook code.
 * All words little endian; no JSON or pointer dereference in the wire.
 * Ring ticket is phase. ENTRY's own phase is call identity. REPLY references
 * that entry. A GAP never fabricates a response for a missing target return.
 */
#define DW_MAGIC UINT32_C(0x44574331)
#define DW_VERSION 3u
#define DW_HEADER_BYTES 144u
#define DW_FX_BYTES 512u
#define DW_DATA_MAX 368u
#define DW_INPUT_META_BYTES 48u
#define DW_INPUT_SCHEMA 7u
/* Normal input boundaries contain these 12 little-endian words, then the
 * complete bounded snapshot. Count values are memory observations, distinct
 * from the raw pointer arguments in dw_header. validity: buffer=1, count=2.
 * These are caller-attested device identities, not COM lifetime proof.
 */
struct dw_input_meta {
  uint32_t schema,device_id,incarnation,device_kind,element_size;
  uint32_t count_in,count_value,flags,snapshot_bytes;
  uint32_t related_raw,retired_interfaces,validity;
};
_Static_assert(sizeof(struct dw_input_meta)==DW_INPUT_META_BYTES,"input metadata ABI");
enum dw_kind { DW_ENTRY=0x1001, DW_REPLY=0x1002, DW_GAP=0x1003 };
enum dw_identity { DW_UNQUALIFIED=0, DW_OWNED_MAIN=1 };
enum dw_data_role { DW_NO_DATA=0, DW_PREIMAGE=1, DW_POSTIMAGE=2 };
/* Explicitly follows current recorder IDs; no implicit remapping of methods. */
enum dw_api { DW_QPC=0,DW_QPF=1,DW_TICK=2,DW_ASYNC=3,DW_TIME=4,DW_RNG_SEED=5,
  DW_RNG_DRAW=6,DW_INPUT_STATE=7,DW_INPUT_ACQUIRE=8,DW_INPUT_DATA=9,
  DW_INPUT_QUERY=10,DW_INPUT_ADDREF=11,DW_INPUT_RELEASE=12 };
struct dw_header {
  uint32_t magic,version,kind,header_bytes,total_bytes;
  uint32_t phase,call_entry,api;
  uint32_t process_id,thread_id,lane_id,lane_generation,identity;
  uint32_t caller_raw,arg[5];
  uint32_t eax,last_error,cw,mxcsr;
  uint32_t fx_bytes,data_bytes,data_role,gap;
  uint32_t gpr[8],eflags; /* pushal order: EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX */
};
_Static_assert(sizeof(struct dw_header)==DW_HEADER_BYTES,"wire ABI");
_Static_assert(offsetof(struct dw_header,phase)==20,"phase ABI");
_Static_assert(offsetof(struct dw_header,arg)==56,"argument ABI");
_Static_assert(offsetof(struct dw_header,fx_bytes)==92,"variable data ABI");
_Static_assert(DW_HEADER_BYTES+DW_FX_BYTES+DW_DATA_MAX<=1024,"bounded ring payload");
/* Exactly header, followed by fx_bytes (0 or512), then data_bytes (0..368).
 * No extra padding/trailing bytes. Caller buffer lifetimes remain producer's
 * responsibility. The rawFX CW/MXCSR fields must match this header when present.
 * DW_OWNED_MAIN is valid ONLY after actual CreateProcess main-thread binding;
 * Stock's raw numeric TID must not be guessed into lane_generation=1.
 */
/* Caller owns stable, nonoverlapping input and valid mapping; this serializer
 * does not dereference target pointers or attest process/thread identities.
 * A failure after reservation intentionally leaves incomplete transport.
 */
#include "durable_ring.h"
int dw_publish(struct dr_view view,const struct dw_header* snapshot,
               const unsigned char* fx,const unsigned char* data,uint32_t* phase);
#endif
