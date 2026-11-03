# Stock script tree evidence

The stock executable loads serialized `TREE` nodes and runs decoded nodes in `FUN_00420ff0`. These notes describe the decompiler output, not a completed interpreter specification. The stock program open in Ghidra reported MD5 `e686099e7233bb7d7c461b1b35c5064c` and 32-bit x86 pointers.

## Loading

`FUN_00411b20` reads a chunk tag and length, then advances by eight bytes plus the length. In the `TREE` branch at `00411c4f`, it reads a tree count and a node count for each tree, allocates eight bytes per node, and calls `FUN_0041a5f0` for each node. `FUN_0041a5c0` determines how many input bytes to advance. For a zero node count, the loader leaves the tree pointer null; otherwise it allocates and decodes the nodes. It stores each pointer in the class data and calls `FUN_00403a90` after the `TREE` chunk. `FUN_0041fbc0` skips a null method tree, so an empty void method returns without executing a frame. This is exercised by the 2005 `OptionsDialog.addCustomGroups()V` and the focused VM test. `FUN_00403a90` initializes and registers the class; it is not the operation dispatcher.

`FUN_0041a5f0` uses the first serialized byte as an index into the 16-bit table at `005f0914`. It stores that table value in the decoded node, reads a 16-bit value at input offset one, and, when the table value masked with `0xf000` is `0x1000`, `0x2000`, or `0x4000`, reads a 32-bit value at offset three. `FUN_0041a5c0` returns seven bytes for those three groups and three otherwise. The table maps indices `00` through `2e` to distinct values whose low byte matches the index. The high bits are part of the decoded operation used by the dispatcher. The meaning of the 16-bit field has not been independently established.

## Execution

`FUN_00420ff0` reads the first 16-bit value of the current decoded node and dispatches on it. It also copies the node's second 16-bit field into thread-associated state before dispatch. The following are narrow observations from that function, with serialized byte indices derived from the stock table.

| Byte | Decoded value | Observed behavior |
|---|---|---|
| `1f` | `101f` | Repeats the operand count times, creating a value with payload zero and pushing it onto the current frame's value array. |
| `27` | `1027` | Creates a value, copies the node's 32-bit operand into its payload, and pushes it onto the current frame's value array. |
| `1c` | `401c` | Pops a value, checks its object payload, resolves the operand as a name, calls `FUN_00408800`, and pushes the result. The error strings identify illegal field access. |
| `25` | `4025` | Pops a value, checks its object payload, resolves the operand as a name, and calls `FUN_00408a30`. The error string identifies an illegal method call. |
| `26` | `4026` | Resolves the operand as a name and calls `FUN_004207c0` with current frame data. Its full method lookup and call semantics remain unverified. |

These observations do not establish operand types, value ownership, error recovery, or the behavior of every operation. The decompiler's inferred calling conventions and local variables are not authoritative. The run loop's branches, arithmetic helpers, object lifetime, method frames, and native calls require separate checks against stock instructions and data before implementation can be called matched.

## `Object.equals` tree

The installed `ref/raw/system/Scripts/lang/Object.class` has a method record named `equals` with descriptor `(Ljava.lang.Object;)I` and tree index 9. Reading the chunk records with `tools/tufa_census.py` and the stock width table gives these nodes. The 16-bit field is 25 for the first seven nodes and 26 for the last two. The table shows operands in decimal.

| Node | Byte | Operand |
|---|---|---|
| 0 | `01` | 0 |
| 1 | `01` | 1 |
| 2 | `07` | 7 |
| 3 | `28` | none |
| 4 | `03` | 1 |
| 5 | `03` | 0 |
| 6 | `2b` | none |
| 7 | `03` | 1 |
| 8 | `03` | 0 |

The method record's tree-index interpretation follows the existing parser's record layout and the stock `FUN_0041fbc0`, which indexes the class tree array using the word at method offset `0x10`. The entire call path and any reachability of the final two nodes remain to be checked at runtime.

For these nodes, `FUN_00420ff0` and its helpers show the following:

