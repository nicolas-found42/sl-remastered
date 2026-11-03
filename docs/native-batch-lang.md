# Native batch: lang (strings, Vector3, System)

Stock executable `StreetLegal_Redline.bak.exe`, MD5 `e686099e7233bb7d7c461b1b35c5064c`, confirmed with `get_program_info` on the live Ghidra MCP. Most of these handlers sit in undefined gaps of the windowed project, so they were read from the headless project (`local/ghidra-proj`, read-only) with a throwaway script that decompiles and lists instructions at given addresses. Constants were read with the live `read_memory` tool.

## Final state

All 12 handlers are registered in `src/natives/registry.cpp` and their three source files are in the shared CMake list `NATIVE_REGISTRY_SOURCES`, which `sl_data`, `vm_test` and the registry-linked `native_*_test` targets use. The unit tests `native_lang_string_unit`, `native_vector3_unit` and `native_lang_system_unit` call the handlers directly. `native_vm_dispatch_unit` (`tests/native_vm_dispatch_test.cpp`) runs each registered native through `vmExecute` with a synthetic caller tree and the real stock classes, checks the values, and prints the VM-dispatch inventory that `tools/native_audit.py` reads next to the one from `native_vm_audit_test`.

Marked `matched` in `docs/natives.tsv`, each with a VM-dispatch test and evidence in `docs/native_evidence.json`: Integer.toString(I), String.length, String.append(I), String.token, System.exit(String). Their key claims were re-read from the stock executable again (MD5 confirmed): the argument parser call shape, the `%d` format at 00613398, the INT_MIN digit loop in 00554170, the content-keyed intern lookup 0041eb70, the terminating copy 005548c0, and the strtok loop. Every other row of this batch stays `read` with the gaps below.

Float.toString and the four Vector3 handlers pass VM-dispatch checks and appear in the test inventory, but stay `read` because of the PARTIAL gaps.

**System.arraycopy has no VM-dispatch row.** The VM refuses an array for a `Ljava.lang.Object;` parameter (`native argument type mismatch`), because a `L` parameter accepts only an object of that class or an array with exactly the same descriptor. Stock passes any reference through. The test reports this on stderr and leaves the row out of the inventory until `src/vm.cpp` accepts arrays for `Object`.

## Status per handler

| Native | Stock | Status | Gaps |
|---|---|---|---|
| Integer.toString(I) | 00481e30 | Fully read, matched (VM dispatch) | Result is interned by content, as 004174a0 does. The stock GC relinking inside 004174a0 is not modeled and not observable. INT_MIN prints the stock garbage `-./,),(-*,(`, copied from the digit loop. |
| Float.toString(F, String) | 00481dd0 | Read in full, PARTIAL | The format string is used directly (see below). Only literal text, `%%` and one `%f` are modeled. `%d %i %c %s %x %X` and a second `%f` read stack bytes beyond the single double and return an error. A format that ends inside a `%` spec makes stock read past the terminator, so it is an error. A result over 1023 bytes smashes the 1024 byte stack buffer in stock, so it is an error. Values at or above 2^53 depend on x87 precision control (unknown). |
| String.length() | 00481fe0 | Read, matched (VM dispatch) | Bytes, not characters. Receiver must be a String in `arena->strings`. The stock "vm_get_int_field" error path (missing field) is not reachable in the port model. |
| String.append(I) | 00481f50 | Read, matched (VM dispatch) | The 256 byte stack buffer holds at most 255 source bytes, so longer strings are truncated to 255 (tested). Appending a real character to a 255 byte string writes one byte past the buffer in stock (hits the return address), so it is an error. |
| String.token(I, String) | 00481cf0 | Read incl. strtok 00554c70, matched (VM dispatch) | Negative or too large index returns null. Null delimiter String crashes stock, so it is an error. |
| Vector3.length() | 004823e0 | PARTIAL | See precision note. Sum order (z*z + y*y) + x*x. |
| Vector3.distance(Vector3) | 00482550 | PARTIAL | See precision note. Sum order (dx*dx + dy*dy) + dz*dz. |
| Vector3.normalize() | 00482450 | PARTIAL | See precision note. Leaves fields alone when the length is 0 or NaN (x87 compare reads unordered as equal). Scale is 1.0/length applied unrounded, then stored as float. |
| Vector3.rotate(Ypr) | 00482110 + 0054ecd0 | PARTIAL | See precision note. Uses x87 FSIN/FCOS in stock. The host uses libm `sin`/`cos`. Returns the receiver. Matrix and rounding points were copied from the instruction listing. |
| System.arraycopy | 00487660 | PARTIAL | In bounds copies and overlap safe same array copies match the read. The stock bound check uses the script "length" int field while indexing uses a separate element count. The port has one count. Negative values, 32-bit overflow in the bound sum and bad indices dereference address 8 in stock, so they are errors. The two stock error messages go to 005513b0, whose effect after returning was not followed, so they are reported as errors. A zero length array is assumed to have empty storage ("empty array" error), which is unverified. Arrays with different descriptors are rejected (stock moves raw dwords). |
| System.exit(String) | 0047be60 | Read, matched (VM dispatch) | The handler only parses its argument and returns. Parse failures are not reachable. |
| System.log(String) | 0047be80 | PARTIAL | Stock never prints. It calls 0054bed0 twice, which only waits on and releases the semaphore handle at `[0x0062def8+0x100]`. The port does nothing. The semaphore state is engine state and is not modeled. |

