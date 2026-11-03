# Sound.enableDebugDump evidence

The stock executable registers `java.sound.Sound.enableDebugDump` with descriptor `(Ljava.util.resource.ResourceRef;)I` and handler `00487f00`. Its executable MD5 is `e686099e7233bb7d7c461b1b35c5064c`. These notes describe a static reading, not a comparison with the running original.

## Reproduce the reading

Run `tools/ghidra_run.sh DecompileDebugDump.java`. The script checks the loaded program's MD5 and decompiles both the handler and its argument helper. Read `local/census/DecompileDebugDump.log` and check for `DEBUG_DUMP_DECOMPILE_OK`. Ghidra headless can return success when a script fails, so the marker matters.

The handler calls `FUN_0045d910(callInfo, 0, &argument)` and returns zero. For a valid call with this descriptor, the helper copies the reference payload into `argument`. The handler does not use that copy. The zero second argument means the helper does not copy the receiver. The stock native dispatcher `FUN_0041fbc0` captures an `I` result and pushes an integer value. The helper can log errors for malformed call information or unsupported descriptor types, so an unconditional zero does not describe all malformed calls.

## Script call

Run `python3 -I tools/menu_trace.py ref/raw/system/Scripts/sound/Sound.class --class java.sound.Sound --method init`. In tree 16, nodes 3 through 10 construct a `GameRef` with `GameRef.WORLDTREEROOT`. Nodes 11 through 16 pass the local object to `enableDebugDump`. The installed `GameRef.class` declares `ResourceRef` as its superclass. Node 18 branches by 63 nodes when the result is zero, reaching node 81 and skipping the debug viewport and camera setup. The branch reading follows `FUN_00420ff0` as documented in `vm-ops.md`.

The tree is static evidence. It does not prove that the port can construct the `GameRef`, perform compatible method lookup, or execute this call. The current native registry implements the valid one-reference return value and rejects invalid argument shapes, but it does not reproduce the helper's malformed-call diagnostics. The native remains `unread` until the project audit can verify compiled registration and an executed behavioral test with the required dispatch and argument semantics.
