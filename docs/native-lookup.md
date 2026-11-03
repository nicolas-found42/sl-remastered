# Stock native lookup

The evidence below comes from Ghidra's open `StreetLegal_Redline.bak.exe`, MD5 `e686099e7233bb7d7c461b1b35c5064c`. Addresses are virtual addresses in that executable. This is a reading of the dispatcher, not a behavioral comparison against a running original.

## Registration and dispatch

`FUN_00416b00` registers a class name, method name, descriptor, and handler. It interns the descriptor through `FUN_0041dc30` and `FUN_0041e800`. Its hash is 73 times the sum of the method-name bytes plus the sum of the class-name bytes. `FUN_00423da0` uses the low seven bits to choose a bucket and then checks the full hash. `FUN_00423de0` continues through entries with the same hash.

In the native branch of `FUN_0041fbc0`, the dispatcher checks the class and method strings and calls `FUN_0041ded0` on the registered and requested descriptors. That helper compares their interned string pointers. `FUN_0041e610` interns strings by length and content, so the comparison includes the return type. The dispatcher advances to another candidate when a comparison fails. If no candidate remains, it reports a missing native implementation and returns `-1`.

After a candidate is selected, `FUN_0041fbc0` chooses how to call and capture the result using the requested descriptor's return type. This matters because integer, float, reference, and void results follow different branches.

## A descriptor discrepancy

At `0044298d`, `FUN_004427e0` registers `java.game.parts.bodypart.Chassis.setBuck` with descriptor `(IIFFFF)I` and handler `0043e110`. The script inventory records `(IIFFFF)V` in `docs/natives_called.tsv`. These descriptors have identical arguments but different return types. The native dispatch comparison above does not treat them as equal. The inventory records no direct call sites for the script declaration, so this finding alone does not establish a gameplay failure. Reflection and other paths still need examination.

The Ghidra window's analysis does not define a function at `0043e110`, so the handler's behavior has not been checked here. No native has been marked matched.

## Runtime method lookup

`FUN_004207c0` calls `FUN_0041dcb0`, then passes the resulting descriptor and method name to `FUN_00404910`. On success it calls `FUN_0041fbc0`. `FUN_0041dcb0` builds a descriptor beginning with `(` and ending with `)`, with no return type. It takes argument types from the operand stack; for non-null references and arrays it uses the object's runtime class, and for other values it uses the stored type. `FUN_0041ddc0` compares cached argument types with a compatibility function, excluding the last cached entry. `FUN_00405920`, called by `FUN_00404910` on a cache miss, searches method lists and walks parent classes. It keeps the lowest nonnegative compatibility score, stops on a zero score, and emits a diagnostic for a tied best score in the same class. The complete tie and conversion behavior still needs a separate check.

The installed `ref/raw/system/Scripts/lang/Object.class` has no `runMethod` string. The stock executable has no ASCII `runMethod` string either, as checked with `strings -a`; `docs/natives.tsv` also has no such registry entry. These observations do not rule out another class file, generated name, or an indirect implementation. The connection between this lookup and a script-level `Object.runMethod` entry point is unproven. Its argument-only descriptor must not be confused with the full descriptor used later to select a native handler.

## Calls initiated by the executable

`FUN_00408560` calls `FUN_004207c0` with the literal method name `finalize` on an object before freeing its fields. If the selected method is native, the native branch of `FUN_0041fbc0` can dispatch it. This proves an executable-initiated method call, but does not establish that a particular `finalize` implementation is native.

`FUN_004255e0` creates a VM thread, prepares arguments, and calls `FUN_00408a30`, which forwards the object's class and method name to `FUN_004207c0`. Its wrapper `FUN_00425a90` has executable callers that supply literal names including `animate` at `0042710c`, `getInfo` at `00426598`, and `updateDamage` at `0043c4e2`. These are concrete callbacks into script methods. Whether any resolved target is native depends on the loaded class and remains unverified. The Ghidra window has undefined code, so its caller lists are not a complete census.

## Spot checks of language and system triage

The Ghidra window does not define functions at the handler addresses below. These checks use bytes read from its stock program memory, together with the registered addresses in `FUN_00487f20` where applicable. They do not establish complete handler equivalence in the port.

- `System.buildNumber` at `0047c0b0` contains `mov eax, 0x25e; ret`, confirming the value 606 in `docs/triage/lang-system.tsv`.
- `System.exit()` at `0047be50` writes 1 to `0063c520` and returns. The overload at `0047be60` calls the argument parser `FUN_0045d910` and returns without that write. The parser has error paths, so describing the overload as having no effects at all would be too broad.
- `System.currentTime` at `0047bfe0` calls `FUN_005516c0` and multiplies its result by the float at `005f10a4`, approximately 0.001. `FUN_005516c0` reads the performance frequency and counter and computes a difference from `0076a288`, multiplied by the float at `005f0910`, which is 1000. The origin stored at `0076a288` and initialization of that value have not been checked. This supports a scaled elapsed-counter value, not a claim about a calendar clock.
- `Math.random` at `0047c820` masks the called generator's result with `0x7fff`, changes `0x7fff` to `0x7ffe`, and multiplies by the float at `005f1388`, approximately `1/32767`. The generator at `005d7408` has not been checked in this pass, so its sequence and identity remain unverified.
- `Integer.toString` at `00481e30` passes the format at `00613398`, which contains `%d`, to `FUN_00551220`, then passes the result to `FUN_004174a0`. Neither callee was checked in this pass.
- `GameType.unregisterCallbacks` at `00481cc0` calls `FUN_004274d0`, which immediately returns. The handler also calls argument and object helpers before that call, so the empty callee alone does not prove that the whole handler has no side effects.
