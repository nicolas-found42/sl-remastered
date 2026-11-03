# Bounded Math natives

Ghidra identified the stock executable by MD5 `e686099e7233bb7d7c461b1b35c5064c`.

`Math.sqrt(F)F` is registered at `0047c850`; instructions at `0047c861` and `0047c865` load its input and perform x87 `FSQRT`. `tools/ghidra_run.sh DecompileSqrt.java` records the identified handler; check its log for `SQRT_DECOMPILE_OK`. The host accepts finite, nonnegative normal inputs and signed zero with nearest rounding. Exact squares and signed zero are tested directly, and the actual menu-to-TrackSelect script path calls the registered handler. Negative, subnormal, non-finite, x87 exception and exact general rounding behavior remain unverified; the native is unread in the audit.

`Math.random()F` is registered at `0047c820`. The stock `_rand` at `005d7408` updates thread-local state by multiplying by `0x343fd` and adding `0x269ec3`, then returns bits 16–30. The handler clamps 32767 to 32766 and multiplies by binary32 constant `0x38000100`. Direct tests check a known initial sequence and the clamp. The host state is explicitly supplied per VM thread. Stock seeding, other consumers of the C runtime random state, timing, and complete floating-point behavior still need integration; this native remains unread.
