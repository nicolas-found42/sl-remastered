# Durable call-boundary records

`durable_wire.c/.h` publishes bounded entry, reply and gap records through the
[durable ring](durable-ring.md). `durable_wire.py` independently reads the final
mapping and journal. This is an experimental serialization layer. It is not yet
connected to Stock, and its generated host fixtures do not qualify target ABI,
process/thread identity, complete interception or replay.

Version 3 has a 144-byte little-endian header, a 512-byte FX snapshot on normal
boundaries and at most 368 data bytes. The header includes all eight pushal
registers, EFLAGS, LastError, CW/MXCSR, five raw arguments and the raw caller.
Raw EAX and FX control words must agree with their header copies. Raw pointers
remain diagnostic values; they are not stable replay addresses.

The ring reservation ticket supplies the shared phase. An entry uses its own
ticket as call identity; a reply references the entry. The reader checks matching
call context and synchronous LIFO nesting. A referenced gap leaves the call open
and ends that lane's qualification. A reserved or partly written record remains
incomplete; the reader never synthesizes a reply. Failure after reservation can
intentionally leave a hole in the transport.

Normal input records carry a 48-byte typed prefix followed by their complete
snapshot. Its twelve words are schema 7, device ID, incarnation, device kind,
element size, input count, boundary count value, flags, snapshot byte count,
related interface pointer, retired-interface count and validity bits. The prefix
does not establish that those identities were actually observed. Buffer validity
is bit 1 and count-pointer validity is bit 2; admitted shapes require exact bits.

- State: 256 keyboard bytes or 16 mouse bytes at both boundaries.
- Buffered keyboard: 20-byte elements, flags zero, capacity 1 through 16, and the
  full caller capacity at both boundaries. Returned counts must not exceed the
  input capacity. Even a zero-event return retains the unchanged buffer tail.
- QueryInterface: the 16-byte input GUID and actual output-pointer preimage at
  entry; four pointer bytes and the actual pointer postimage at return. Successful
  admitted aliases must agree with the related pointer.
- Acquire/AddRef/Release: metadata without a buffer. A zero Release requires
  an observed retirement count. This checks record consistency, not general COM
  alias or lifetime behavior.

The largest buffered record is exactly 144 + 512 + 48 + 320 = 1,024 bytes.
Counts are captured memory values, separate from the five raw call arguments.
Neither the publisher nor the reader dereferences target pointers. A producer
must supply stable nonoverlapping snapshots and independently qualify its memory
guards, raw-state preservation and ordering around the original call.

The reader currently accepts only an externally supplied owned CreateProcess
main-thread binding. Raw TIDs cannot be promoted to worker incarnations. The
external owner must also supply actual process-handle exit evidence and establish
stopped writers before inspecting the frozen mapping. The schema cannot attest
these observations. `normal_pairs_complete` means consistency of supplied bytes;
`target_abi_qualified` and `process_replay_qualified` remain false.

Qualification evidence is under
`local/verify/parity-goal-20261102-092437/durable-wire-input/`: 18 Python tests,
ASan/UBSan C publication read back independently in Python, PE32 compilation,
the focused CTest and three compiled publisher mutations. The mutations replace
the returned count with capacity, omit an untouched buffer tail, or remove
validity. Each produces a transport that the independent artifact audit rejects.
The fixture's CPU snapshots, identities and terminal event are generated; they
are not observations of Wine or Stock.

Earlier version-2 sources, artifacts, raw-register omission controls and Jev
review remain in `durable-wire-qualification/` and `durable-wire-input/before-v3/`.
The current reader rejects that older version rather than silently interpreting
it as typed input evidence. The separate owned runtime's intermittent ordinary
shutdown failure remains unresolved; this serializer does not repair it.
