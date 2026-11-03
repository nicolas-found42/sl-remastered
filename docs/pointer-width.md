# Pointer width

The original game is a 32-bit program, and a pointer in it takes 4 bytes. This port runs on a 64-bit Mac, where a pointer takes 8 bytes. This page records what that difference does to code copied from `slrr-reverse`, and which way to handle it.

## The problem

`slrr-reverse` rebuilds engine objects with the same byte layout as the original and pins that layout with `static_assert`. The assertions check each field's offset and each structure's size.

Measured on the `ref/` copy of the engine:

- 27 structures carry `sizeof` or `offsetof` assertions. The largest groups are `HostLoadGameInitGi` (16 assertions) and `HostMidPayload` (14).
- Of the engine's 95 `static_assert` lines, 79 pin a `sizeof` or an `offsetof`.
- The engine has 651 `reinterpret_cast` sites to `void*` (428), `void**` (185), `uint32_t*` (37), and `void***` (1). The first lines of `experiments/pointer_width/run.sh` count them. Other cast types exist and are not in this number. The 651 is an upper bound on the places that read or write memory by raw offset for these types. We have not reviewed them.

## The experiment

`experiments/pointer_width/run.sh` takes one real structure, `HostMidPayload`, from `Resources_part1.inc` lines 283 to 331. It reads `ref/` and writes only to `local/pointer_width/`. It compiles the structure three ways and runs a small program that checks the structure's own behavior.

| Approach | What it does | Result on this Mac |
|---|---|---|
| A. Native pointers | Keep the structure as written | 13 of 14 assertions fail, and `sizeof` is 0x1a8 where the original is 0x17c |
| A, assertions removed | Compile it anyway | The constructor writes an 8-byte pointer into a 4-byte slot. In this run it overwrote the next field, and the check `bone_own_head == bone_own_sentinel` failed |
| B. Four-byte handles | Replace each pointer field with a `Ptr32` that holds an index into a handle table | All 14 assertions hold and `sizeof` is 0x17c. The three behavior checks pass, but they are weak. See below |
| C. Memory below 4 GB | Allocate objects below the 4 GB line so real pointers fit in 4 bytes | In this test macOS placed the memory above 4 GB when given a hint, which is a request and not a demand. A smaller zero page made the process exit at launch with `Killed: 9`. We did not look into why, and we did not force an address with `MAP_FIXED` |

Approach B needed 8 edits for this structure, and a script made them: 7 pointer fields became `Ptr32`, and 1 raw pointer store became a handle store. The script is inside `run.sh`.

## What the experiment does not show

- The layout assertions are the real test of B. The behavior checks are close to circular. One of them reads the back-link as a handle, which is exactly what the rewritten store writes, and the other two compare `Ptr32` with `void*`, which agree by design.
- Two exact text patterns written for this structure make the 8 edits. Another structure may need patterns that these do not match.
- It covers 1 of 27 structures. Each of the others may hold a different mix of pointers.
- It does not review the 651 cast sites. Code that reads a pointer from a raw offset breaks under B unless someone rewrites it to read a handle.
- It does not measure the cost of the handle table, the lifetime of handles, or use from more than one thread.
- The corruption under A is undefined behavior. It happened here, and another compiler or layout could behave differently.

## Recommendation

This is a judgment based on one structure, and phase 3 tests it again on a native handler.

1. New code that we write uses native 64-bit pointers.
2. A structure that must keep the original byte layout uses 4-byte handles, as in B. That covers structures that code reads by raw offset, structures stored in saves, and structures passed to scripts.
3. Copied structures from `slrr-reverse` convert with a script, and a person reviews each raw cast.
4. Approach C looks closed on this machine, on the evidence of one test that we did not push with a forced address.

One question stays open and matters more than the three approaches. `slrr-reverse` keeps the original layout so that it can transcribe each decompiled handler offset by offset. If we write each handler against clean typed structures, the layout matters only for data formats, and most of these 27 structures can use native pointers. Phase 3 settles it by porting one native handler both ways.

## Rerun

Run `experiments/pointer_width/run.sh`. It prints the table above and saves the full output to `local/pointer_width/results.txt`.
