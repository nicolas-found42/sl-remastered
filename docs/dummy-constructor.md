# Dummy constructor investigation

The installed `Sound.init` constructs `java.util.resource.Dummy` with `GameRef.WORLDTREEROOT`. Its `Dummy.<init>(I)` tree pushes null and the integer, then uses operation `22` with two arguments. The installed `Dummy.<init>(GameRef,I)` tree pushes its integer and uses operation `22` with one argument. An earlier reading interpreted the integer local in the second tree as the original integer argument and suggested a cycle. That interpretation was wrong.

The Ghidra MCP reported stock executable MD5 `e686099e7233bb7d7c461b1b35c5064c`. The dispatcher at `004212c7` takes the current frame's class from offset `0x34` and receiver from offset `0x30`, then calls `FUN_00408a70`, which invokes `FUN_004207c0` with `<init>`. Frame setup `FUN_00407b10` stores the selected method's declaring class at `+0x34`; `FUN_00404d40` resolves inherited methods to that owner. Operation `23`, by contrast, explicitly takes the frame class's parent at `+0x1c8`.

`FUN_0041dcb0` constructs an argument signature, and `FUN_00417c30` treats the null literal's `L;` type as compatible with reference parameters. `FUN_00405920` checks the current class first and accepts an exact match. `FUN_0041fbc0` pops multiple scripted arguments in reverse order into local slots. In the two-argument constructor, local 2 is the first argument, the null reference, rather than the integer. Its next delegation can therefore select the one-reference constructor. A test of the real `Vector3(FFF)` constructor with three different values confirms the required local ordering in the bounded VM. The corrected startup probe advances beyond the former depth limit. General overload selection and stock runtime behavior still require further verification.

The stock executable is distinct from the mod executable; the provenance of every installed script has not been established. A runtime comparison must use a writable copy outside `ref/` and record the data files used.

A local search found no independent copy or source for this `Dummy.class`. Its SHA-256 is `2b98757360b9b510fd0a219994b7f12aebda10450fb631e636512b89e6c3d64a`, matching this repository's reference manifest; that establishes only that the file has not changed since the baseline. A report in a reference repository describes a different compiler version but does not include class bytes or constructor trees, so it cannot establish this file's provenance or behavior.

The user later identified `.scratch/221-files/ex1/Data1.cab`, alongside a Redline MSI dated 2005. `cabextract -p -F Dummy.class .scratch/221-files/ex1/Data1.cab | shasum -a 256` produces the same SHA-256 as the installed `Dummy.class`. This independently corroborates that this installer contains the same constructor data, but the installer's distribution provenance is not established. Its bundled executable has MD5 `41d48c5c98e72bfae0d822929d471e81`, different from the specified stock executable, so it is not the ground-truth executable for this port.

## State after the createNativeInstance work

Established, with evidence strength noted. Run `build/startup_probe --init` (see `docs/startup-frontier.md`).
- `Dummy.<init>(I)` runs to the end through the real scripts and the registered natives `ResourceRef.newNative`, `GameType.createNativeInstance` and `GameRef.setFlags`. The probe's diagnostic world then holds one game node named `dummy` with flags `0x231` and its own sector link. This is the bounded VM and the host `GameWorld` model, not a recorded run of the stock game (no runtime comparison exists).
- The `getInfo` question is answered from the stock disassembly: the Java argument is the record kind (8, 9, 0x1c) as an int, and the installed answer is 0 (`docs/game-creation-evidence.md`, open item 1). Not confirmed at run time.
- `Sound.init()V` completes. Its `enableDebugDump` test returns 0, so the viewport and camera block is skipped, and the method returns through `29 2b`.

Not established: which world root stock uses (the probe now builds the type-0 `nulltype` root of `FUN_00535e70`), the true instance ids (the probe's ids come from fresh allocators), and the stock order of the Config and Init constructors.
