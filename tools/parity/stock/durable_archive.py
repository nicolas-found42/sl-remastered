"""Validate frozen durable-ring bytes without claiming target/replay completeness.

The caller must independently prove that all writers have stopped and bind the
mapping nonce to the actual target process. These bytes cannot prove either fact.
Holes survive as explicit states; no absent payload is synthesized. This transport
validator deliberately does not interpret owned fixture events as API responses.
"""
from dataclasses import dataclass
import hashlib
import struct

HEADER_BYTES = 128
SLOT_BYTES = 1040
PAYLOAD_BYTES = 1024
MAX_SLOTS = 32768
MAX_TICKET = 0x1fffffff
MAX_ARCHIVE_BYTES = 64 * 1024 * 1024


class ArchiveError(ValueError):
    pass


def require(condition, reason):
    if not condition:
        raise ArchiveError(reason)


@dataclass(frozen=True)
class Record:
    ticket: int
    generation: int
    kind: int
    payload: bytes


@dataclass(frozen=True)
class FrozenArchive:
    records: tuple
    unfinished: tuple
    next_ticket: int
    ack_ticket: int
    flags: int
    collector_lock: int
    mapping_sha256: str
    journal_sha256: str

    @property
    def transport_complete(self):
        # Even True says nothing about unhooked calls or missing API replies.
        return not (self.unfinished or self.flags or self.collector_lock)


def inspect(mapping, journal, nonce):
    """Inspect exact immutable bytes of a stopped mapping and its full journal.

    Journal order is acknowledged prefix followed by frozen committed/reading
    tail in ticket order. Frozen tail records need not have been acknowledged.
    A collector killed after write but before ack may leave an identical record
    already in the journal; it must appear once, never be appended twice.
    """
    require(type(mapping) is bytes and type(journal) is bytes, 'immutable raw bytes required')
    require(type(nonce) is tuple and len(nonce) == 4 and
            all(type(v) is int and 0 <= v <= 0xffffffff for v in nonce), 'nonce words')
    require(HEADER_BYTES <= len(mapping) <= HEADER_BYTES + MAX_SLOTS * SLOT_BYTES, 'mapping bound')
    require(len(journal) <= MAX_ARCHIVE_BYTES and len(journal) % SLOT_BYTES == 0, 'journal bound/alignment')
    header = struct.unpack_from('<32I', mapping)
    magic, version, hb, sb, slots, total = header[:6]
    require((magic, version, hb, sb) == (0x44525231, 1, HEADER_BYTES, SLOT_BYTES), 'mapping ABI')
    require(1 <= slots <= MAX_SLOTS and total == len(mapping) == hb + slots * sb, 'mapping shape')
    require(header[6:10] == nonce and not any(header[14:]), 'mapping nonce/reserved words')
    next_ticket, ack, flags, lock = header[10:14]
    require(1 <= ack <= next_ticket <= MAX_TICKET + 1 and next_ticket - ack <= slots, 'ticket range')
    require(flags & ~31 == 0 and lock in (0, 1), 'unknown fault/collector state')
    count = len(journal) // SLOT_BYTES
    require(ack - 1 <= count, 'acknowledged records missing')
    retained = {}
    unfinished = []
    tail = []
    for index in range(slots):
        offset = hb + index * sb
        raw = mapping[offset:offset+sb]
        tag, kind, length, reserved = struct.unpack_from('<4I', raw)
        first = index + 1
        if first >= next_ticket:
            require(raw == bytes(sb), 'unreserved slot changed')
            continue
        ticket = first + ((next_ticket - 1 - first) // slots) * slots
        state = tag & 7
        # Reservation increments next_ticket before claiming this slot. Death
        # between those atomics leaves the previous generation's FREE tag.
        previous = ticket - slots if ticket > slots else 0
        if ticket >= ack and tag == previous << 3:
            unfinished.append((ticket, -1))  # reserved ordinal, slot not claimed
            if previous:
                require(reserved == 0 and kind != 0 and length <= PAYLOAD_BYTES, 'previous free slot payload shape')
                retained[previous] = (kind, raw[16:16+length])
            else:
                require(raw == bytes(sb), 'unclaimed initial slot changed')
            continue
        require(tag >> 3 == ticket and state <= 4 and reserved == 0, 'slot generation/state')
        if ticket < ack:
            require(state == 0, 'acknowledged slot still owned')
        elif state in (3, 4):
            tail.append(ticket)
        elif state == 0:
            # dr_ack frees the tag before advancing ack_ticket. A collector
            # killed between those stores has already archived this record.
            # Retain it once, while keeping the unfinished ack explicit.
            tail.append(ticket)
            unfinished.append((ticket, 0))
        else:
            unfinished.append((ticket, state))
        if state in (0, 3, 4):
            # dr_commit only writes length bytes. A reused mapping slot can
            # retain an older longer payload beyond length; copy_record zeros
            # the journal tail, but those mapping bytes are not observations.
            require(kind != 0 and length <= PAYLOAD_BYTES, 'published payload shape')
            retained[ticket] = (kind, raw[16:16+length])
    expected = list(range(1, ack)) + sorted(tail)
    require(count == len(expected), 'journal count differs from acknowledged/frozen records')
    records = []
    for index, ticket_expected in enumerate(expected):
        offset = index * sb
        ticket, generation, kind, length = struct.unpack_from('<4I', journal, offset)
        require(ticket == ticket_expected and generation == (ticket - 1) // slots, 'journal ticket/generation/order')
        require(kind != 0 and length <= PAYLOAD_BYTES, 'journal kind/payload length')
        payload = journal[offset+16:offset+16+length]
        require(journal[offset+16+length:offset+sb] == bytes(PAYLOAD_BYTES-length), 'journal unused payload bytes')
        if ticket in retained:
            require((kind, payload) == retained[ticket], 'journal differs from retained mapping')
        records.append(Record(ticket, generation, kind, payload))
    return FrozenArchive(tuple(records), tuple(sorted(unfinished)), next_ticket, ack, flags, lock,
                         hashlib.sha256(mapping).hexdigest(), hashlib.sha256(journal).hexdigest())