- `01` (`1001`) indexes the frame's array at offset `0x18` using the operand and pushes the selected pointer onto the value array at offset `0x28`. The decompiler checks the array's capacity rather than its current count. Out-of-range access pushes null. `FUN_0041fbc0` constructs the frame and adds the receiver and arguments to that first array.
- `07` (`1007`) selects a suboperation using its operand. Suboperation 7 pops two values, copies values whose virtual type is 2, calls `FUN_0041c5c0` on the two values, releases the second, and pushes the first. `FUN_0041c5c0` first calls `FUN_00417570`, then compares the payloads. For `I`, `L`, and `[` it compares the 32-bit payloads; for `F` it compares floating-point payloads. It writes 0 or 1 and changes the first value's type to the integer type. The conversion helper and exceptional cases need further study.
- `28` pops a value through `FUN_0041f7d0` and pushes the resulting value through `FUN_00423ba0`. The pop helper copies a value whose virtual type is 2. The exact ownership implications are unresolved.
- `03` (`1003`) removes and releases the last entry of the frame's array at offset `0x18`. Its branch does not read the serialized operand. It logs a container underflow when the array is empty.
- `2b` pops a value, destroys the current frame, restores the current node pointer through `FUN_0041f7b0`, and pushes the value on the restored frame. If the restored node pointer is null, it sets thread flag `0x40` and exits the loop. The frame destructor and invalid-frame behavior need further study.
- `07` with operand 28 advances to the next node and creates a value whose payload is that node's operand. The integer type at `DAT_0062e00c` is initialized by `FUN_0040fe70` from the string `I` at `0060ab54`. The installed `GameType.getInfo(I)I` and `Dialog.validator(Ljava.lang.String;)I` use a following `0b` node to return integer literals. Other literal forms remain unread.

The dispatcher normally advances the decoded node pointer by eight bytes. Operation `14` (`1014`) instead adds eight times its 32-bit operand to the current pointer and skips the ordinary advance. Operation `15` (`1015`) pops and converts a value using `FUN_00417840`, then takes that same branch if its payload is zero. Operation `16` (`1016`) takes it if the payload is nonzero. `FUN_00417840` converts floats and object or array references to integer truth values. These branches do not occur in the tree above. The partial port supports integer and object conditions and rejects an out-of-tree target. The stock behavior at invalid targets and the interpretation of negative offsets remain unverified.

`07` with operand 30 consumes the next `06` node as a class descriptor. It resolves the class and allocates an object. `FUN_00404e20` initializes class state before allocation. `FUN_00407da0` creates instance fields from the oldest ancestor to the concrete class and runs field initializer trees. Operation `21` then invokes a constructor. Operation `23` invokes a superclass constructor. The port supports only a bounded subset of this sequence and rejects unsupported initializers and class structures.

Operation `1b` resolves a field location. Operation `08` with operand 35 copies a value into that location through `FUN_0041acb0`. The port supports integer fields and a restricted set of object references. It does not yet implement all conversions or the stock lifetime behavior. Operation `10` ends an initializer thread without the method-return behavior of `2a` or `2b`.

## Method lookup and frames

For operation `25` (`4025`), `FUN_00420ff0` obtains the receiver from the value stack and calls `FUN_00408a30` with the resolved operand name when the receiver payload is nonzero. `FUN_00408a30` forwards to `FUN_004207c0` using data from the receiver. Operation `26` (`4026`) also calls `FUN_004207c0`, but uses class and receiver data from the current frame. These calls are separate dispatch paths. Their precise source-language meanings need more evidence.

The host now supports `25` for an arena-owned non-null object: it pops the
receiver, resolves the operand method name against its runtime class and uses
the existing descriptor-driven argument selection. A null or unowned receiver
fails explicitly. `tests/vm_eval_name_test.cpp` exercises the stack receiver,
argument and null cases. Fresh stock logs for `00408a30` and `004207c0` are in
`local/census/DecompileAt-00408a30.log` and `DecompileAt-004207c0.log`.

`FUN_004207c0` calls `FUN_0041dcb0` to construct a signature from the current value stack. That helper pops a value whose payload is the argument count, then examines that many remaining stack entries. For non-null object and array values it uses the object's class type; otherwise it uses the value's stored type. `FUN_0041def0` produces a signature string, and `FUN_004207c0` passes that string and the requested name to `FUN_00404910`.

`FUN_00404910` searches a cache using both strings. On a miss it calls `FUN_00405920`, then resolves and caches the result if found. `FUN_00405920` scans method records with the requested name, compares signatures through `FUN_0041ddc0`, and follows class references while selecting a match. It logs an ambiguity when equally ranked matches occur in the same class. The complete conversion ranking, inheritance rules, and error behavior remain unverified. `FUN_004207c0` reports a missing method if lookup fails; otherwise it calls `FUN_0041fbc0` with the result.

`FUN_0041fbc0` rejects a null receiver when the method record's flag `0x08` is clear. When flag `0x40` is clear, it uses the method record word at offset `0x10` to select a tree pointer from the owning class. For a non-null tree it saves the caller's current node pointer, creates a frame with `FUN_00407b10`, and links that frame into the thread's frame list. If flag `0x08` is clear it appends a receiver value to the new frame's local array. It then pops each argument from the caller's value stack, constructs a value using the corresponding signature type, copies the argument into that value, and appends it to the new frame's local array. The loop counts argument indexes down, so the order on the caller's stack matters. The function installs the new frame and its tree pointer and returns zero to the dispatch loop.

If the selected tree pointer is null or its index fails the shown bounds check, `FUN_0041fbc0` discards the arguments. When the signature has a non-null return type, it pushes a zero integer value and returns one. A method with flag `0x40` follows a separate native-handler lookup and call path. Those details and allocation failures need further checks before implementing either path.

