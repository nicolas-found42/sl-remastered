#ifndef SLRR_DURABLE_RING_H
#define SLRR_DURABLE_RING_H
#include <stddef.h>
#include <stdint.h>

/* Shared ABI: uint32 words and byte payload only. Initialize before publishing
 * the mapping; immutable layout/nonce must never change while actors run.
 * No pointers, allocations, waits or I/O inside the producer operations.
 * This protocol does not qualify mapping attachment, target ABI or replay.
 */
#define DR_MAGIC UINT32_C(0x44525231)
#define DR_VERSION 1u
#define DR_PAYLOAD 1024u
#define DR_MAX_SLOTS 32768u
#define DR_MAX_TICKET UINT32_C(0x1fffffff)
enum dr_state { DR_FREE=0, DR_RESERVED=1, DR_WRITING=2, DR_COMMITTED=3, DR_READING=4 };
enum dr_result { DR_OK=0, DR_WAIT=1, DR_BUSY=2, DR_FULL=3, DR_FAILED=4, DR_INVALID=5 };
enum dr_flag { DR_OVERFLOW=1u, DR_BAD_STATE=2u, DR_BAD_ACK=4u,
               DR_ARCHIVE_FAILURE=8u, DR_EXHAUSTED=16u };
enum dr_archive_result { DR_ARCHIVE_FAILED=0, DR_ARCHIVE_WRITTEN=1 };
struct dr_header {
  uint32_t magic,version,header_bytes,slot_bytes,slot_count,total_bytes;
  uint32_t nonce[4];
  uint32_t next_ticket,ack_ticket,flags,collector_lock;
  uint32_t reserved[18];
};
struct dr_slot { uint32_t tag,kind,length,reserved; unsigned char payload[DR_PAYLOAD]; };
struct dr_view { void* memory; uint32_t bytes; };
struct dr_reservation { uint32_t ticket,generation; };
struct dr_record { uint32_t ticket,generation,kind,length; unsigned char payload[DR_PAYLOAD]; };
struct dr_summary {
  uint32_t next_ticket,ack_ticket,flags,collector_lock;
  uint32_t reserved,writing,committed,reading,incomplete;
};

uint32_t dr_required_bytes(uint32_t slots);
int dr_init(struct dr_view view,uint32_t slots,const uint32_t nonce[4]);
int dr_validate(struct dr_view view,const uint32_t nonce[4]);
int dr_reserve(struct dr_view view,struct dr_reservation* reservation);
int dr_commit(struct dr_view view,struct dr_reservation reservation,uint32_t kind,
              const void* payload,uint32_t length);
/* One logical collector. BUSY is a nonblocking competing-collector refusal.
 * The copied record is immutable. Ack requires exact ticket/generation/bytes.
 * Caller must derive archive_result from its checked real write/flush result.
 * DR_ARCHIVE_FAILED retains the slot and makes failure sticky.
 */
int dr_take(struct dr_view view,struct dr_record* record);
int dr_ack(struct dr_view view,const struct dr_record* record,int archive_result);
/* Also used for final archive flush/close failure after the last acknowledgement. */
int dr_archive_failed(struct dr_view view);
/* External owner must FIRST establish that producers and collector are stopped.
 * These do not establish process death themselves. Never clear or synthesize
 * missing entries/replies. Frozen copy can retain later committed records even
 * when an earlier dead reservation blocks the live collector.
 */
int dr_frozen_summary(struct dr_view view,struct dr_summary* summary);
int dr_frozen_record(struct dr_view view,uint32_t ticket,struct dr_record* record);
#endif