## Float.toString and the stock printf

`FUN_00551220` is not the C runtime. It calls a small custom formatter at `00553d70`. Its behavior, read from the instruction listing and checked against the jump table at `005540f4` and byte table at `00554114`:

- Supported conversions are `%` `c` `d` `i` `f` `s` `x` `X`. Everything else (`%e`, `%g`, `%-5f`...) prints nothing and the spec character is consumed.
- Flags: only a leading `0` (zero pad). Then width digits, `.precision` digits, one optional `h`, `l` or `L`.
- `%f` prints the sign, then the integer part through `00554170` with the width applied to the integer part only, then `.`, then the fraction. The fraction is `trunc((|x| - trunc(|x|)) * 1e8)` rounded up by one when the remainder is at least 0.5, printed as 8 zero padded digits, then cut to the precision. So the precision truncates instead of rounding (0.75 with `%.1f` is `0.7`), a precision above 8 prints 8 digits, no precision trims trailing zeros but keeps one digit, and precision 0 prints no decimal point.
- `trunc` is `__ftol` at `005d6750` (FISTP to 64 bits, low 32 bits returned), so values at or above 2^31 wrap. For example 1e10 prints `1410065408.00000001`.
- Constants read from memory: `005f0c18` = 0.0, `005f3ab8` = 1e8, `005f0cf0` = 0.5.

The script String argument arrives as the format directly, as `File.write` and `System.log` also use their String arguments as text. The old note that "L already Native.ptr" in the `ref/slrr-reverse-main` lead is consistent with this.

## Precision note for the Vector3 handlers

The stock code computes in x87 registers. Products of two floats are exact in double. Differences from the host can come only from where intermediate sums are rounded: 24 bits (single precision control, which Direct3D sets unless told not to), 53 bits or 64 bits. The executable has `FLDCW` only in the CRT, so the control word used while scripts run was not determined. The host keeps intermediates in double and rounds to float where the listing stores a float. That is bit exact whenever the stock rounding does not matter, which includes all the exact test values, but it is not proven bit exact for general inputs. `sin` and `cos` have the same open question.

## Skipped natives and why

| Native | Address | Reason |
|---|---|---|
| Math.randomize | 0047c7d0 | Seeds the CRT generator from `QueryPerformanceCounter` through 005516c0. Needs a clock source the arena does not have. |
| Object.toString | 0047c870 | Format `%s<%0x>` is given one value. The second conversion reads an uninitialised stack word. Output is not reproducible. |
| Object.wait / notify / notifyAll | 0047c920 / 0047c950 / 0047c970 | Operate on VM thread and scheduler objects. |
| System.exit() | 0047be50 | Sets the global quit flag at `0063c520`. Engine state. |
| System.currentTime, simTime, syncGameTime, timeWarp | 0047bfe0 / 0047bfd0 / 0047bff0 / 0047bf80 | Clock singletons. |
| System.compileAll, openLib, getConfigOptions, isLoading, isLoadingReset | various | Script compiler, resource packs, config and renderer state. |
| System.setMeasure | 0047c4b0 | Already implemented elsewhere. |
| Thread.init / start / stop / sleep | various | Scheduler thread records. |
| GameType.* | various | Game world, timers and callbacks. |
| Integer.toHexString, String.intValue, String.floatValue | 00481e80 / 00481ed0 / 00481f10 | Not called by any script per `docs/natives_called.tsv`. They are cheap follow ups. |