For return, `2a` destroys the current frame and restores the node pointer with `FUN_0041f7b0`. `2b` first pops a return value, performs the same frame transition, and pushes that value onto the restored frame. If the restored node pointer is null, either branch sets thread flag `0x40` and exits the run loop. The frame destructor's ownership and restoration behavior are not fully resolved.

## Evidence limits

`local/census/all.c` has entries for these functions and `tools/ghidra/DecompileAll.java` describes its format. `tools/ghidra_run.sh` checks the stock file's MD5 before running scripts, and the existing `local/census/DecompileAll.log` reports 7,535 decompiled functions. The dump does not embed an executable hash, and the log alone does not prove which binary produced this particular file. Its contents are leads. The statements above were checked against the live Ghidra program after verifying its MD5.

The headless and windowed Ghidra projects have different function counts and analysis state. Decompiled output can contain incorrect inferred types or incomplete control flow. No runtime comparison with the original game was performed.

## Configuration loader's local and constructor operations

In `FUN_00420ff0`, operation `02` consumes a following `06` type node and creates a local, with `FUN_00408d60` initializing its value. A following `2c` exposes the new local as a location. Operation `2d` pops a source and destination and passes them to `FUN_0041acb0` with assignment operation `0x23`; `FUN_0041ab60` performs the assignment. The port supports only integer, float, and restricted reference local defaults and assignment types. It rejects invalid local references and does not reproduce all stock error handling.

Operation `22` invokes the current class constructor on the current receiver through `FUN_00408a70` and `FUN_004207c0`. The installed `File.<init>(String)` uses it to delegate to `File.<init>()`. The port currently accepts the exact non-null, arena-owned String case used by this constructor, as well as its earlier bounded integer and zero-argument cases; general overload conversions remain unimplemented.

The installed `Config.loadConfig` uses `24 11 01 1a` for resolved `File.readInt` and `File.readFloat` calls on a local receiver. The stock dispatcher distinguishes integer from x87 floating-point native results and stores a typed 32-bit result. The following `1b` field location and `08` assignment suboperation 35 store the value; the stock assignment helper can perform conversions. The exact general conversion rules and x87 rounding and NaN behavior remain unverified.

The assignment conversion in `FUN_00417da0` calls `__ftol` at `005d6750` for float-to-integer conversion; its x87 rounding mode truncates toward zero and the low 32 bits are used. The reverse integer-to-float conversion uses `FILD` and `FSTP`; the port accepts exactly representable integers for the supported native call. For operation `07` suboperation 21, the dispatcher `FUN_00420ff0` copies a location's value box with `FUN_00423e30` before calling `FUN_0041bdb0`. The helper first calls `FUN_00417840` to convert floats and references to boolean, then changes the integer copy to one for zero or zero otherwise. Unsupported types retain the stock error boundary. The source field or local therefore does not change. This corrects the earlier in-place interpretation. `tests/vm_eval_name_test.cpp` checks that logical-not returns zero for a nonzero field while leaving the field unchanged.

For float multiplication and addition, the stock instructions at `0041b952` and `0041b740` load binary32 operands, perform x87 arithmetic, and store binary32 results. The port accepts finite normal values and zero using nearest rounding, including ordinary inexact results. It rejects subnormals, underflow, non-finite values, and non-nearest host rounding. Stock control-word initialization and exceptional behavior are not yet verified.

## General native call path

Evidence read from live Ghidra (stock MD5 `e686099e7233bb7d7c461b1b35c5064c`). The port's single native path is `callNative` in `src/vm.cpp`.

**How stock reaches a native.** Operation `24` in `FUN_00420ff0` calls `FUN_004208e0` to resolve the call. A `101a` method reference resolves at load time, while a bare name node produces a record whose type word is `-2`, the dynamic marker. The dynamic record holds the class, the receiver, and the name. For `-2`, `FUN_00420ff0` calls `FUN_004207c0`, which builds the `(types)` signature from the stack with `FUN_0041dcb0`, looks the method up with `FUN_00404910`, and calls `FUN_0041fbc0`. A resolved reference calls `FUN_0041fbc0` directly. So the dynamic call and the resolved call end in the same function. This is why the old `Config.loadConfig` `File.open` special case could be folded into the shared path.

**Native branch of `FUN_0041fbc0` (method flag `0x40`).**
- It hashes the method name and the declaring class name (73 times the name sum plus the class sum) and walks `FUN_00423da0` and `FUN_00423de0`. It compares class name, method name, and the interned descriptor with `FUN_0041ded0`. Nothing found reports "native implementation not found" and returns `-1`. The class used is the class that declares the method record, not the receiver's class, so the port looks handlers up with the declaring class found by ancestry.
- It pops one stack value per signature entry, last argument first, converts a value whose type differs from the signature entry through `FUN_0041ab60`, and appends each to a local array. The receiver slot is appended last, and its payload at offset `+8` is set only when flag `0x08` is clear. The array is therefore `[argN ... arg1, receiver]`.
- The result type is the first character of the return descriptor. `F` reads an x87 float result. `I`, `L`, and `[` read an integer-register result. `V` pushes nothing. A pushed result becomes a new value with the return type.

