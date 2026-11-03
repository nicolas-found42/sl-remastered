# Phase 3 plan and findings

Phase 3 builds the script interpreter and the native functions. This page holds the sizing, the exit predicate, and the order of work. It is updated as the phase moves.

## Exit predicate

`docs/natives.tsv` marks every native reached by stock scripts or executable-originated calls as `matched`. The `called` bucket in `docs/natives_called.tsv` is a minimum starting set, not a complete runtime set. A row is `matched` only when all four hold.

1. The handler's decompiled behavior in the stock executable (MD5 `e686099e7233bb7d7c461b1b35c5064c`) was read, and the row names its address.
2. Our C++ implementation exists in `src/` and names the same address in a comment-free table, `src/natives/registry.cpp`.
3. A test drives it and fails if the behavior changes.
4. A script, not a person, checks 1 to 3 for every row. `tools/native_audit.py` runs the compiled registry inventory and VM behavioral-test producer, and joins their identities and addresses with stock evidence. The audit checks traceability; it cannot judge whether a test covers every behavior.

The interpreter has its own predicate. It loads every stock class file, and it runs the `equals` method of `java.lang.Object` and then the scripts that the main menu starts, with each native in a state that is not `unread`. The second half cannot be reached before phase 4 gives the scripts a window to draw into.

## Size, measured

Command: `python3 -I tools/natives_called.py`, which writes `docs/natives_called.tsv`.

| Bucket | Rows | Meaning |
|---|---:|---|
| called | 214 | Registered, declared native in a script, and referenced by a stock script constant pool |
| uncalled | 121 | Registered and declared native, no matching constant-pool reference found |
| registry-only | 25 | Registered, but no script declares that class, name and descriptor |
| script-only | 13 | Declared native in a script, but the executable does not register it |

The 13 script-only rows are the network natives (`netHost`, `netJoin`, `netLeave`) and others that SLRR_rev's `natives_table.inc` also lists. A script that calls one on the stock build calls a native the executable never registered. Several of the 25 registry-only rows differ from a script declaration only in the return type, for example `Chassis.setBuck` is `(IIFFFF)V` in scripts and `(IIFFFF)I` in the registry. Which of the two the stock lookup uses is unread.

The 214 called natives are spread over 34 classes. The largest are `WheelRef` (22), `Part` (21), `System` (15), `ResourceRef` (14), `Chassis` (13), `GameRef` (12), `File` (11), `GameType` (10), and `Viewport` (10).

This count is static. It counts method references in constant pools, rather than executed calls, and misses natives the executable calls on its own and calls through reflection. The installed `Config.loadConfig` dynamically invokes `File.open`, which the inventory labels uncalled; see `docs/file-handler-evidence.md`. `docs/native-lookup.md` records the lookup evidence and the remaining reflection gap. The inventory reader accepts only the exact known stale `Dialog.class` and fails on other parse errors.

## Order of work

1. Read the interpreter in the stock executable and write down what each tree op does. Output `docs/vm-ops.md`.
2. Triage each called native. For each handler record what it touches (its own arguments, a physics record, the renderer, the file system, the clock), whether it stores addresses, and how many lines of decompiled code it has. Output `docs/triage/`.
3. Choose the object model from steps 1 and 2. The pointer-width question is settled here, by porting one handler against a clean structure and against the copied layout. The user selected typed native runtime structures and pointers, with 4-byte handles where needed; see `docs/wheel-comparison.md`.
4. Build the interpreter, then the natives that need nothing from the engine, then the rest.

## Current state

The bounded interpreter executes the installed Config constructor against a temporary copy of the options save. The `--init` startup probe continues through `Sound.init`, viewport creation, and bounded player and cursor creation with SDL keyboard and mouse input, then stops at Camera.create. This is a diagnostic execution and does not run the full menu. See `docs/startup-frontier.md` for the precise limits and continuation. The registry has six matched entries from earlier work, including `System.buildNumber` and five language natives described in `docs/native-batch-lang.md`. The audit joins evidence and tests but cannot determine whether behavior is complete. Partial handlers remain unread. See `docs/STATUS.md` for the current state.