**`FUN_0045d910`, the shared parameter parser.** It reads the receiver payload from the last slot, then walks the descriptor from the first parameter. The first parameter comes from the slot just below the receiver, and the index counts down. `I` and `F` take the value payload at `+8`. `L` and `[` take the payload pointer, skipping extra `[` characters and then the element type, which must be `I`, `F`, or `L...;`. Any other type logs "CallInfo.parseParameters: unknown ..." and an exhausted array logs "not enough parameters". Stock logs these and keeps running. The port returns an error instead.

**What the port does.** For a call whose method has flag `0x40`, `callNative` does these steps in order.
1. It parses the descriptor into parameters and a return type. Only `I`, `F`, `L...;`, and `[` parameters are representable. Anything else returns `unsupported native descriptor: <class>.<name><descriptor>`.
2. It finds the one registry entry whose declaring class, name, and full descriptor match. None returns `native not registered: <class>.<name><descriptor>`, and two return `ambiguous native`.
3. It requires one stack value per parameter below the count. Fewer returns `missing call arguments`. The count value itself is discarded, as in stock's resolved path.
4. A static method (flag `0x08`) gets no receiver. An instance native needs an arena-owned object whose class is the declaring class or a subclass, from the qualified receiver or the current `this`. Otherwise it returns `missing native receiver` or `invalid native receiver`.
5. Arguments keep descriptor order. `I` takes an integer. `F` takes a float, or an integer that converts exactly. `L` and `[` take null or an arena-owned object whose class is the named class or a subclass, or an array with exactly that descriptor. A mismatch returns `native argument type mismatch: ...`.
6. After the handler runs, its result is checked against the return type. `V` must be the monostate marker, which the port pushes. `I` and `F` need the matching value. `L` and `[` need null or an arena-owned object that fits the descriptor. A mismatch returns `invalid native result` or `invalid native void result`.

A bare name node (`11 02`, a class constant, local, or field qualifier, then `12 name`) takes the dynamic route. The port builds `(types)` from the stack values (`I` for an integer, `F` for a float, the runtime class or array descriptor for an object) and finds the method in the target class and its ancestors by name and parameter text. When several overloads fit, the port picks the most specific: a candidate whose parameter is a strict subclass of another's wins, so the `LoadingScreen.show` call site that stock resolves without error no longer ties; only genuinely incomparable ties stay ambiguous. Stock's full conversion ranking (`FUN_00405920`, `FUN_0041ddc0`) is not a verified copy, so this rule is the port's reading of one observed stock call site, not established stock scoring. A null argument contributes a placeholder to the built text, the direct text lookup cannot match, and the call falls through to the descriptor-driven selection, which fits a null against any reference parameter (`referenceAssignable`). A field of the receiver with the same name returns `dynamic name shadows method`. Stock instead resolves such a name as a field.

**Unverified.**
- Stock compares signatures with a conversion score (`FUN_0041ddc0`, `FUN_00405920`). The port requires parameter text matches, with the subclass-wins rule above; conversion-based fits (int to float on a tie) are not reproduced.
- Stock converts an argument whose type differs from the descriptor. The port converts only integer to float, and only when exact.
- For `L` arguments of a `java.lang.Native` subclass, stock passes the native pointer from the object's `ptr` field. The port passes the object and each handler reads `ptr`.
- Stock native handlers return raw registers. The port's `NativeResult` is typed, so x87 rounding and NaN behavior of float results are not reproduced.
- Dynamic lookup in stock uses a cache (`FUN_00404910`) and a score for ties. Tie handling is not reproduced.
- The dynamic chain in stock supports more qualifier shapes than the three the port reads.
- Scripted calls accept the bounded `I`, `F`, reference and array descriptors parsed by the native signature reader. Dynamic lookup still requires an exact parameter signature.

## Native calls back into scripts, return of `()V` methods, failure trace

**Script call hook.** `VmArena::callScript` finds a non-static script method on an arena-owned receiver and pushes a frame on the active thread. It supports integer and float arguments. The native waits synchronously for the callback result, so a scheduler slice cannot interrupt that callback. `tests/vm_native_call_test.cpp` exercises the callback and failure trace. Stock overload scoring remains unread.

**`()V` script calls that end with a value return.** The installed `Sound.init()V` ends with `29 03 2b` (push a zero value, return it). Stock's `2b` pushes its popped value on the restored frame whatever the descriptor says, so the port now accepts a value return from a `()V` call and pushes that value. Before, it stopped with "invalid script void result". The caller's statement end (`08 27`) discards the value.

**Failure trace.** When a `vmExecute` call fails, `VmArena::failureTrace` lists the class and the source line of the first node of every failing tree, innermost first. The probe prints it. The old message "native not registered" now names the class, method and descriptor.

## Class loading and initialization order

Read from live Ghidra (stock MD5 `e686099e7233bb7d7c461b1b35c5064c`, checked with `get_program_info`). Implemented in `vmInitializeClass`, the `VmArena::ensureClass` hook and `src/class_runtime.hpp`. Tests: `tests/vm_class_init_test.cpp`.

**Verified by reading the decompiled code.**
- `FUN_00403a90` only registers a loaded class in the class table. It does not initialize it. Loading a class (`FUN_00411b20`) therefore runs no script.
- `FUN_00403e80` is the class initializer (its thread is named `THRD_CLASSVAR_INI`). It does nothing if the class word at `+0x1d0` is non-zero. Otherwise, in this order: it looks up the parent (`+0x1c8`, through `FUN_004117a0`, then `FUN_00410890`) and the interfaces (`+0x1cc`); an interface whose kind word at `+0x10` is not 1 logs `Class: Class <name> extends Class <name> Interface`; it calls itself on the parent, then on each interface; it then sets `+0x1d0`; only then does it walk the static field list (`+0x54`) in declaration order. For each field it defines the field (`FUN_004038c0`, which logs `member redefinition` on a clash), and when the field has an initializer tree (record word `+0x10` is not negative) it runs that tree in a frame named `<static_field_init>` with the class as frame class (`FUN_00420ff0(0)`), pops the value it left (`FUN_0041f7d0`) and assigns it with `FUN_0041acb0(field, value, 0x23)`, the same helper as the `08` suboperation 35 assignment.
- Because `+0x1d0` is set before the static initializers run, code that runs during a class's static initialization, and re-enters the class, sees the class as initialized with only the fields defined so far. A static field that is not yet defined is not found.
- Ancestors are initialized before the class, but `+0x1d0` of the class is set only after its parents finish. If a parent's static initializer initializes the subclass, the subclass initializer runs once inside it, then the outer call continues and runs the subclass's static initializers a second time.
- `FUN_00403e80` is called on first use from: instance creation (`FUN_00404e20`, `FUN_00404e60` and `FUN_00407da0`), method record lookup by index (`FUN_00404d40`, reached from the method cache `FUN_00404910`, so any first call including a static call), field lookup by name (`FUN_00404680`), static variable access (`FUN_00404800`), `isInheritedFrom` (`FUN_00404500`), method index (`FUN_00404880`) and `FUN_00404e00`.
- `FUN_00410890` turns `Lname;` into `name`, looks the class up in the registered table (`FUN_00411610`), then loads it from the package files (`FUN_00410630` with `FUN_0041d290` and `FUN_00410430`). When both fail it logs `JVM.getClass: class <name> not found in file <f> at line <n>` and returns null.
- Failure behaviour: every one of these errors goes through `FUN_005513b0`, which calls `FUN_00558510(10000, message)`. That appends the message to the `error_log` file and, because the code is 10000, neither shows a message box nor exits. Stock keeps running after a failed class lookup or a failed initializer, with the class marked initialized and the field unassigned.
- Ending an initializer tree is operation `10`: the caller of `FUN_00420ff0` pops the value the tree left.

**What the port does.**
- A script names a class in four places: `new X` (operation `07` suboperation 30), array allocation (suboperation 31), a qualified static field (`11 02 19 <class> 1b`), and a call with an explicit class (`24 11 02 19 <class> 1a`). A static field read that finds no state for the field's class also counts. When the class is not in the registry the VM was given, or has no static state, the VM calls `VmArena::ensureClass(name)`. The host (`ClassRuntime::ensure`) loads the class and its parents and interfaces, then runs `vmInitializeClass`, which adds the classes to the registry and the state to the static field table. A string literal does the same for `java.lang.String`. Without a hook nothing changes and the old errors apply.
- `vmInitializeClass` follows the stock order: parents, then interfaces, then the class. It registers the class's static state before it runs any initializer, defines each field before its tree, runs trees in declaration order and assigns the result. Literal trees take the old direct path. Any other tree runs as code in the bounded VM with the class as frame class, through the same `execute` as method trees, with the arena's native table (`VmArena::natives`, filled from `vmExecute`). It needs an arena. Without one a code tree is still rejected with `unsupported static initializer tree N`. The new operation `10` returns the top of the stack.
- A static field location now names its class and the state table instead of pointing into the table, because initializing a class while a tree runs appends to the table.

**Differences from stock, kept loud on purpose.**
- A failed static initializer returns an error from the operation that needed the class. The states that call registered are removed, the failing class is recorded in `VmArena::failedInitializations`, and every later use fails with `initialization failed earlier and is not retried`. Stock logs and goes on with partial state. States that other, completed calls registered stay. Objects and interned strings made before the failure stay in the arena.
- If an ancestor's static initializer initializes a subclass, the port returns `reentrant class initialization` where stock would run the subclass initializers twice.
- Initialization nests at most 32 calls deep (`class initialization nested too deeply`). Stock has no such limit that was read.
- Initializer assignments accept supported numeric conversions and references to the declared class, an ancestor or an implemented interface. Array descriptors must match. Stock's complete conversion rules remain unread.
- Instance field initializer trees can run code. They run synchronously during object allocation and cannot be interrupted by a scheduler slice.
- Array allocation initializes the element class. Stock initializes the array class in `FUN_00407da0`. Whether it also initializes the element class was not read.
- Failure inside the hook is reported with the loader's reason. Stock logs `JVM.getClass: class ... not found` and continues with null.
- The class search path is the port's package table, not a reading of `FUN_0041d290`.

## Operations added for `new Vector()`

Needed by `java.render.Frontend.inputQueue`. Read: the installed `Vector.class` trees 0 to 2 (dump with the loader), `FUN_00420ff0` for `401c` (see the table above).
- `1c` with a name operand pops an object and pushes the location of its field with that name (`FUN_00408800`'s illegal-field error is the failure case). Supported: instance fields of an arena object, unique name. Errors: null receiver, unowned receiver, missing or ambiguous field.
- A constructor call with two integer arguments selects `(II)`. The installed `Vector.<init>(I)` calls `Vector.<init>(II)` this way, and the callee's local 1 is the second argument, as with the other multi-argument constructors. Overload conversions are still not modelled.

## Short-circuit tests, bitwise suboperations, instance field initializers, constructor overloads

Read from live Ghidra (stock MD5 `e686099e7233bb7d7c461b1b35c5064c`) and the installed `Frontend.class` (`tools/tufa_dump.py` prints any class's trees with constants resolved). The probe needed them in `Frontend.setFonts` and `LoadingScreen`.

**Operations `04` and `05` (`FUN_00420ff0`, switch cases 4 and 5).** Both take the top value, convert it in place with `FUN_00417840` and leave it on the stack. The conversion makes a float 1 if it is not zero and 0 otherwise, makes a non-null reference 1, and keeps an integer as it is; a null reference keeps its type and a zero payload. Then `FUN_0041bd90` reads the payload as the truth value. Operation `04` skips the next node when the value is truthy and operation `05` skips it when the value is falsy. The compiler emits `a || b` as `a 05 14 N b 07 1`, and `a && b` as `a 04 14 N b 07 5`: the node after the test is the forward jump over the right operand and the combining node. When the left value decides, the jump is taken and the left value is the result. Otherwise the right operand runs and `07 1` or `07 5` joins both. So `04` is and, `05` is or. A first reading that took them the other way round was wrong; the jump node resolved it. Evidence: decompile of the cases, `FUN_00417840`, the `Frontend.setFonts` tree (tree 12, `Config.video_y >= 1024 || ...`). Static evidence.

**`07` suboperations 1, 3 and 5.** Headless `FUN_00420ff0` dispatches suboperation 1 to `FUN_0041bfb0`, which performs logical OR after truth conversion. Suboperation 3 calls `FUN_0041c290` for bitwise OR, and 5 calls `FUN_0041c1a0` for bitwise AND. The port accepts integer operands for these three operations. This corrects the earlier claim that suboperation 1 is bitwise OR. Tests are in `tests/vm_test.cpp`. Suboperation 4 calls `FUN_0041c380`, which XORs integer payloads; the focused VM test checks 6 XOR 3 = 5. Suboperation 6 remains unread.

**Instance field initializers that run code.** `FUN_00407da0`, for each class from the oldest ancestor to the concrete class and each instance field in declaration order: defines the field (`FUN_004038c0`, "member redefinition" on a clash), and when the record's tree index (`+0x10`) is not negative runs that tree in a frame named `<instance_field_init>`, with the declaring class as the frame class and the new instance as the receiver, pops the value and assigns it with `FUN_0041acb0` (`0x23`). The port now does the same for any initializer tree that is not one of the literal forms: the object is put in the arena first (code can name it), the tree runs through `execute` with the object as local 0, and the value is converted and checked against the field type as for static fields. A failure names `Class.field`; the half-built object is removed again when nothing was allocated after it. Tests: `tests/vm_class_init_test.cpp` (initializer allocating an object, one that returns `this`, ancestor fields first, a failing tree, a value of the wrong type). Not read: the frame's receiver as stock sets it versus local 0, and the assignment conversions of `FUN_0041acb0` beyond what static fields already do.

**Constructor choice by assignability.** The port accepts up to 16 integer, float, null and object arguments, including mixed arguments. It prefers candidates with more exact argument types. On a tie, a candidate whose parameter is a strict subclass of the other's parameter wins (stock `FUN_00405920` resolves the `show` overloads this way); genuinely incomparable ties are rejected as ambiguous. Reference parameters accept a class, ancestor or interface, and integer arguments can be converted to float. The callee receives arguments in reverse order after the receiver. Stock builds signatures in `FUN_0041dcb0`; its full conversion ranking in `FUN_00405920` and `FUN_0041ddc0` is not established by this implementation. Tests in `tests/vm_class_init_test.cpp` include mixed and four-argument constructors; `tests/vm_eval_name_test.cpp` covers the subclass tie.

## `Object.toString()Ljava.lang.String;` (`0047c870`)

Disassembled with `tools/ghidra_run.sh DecompileAt.java` (the function is a gap in the live window). `FUN_0045d910` parses the receiver. `FUN_00404ea0` takes the class (`instance+0xc`) and returns `**(class+8)`, the type descriptor `"L<class>;"` that `FUN_00403a90` builds when it registers the class (`DAT_00607514` is `L`, `DAT_00607518` is `;`). The result is `sprintf(buf, "%s<%0x>", descriptor, ?)` made into a String by `FUN_004174a0`. The call passes one variadic argument for two conversions, so `%0x` prints the next stack slot, which is the caller's saved ESI register (`PUSH ESI` at `0047c876`). The number is not defined by the script. The port prints the object's arena position, so the shape is faithful and the number is a stand-in. The handler is `objectToString` in `src/natives/lang_object.cpp`, tested in `tests/native_object_test.cpp`. The only installed use is `Thread(Runnable)` naming a thread. Row stays `unread`.

## Script threads

`java.lang.Thread` uses cooperative VM threads rather than OS threads. Headless decompilation checked the stock executable MD5 `e686099e7233bb7d7c461b1b35c5064c`. `init` at `0047c510` creates a record and stores it in the script object's `ptr`. `start` at `0047c570` clears flag `0x2`, resolves `target.run` and returns without executing it. `stop` at `0047c600` stops the record and clears `ptr`. `sleep` at `0047c650` calls `FUN_0041f630`, which sets flag `0x8` and a float wake time. The decompiled `isAlive` at `0047c730` returns zero. The new native rows remain `unread` in `docs/natives.tsv`.

`VmExecution` stores frames with a tree, program counter, locals and value stack. Script calls push frames and `vmResume` can stop between dispatches. A frame remains at a stable address while locations refer to its locals. `vmExecute` runs an implicit execution to completion. Nested native callbacks use the active frame stack, but callbacks and class or instance initializers run synchronously and may exceed a slice. They need continuations before arbitrary script execution can be interrupted at every node.

`VmScheduler` owns thread records and a clock supplied by the host. It starts a target's script `run`, cycles through runnable records, skips `0x82` and `0x44`, wakes a sleeper only after its wake time, and removes stopped records. A stop of the currently executing thread or sleep requests a yield. Each scheduled thread has its own random state. Tests in `tests/vm_thread_test.cpp` cover interleaving, nested frame resumption, sleep ordering, stop and the registered handlers.

Stock `FUN_00416940` uses `fVar6 * local_8` as a time slice and `FUN_005516c0` as its performance-counter clock. The relation between this time slice and `FUN_00420ff0` dispatch remains unread. The port uses an equal node budget per eligible thread, not the stock priority-weighted time share. It does not implement suspend, resume, priority or daemon natives. Error handling and lifetime are bounded host behavior, not verified stock parity.

For `07` suboperation 18, headless `FUN_00420ff0` calls `FUN_0041b970` for division. The port supports finite ordinary float and integer division and rejects zero divisors, integer overflow, subnormal results and non-nearest rounding. Stock logs a division by zero and then performs the operation. `tests/vm_thread_test.cpp` covers ordinary results and the rejected zero divisor.

## Receiver wait lists

The stock handlers at `0047c920`, `0047c950` and `0047c970` call `FUN_00408b20`, `FUN_00408bc0` and `FUN_00408ce0`. `FUN_00408b20` calls `FUN_0041f650` to set thread flag `0x10` and appends the thread to the receiver's wait array. `notify` pops the newest non-null waiter and `notifyAll` drains the array. `FUN_0041f6f0` clears the waiting flag and returns the thread to the runnable list. These functions do not check ownership of a Java monitor or release a monitor lock. The registered signatures have no timeout argument.

The host scheduler parks the active execution and skips waiting threads until a matching notification. It wakes the newest waiter first and drains all on `notifyAll`. Stopped threads cannot be woken. An invocation of `wait` outside a scheduled execution fails explicitly because the host has no current thread to park. A wait inside a synchronous native callback or initializer also fails explicitly because that nested execution is not resumable. The host does not reproduce all stock thread-list bookkeeping, object destruction, or error logging. `tests/vm_thread_test.cpp` covers interleaving, no spontaneous wake, a different receiver, newest-first notify and notifyAll. The native rows remain `unread`.

## File existence and integer arrays

Stock `File.exists` at `00486170` calls `FUN_0054c170`, which converts forward slashes to backslashes, checks an optional archive through `FUN_005542d0`, and otherwise calls `FUN_005586b0`. The latter attempts `CreateFileA` for reading and returns one only if it opens. The port supports bounded paths under the staged save root, normalizes both slash forms, rejects traversal and reference-root escapes, and checks a readable regular file. Archives, Windows case folding, sharing modes and other paths remain unsupported. `tests/native_file_test.cpp` covers existing, missing and rejected paths. The native row remains `unread`.

The VM allocates `[I` arrays with zero-valued elements and supports integer indexed reads and writes, alongside its existing reference arrays. `FUN_00404e60` calls `FUN_00407da0`, which allocates each stock array element with a zero payload. Compound assignment `08 24` copies the target and invokes `FUN_0041b860` before assigning the result in `FUN_0041acb0`. The port supports the same bounded integer and finite float multiplication used by its ordinary multiply operation. `tests/vm_thread_test.cpp` exercises allocation, indexed write, read and compound multiplication. Array class initialization and exceptional floating-point behavior remain incomplete.

## Chained qualified field access (stock evalName, `FUN_00420d20`)

Read from headless decompilation of the dispatcher's `0x11` arm
(`local/census/DecompileAt-00420ff0.log`, stock MD5 checked). An `11 <n>` node
whose following `n` nodes are `01` locals and `1b` field references walks a
receiver chain: in the port, a chain that starts with `1b` resolves that first
field against local 0; stock's exact base selection for that form is inferred
from the tree shape (stock chains read `this`-relative fields) and is still to
be verified against `FUN_00420d20`'s disassembly. Each `1b` step reads the named
field of the previous step's object, resolving the name on each receiver's
runtime class (`FUN_00406bb0` field id by name). The final step pushes a field
location, not a copy, when the chain ends on a `1b`, so a following `08 35`
store writes through it (`LoadingDialog` tree 1 nodes 11 through 13 store
`this` into `osd.globalHandler` this way). A chain used as a value pushes the
last read value. Stock's per-step helpers take only `I`, `F` and reference
payloads; the port rejects missing fields, ambiguous names and null receivers
with named errors. `tests/vm_eval_name_test.cpp` covers the store, the value
read, and the runtime-class resolution of a later step.

Every class is assignable to `java.lang.Object` (`classAssignableTo`), because
the port models no `java.lang.Object` class for the parent walk to reach.
Object-typed stores from root classes (the `globalHandler` case) pass; arrays
keep their own descriptor checks.

## Float arrays (`[F`) and value boxes

Stock's stack and field tables hold value boxes: `FUN_004087e0` returns a
field's box (the raw table word, not a copy), and the assignment helper
`FUN_0041acb0` case `0x23` writes the source payload into the target box
payload (`FUN_0041ab60`, switch on the target's type char `I`/`F`/`L`/`[`).
Increment `07 23` writes through the original box, while logical not `07 21`
operates on a copy and leaves the original untouched. The port models a box as
a stack location (`FieldLocation`, `LocalLocation`, `ArrayLocation`) and uses
`boxedSlot` for operations that write through it.

For increment, `07 23` leaves the old value on the stack and then increments
the box: it is postfix. `08 23` increments and discards the result. The stock
dispatcher at `00420ff0` copies the value before calling `0041b2d0` for the
`07` case. This corrects the earlier prefix interpretation. The regression in
`tests/game_type_native_test.cpp` executes the installed `Vector.addElement`
and checks its first element at index zero.

Array allocation `07 31` covers `[I`, `[F` and `[L<class>;`. Stock allocates
each element with a zero payload (`FUN_00404e60` through `FUN_00407da0`), so a
float array fills with `0.0f`. An integer stored into a `[F` element converts
to float, matching the assignment conversions. `java.io.Input`'s static
initializer (`player_input = new float[MAXPLAYERS]`, tree at source line 211)
is the real script that needs this. Tests: `tests/vm_test.cpp` array cases.

Logical `07/1` and `07/2` convert integer, float, and reference operands to
boolean before OR/AND, as in `0041bfb0`, `0041bea0`, and `00417840`. A null
reference converts to integer zero; bitwise operations remain integer-only.
For an implicit instance method reference, `004208e0` resolves against the
actual receiver class when it differs from the declaring class. This allows
`OptionsDialog.show` to call `MainMenuDialog.addCustomGroups` and initialize
its buttons. Tests exercise null and non-null logical operands and an inherited
method calling a derived override.
