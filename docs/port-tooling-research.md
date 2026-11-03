# Port tooling research

Scope: native arm64 reimplementation, not binary matching or a DLL wrapper. This is a research/design document, not a record that any proposed config, hook, or script was installed or tested. Local citations are relative to the repository; upstream links are primary sources. Discovery lists and independently reconstructed ports are pointers or comparison implementations, not evidence of stock behavior. Binary-identification observations below were collected read-only; reported build/guard telemetry was not rerun.

## 1. Binary identity and Ghidra / REA unification

### Binary identity

The two bytes are **not PE headers or a section-table field**. Read-only parsing and disassembly give:

| Property | Value |
|---|---:|
| DOS `e_lfanew` | `0x120` |
| COFF header | `0x124` |
| Optional header | `0x138`, size `0xe0` |
| Section table | `0x218` |
| Image base | `0x400000` |
| `.text` raw offset / RVA | `0x1000` / `0x1000` |
| Changed file offsets | `0x150a–0x150b` |
| Changed VAs | `0x40150a–0x40150b` |

At `0x401505`, the code compares `eax` with 5. The instruction at `0x401508` is a six-byte `jne rel32`:

```text
.bak:       0f 85 f0 01 00 00    jne 0x4016fe
extracted:  0f 85 12 02 00 00    jne 0x401720
```

The displacement is relative to the next instruction, `0x40150e`; the bytes change the branch destination by `0x22`. **[INFERENCE]** This may change initialization/control flow; the purpose and downstream runtime effect were not established. It is unsafe to claim the executables have identical behavior or to transfer findings at this branch without qualifying binary identity. Other addresses are not shifted by this same-size edit, but unchanged addresses alone do not establish equivalent control flow.

### Existing project and REA boundaries

- `tools/ghidra_run.sh:14–21,54–71` pins the `.bak` MD5, imports it once, executes indirect/gap discovery once, then processes the saved project with `-noanalysis`. `DefineGapFunctions.java` is deliberately not idempotent (`tools/ghidra_run.sh:6–10`).
- `DefineIndirectFunctions.java:62–74` seeds native handler **addresses**, not their names. `DecompileAll.java:22–29` subsequently emits Ghidra’s function name and address.
- `docs/natives.tsv:1–20` has six fields, including handler address and VM descriptor. All 360 rows cover **357 unique addresses**; three addresses each have two rows. A sync must group aliases rather than assume one native per handler.
- `docs/native_evidence.json:1–16` records stock MD5 and human evidence, not native machine signatures. `src/natives/registry.hpp:10–20` describes the *port’s* C++ handler ABI, not the stock handler ABI. Do not translate JVM descriptors or that C++ signature into Ghidra function prototypes.
- REA 6.8.0’s live `binary_session` reports that its Ghidra program/project is **ephemeral**, its annotations are discarded on close, and metadata edits invalidate immutable snapshots. The [`open_binary` contract](https://github.com/morluto/rea/issues/1048) accepts a matching REA *analysis snapshot*, not a caller-selected `.gpr`/`.gzf` project. Upstream issue #1048 explicitly distinguishes replayed snapshot results from a reusable Ghidra database. Thus **no current supported two-way project bridge** is established; do not promise that opening a snapshot makes REA see this project’s added function bodies or struct types.

Recommended immediately usable workflow: keep the persistent `.bak` Ghidra project authoritative; run the proposed symbol script with `--save`, then `tools/ghidra_run.sh DecompileAll.java local/census/all.c`. Treat REA as a separate, independently imported analysis and always record its executable digest and provider profile. To genuinely unify them would require an upstream REA provider feature: copy the caller-selected project to an owned temporary directory; open the selected program with `-process -readOnly -noanalysis`; check its original executable SHA-256 and profile; preserve project provenance; and reject locked, ambiguous or mismatched programs. This is a **proposal**, not a capability observed in REA.

### Implementable Ghidra script proposal

The following deliberately syncs native *names and aliases* and the **verified `State` layout only**. It does not pretend to parse C++20 or to import all structs: `Shape` and `BodyProto` are semantic port structures containing vectors, ownership and inline fields, whereas comments describe different stock offsets and pointers (`src/physics_body.hpp:88–109,123–133`). Blindly importing their host layout would be incorrect. `State` is explicitly size/offset asserted at `src/physics_body.hpp:145–169`.

Save as `tools/ghidra/ApplyProjectSymbols.java` as part of the proposed future implementation:

```java
import java.io.BufferedReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.CategoryPath;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FloatDataType;
import ghidra.program.model.data.StructureDataType;
import ghidra.program.model.data.UnsignedIntegerDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplyProjectSymbols extends GhidraScript {
    private static final String STOCK_MD5 =
        "e686099e7233bb7d7c461b1b35c5064c";
    private static final String BEGIN = "[sl_port native aliases]";
    private static final String END = "[/sl_port native aliases]";

    private static final class NativeRow {
        final String className, method, descriptor, address;
        NativeRow(String[] c) {
            className = c[0];
            method = c[1];
            descriptor = c[2];
            address = c[3];
        }
        String display() {
            return className + "." + method + descriptor;
        }
    }

    private static String ident(String s) {
        return s.replaceAll("[^A-Za-z0-9_]", "_");
    }

    private static DataType floats(DataTypeManager dtm, int count) {
        return new ArrayDataType(FloatDataType.dataType, count, 4, dtm);
    }

    private static void field(StructureDataType s, int offset, DataType type,
                              String name) {
        s.replaceAtOffset(offset, type, type.getLength(), name, null);
    }

    private void installState() throws Exception {
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        CategoryPath category = new CategoryPath("/sl_port");
        StructureDataType state =
            new StructureDataType(category, "PhysicsState", 0xd4, dtm);
        field(state, 0x00, floats(dtm, 3), "pos");
        field(state, 0x0c, floats(dtm, 3), "vel");
        field(state, 0x18, floats(dtm, 3), "shift");
        field(state, 0x24, FloatDataType.dataType, "time");
        field(state, 0x28, floats(dtm, 3), "force");
        field(state, 0x34, floats(dtm, 2), "unknown34");
        field(state, 0x3c, UnsignedIntegerDataType.dataType, "stepFlags");
        field(state, 0x40, floats(dtm, 12), "rot");
        field(state, 0x70, floats(dtm, 3), "omega");
        field(state, 0x7c, floats(dtm, 3), "angMom");
        field(state, 0x88, floats(dtm, 3), "torque");
        field(state, 0x94, floats(dtm, 4), "quat");
        field(state, 0xa4, floats(dtm, 12), "invInertiaWorld");

        DataType existing = dtm.getDataType(category, "PhysicsState");
        if (existing != null && !existing.isEquivalent(state)) {
            throw new IllegalStateException(
                "conflicting /sl_port/PhysicsState; resolve manually");
        }
        if (existing == null) {
            dtm.addDataType(state, DataTypeConflictHandler.DEFAULT_HANDLER);
        }
    }

    private static String mergeComment(String prior, List<NativeRow> rows) {
        String base = prior == null ? "" : prior;
        int start = base.indexOf(BEGIN);
        int end = base.indexOf(END);
        if ((start < 0) != (end < 0) || (start >= 0 && end < start)) {
            throw new IllegalStateException("malformed existing alias block");
        }
        if (start >= 0) {
            base = base.substring(0, start)
                + base.substring(end + END.length());
        }
        base = base.trim();
        StringBuilder b = new StringBuilder(base);
        if (!base.isEmpty()) b.append("\n\n");
        b.append(BEGIN);
        for (NativeRow row : rows) b.append("\n").append(row.display());
        b.append("\n").append(END);
        return b.toString();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1)
            throw new IllegalArgumentException("expected path to natives.tsv");
        if (!STOCK_MD5.equalsIgnoreCase(currentProgram.getExecutableMD5()))
            throw new IllegalStateException("wrong executable MD5");

        Map<String, List<NativeRow>> byAddress = new LinkedHashMap<>();
        try (BufferedReader r = Files.newBufferedReader(
                Path.of(args[0]), StandardCharsets.UTF_8)) {
            String header = r.readLine();
            if (!"class\tmethod\tdescriptor\thandler\tcall_site\tstatus"
                    .equals(header))
                throw new IllegalArgumentException("unexpected TSV header");
            String line;
            while ((line = r.readLine()) != null) {
                if (line.isEmpty()) continue;
                String[] c = line.split("\t", -1);
                if (c.length != 6 || !c[3].matches("[0-9a-fA-F]{8}"))
                    throw new IllegalArgumentException("bad TSV row: " + line);
                NativeRow row = new NativeRow(c);
                byAddress.computeIfAbsent(c[3].toLowerCase(),
                    ignored -> new ArrayList<>()).add(row);
            }
        }

        installState();
        int renamed = 0, preserved = 0;
        for (Map.Entry<String, List<NativeRow>> entry : byAddress.entrySet()) {
            monitor.checkCancelled();
            Address address = toAddr(entry.getKey());
            Function f = getFunctionAt(address);
            if (f == null)
                throw new IllegalStateException("no function entry at " + address);
            NativeRow first = entry.getValue().get(0);
            String name = "native_" + ident(first.className) + "_"
                + ident(first.method) + "_" + entry.getKey();
            if (f.getSymbol().getSource() == SourceType.DEFAULT) {
                f.setName(name, SourceType.USER_DEFINED);
                renamed++;
            } else {
                preserved++;
            }
            f.setComment(mergeComment(f.getComment(), entry.getValue()));
        }
        println("symbols renamed=" + renamed + " preserved=" + preserved
            + " entries=" + byAddress.size());
        println("APPLY_PROJECT_SYMBOLS_OK");
    }
}
```

**Qualifications and integration details:**

1. API design checked against Ghidra’s [`StructureDataType`](https://ghidra.re/ghidra_docs/api/ghidra/program/model/data/StructureDataType.html) and [`Function`](https://ghidra.re/ghidra_docs/api/ghidra/program/model/listing/Function.html) API pages; **the script was not compiled or executed**. Add an `APPLY_PROJECT_SYMBOLS_OK` case to the existing marker check (`tools/ghidra_run.sh:37–49`) before relying on shell exit status; headless can return zero after a script exception.
2. Run `tools/ghidra_run.sh --save ApplyProjectSymbols.java docs/natives.tsv`, then regenerate `all.c`. Back up the project before first use. An existing non-default function name is preserved; all TSV aliases are placed in a bounded, idempotently replaced comment block.
3. The script creates a datatype but does **not** apply it to guessed function parameters. Apply only at sites with independently established stock calling conventions and parameter storage. Importing a datatype alone does not make all decompilation use its field names.
4. For sustainable automated layout sync, generate a narrow, fixed-width **stock-layout** C header or machine-readable offset manifest from reviewed evidence, not the host C++ object definitions. Parse the C subset with Ghidra’s [`CParser`](https://ghidra.re/ghidra_docs/api/ghidra/app/util/cparser/C/CParser.html) against the PE32 program’s datatype manager. Compare Ghidra field offsets and lengths against `sizeof`/`offsetof` assertions and independently observed stock evidence; a shared generated manifest verifies consistency, **not independent correctness**. Synchronizing `native_evidence.json` would require an additional JSON import step; this script does not claim to do so.

## 2. Comparative architecture matrix

### Comparative architecture: source-backed findings

Use **“not established by inspected source”**, rather than “absent,” for unresearched matrix cells.

| Project | VM / native dispatch | 32→64-bit compatibility and verification | Separation / transferable practice |
|---|---|---|---|
| **REGoth** | Registers Daedalus host functions by symbolic name, e.g. `Mdl_SetVisual`, whose callback pops VM arguments. [Externals.cpp](https://github.com/REGoth-project/REGoth/blob/b6d91b8107bfeae1431dfd11af5a20545b3e7643/src/logic/scriptExternals/Externals.cpp#L19-L110). | Its inspected `PtrHandle<T>` wraps a **native pointer**, not a proven 32-bit address or offset. Do not present it as a `Ptr32` precedent. [Handle.h](https://github.com/REGoth-project/REGoth/blob/b6d91b8107bfeae1431dfd11af5a20545b3e7643/src/handle/Handle.h). | Separate `logic/scriptExternals` is a useful organization precedent for TUFA native handlers; its name-keyed registry differs from this project’s stock-VA keys. |
| **OpenMW** | Installs core interpreter opcodes and domain-specific cell, UI, sound, AI and other extensions separately. [extensions.cpp](https://github.com/OpenMW/openmw/blob/09d08d51003da24e825b61cfb08e17bccffb9b49/apps/openmw/mwscript/extensions.cpp#L20-L41). | Original executable struct parity is not established by this source. | Good precedent for separating interpreter mechanics from host-domain opcode implementation, **not evidence that OpenMW executes TUFA or original x86 code**. |
| **ScummVM** | SCUMM v5 explicitly maps byte opcodes to C++ handler methods. [script_v5.cpp](https://github.com/scummvm/scummvm/blob/master/engines/scumm/script_v5.cpp#L38-L100). | Original x86 layout parity not established. | Useful precedent for version-specific opcode tables; its opcode dispatch is not identical to sl-remastered’s native-VA dispatch. |
| **openblack** | LHVM maintains instructions, tasks, stack and an opcode handler table; native-function table and enter/exit callbacks are explicit. [LHVM.h](https://github.com/openblack/openblack/blob/bd8827e683315ae5397893cc402e33548502d0a0/components/ScriptLibrary/include/LHVM.h#L25-L125). | VM uses explicit `uint32_t` object/task IDs, but this does **not** establish stock-memory pointer parity. | Strongest adjacent C++ tooling example: [`lhvmtool`](https://github.com/openblack/openblack/blob/bd8827e683315ae5397893cc402e33548502d0a0/apps/lhvmtool/lhvmtool.cpp#L20-L140) reports instructions, scripts and runtime state. The project also has [CMake presets](https://github.com/openblack/openblack/blob/bd8827e683315ae5397893cc402e33548502d0a0/CMakePresets.json…
| **OpenRCT2** | Inspected replay files do not establish original-VM/native dispatch. | Replay commands carry tick and sequence index; normal recording checksums entity state every tick and playback checks state. [ReplayManager.cpp](https://github.com/OpenRCT2/OpenRCT2/blob/20fbc0936d060dd927d7127d785c8090c2104a24/src/openrct2/ReplayManager.cpp#L50-L105), [update](https://github.com/OpenRCT2/OpenRCT2/blob/20fbc0936d060dd927d7127d785c8090c2104a24/src/openrct2/ReplayManager.cpp#L155-L205). Snapshot serialization records concrete entity sizes and checks them on load. [GameStateSnapshots.cpp](https://github.com/OpenRCT2/OpenRCT2/blob/20fbc0936d060dd927d7127d785c8090c2104a24/src/openrct2/GameStateSnapshots.cpp#L25-L105). | These sources show **OpenRCT2 record/pla…
| **DevilutionX** | VM dispatch not established by inspected source. | Save loader reads through a cursor, `memcpy`s fixed-size values and swaps LE/BE explicitly. [loadsave.cpp](https://github.com/diasurgical/DevilutionX/blob/b325996a89c59eb0a680406b7ede3747011b9e08/Source/loadsave.cpp#L52-L160). This is evidence for **serialized byte handling**, not identical native 32-/64-bit struct layouts or original-vs-port per-tick replay. | Adopt explicit disk widths/endian conversion and fixture checks instead of casting file bytes to native structs. |
| **dethrace** | VM native dispatch not established. | Its vehicle source annotates original globals with CARM95 addresses. [car.c](https://github.com/dethrace-labs/dethrace/blob/166dd93cf3dd985d027095c29d236e38bd9ae5b8/src/DETHRACE/common/car.c#L39-L78). This alone does not prove cross-architecture physics parity. | [Code layout](https://github.com/dethrace-labs/dethrace/blob/166dd93cf3dd985d027095c29d236e38bd9ae5b8/docs/CODE_LAYOUT.md) separates original-like game logic from platform `harness`; [porting guidance](https://github.com/dethrace-labs/dethrace/blob/166dd93cf3dd985d027095c29d236e38bd9ae5b8/docs/PORTING.md) uses isolated platform callbacks. Useful physics reference, but Carmageddon’s collision algorithm is not evidence for SLRR’s. |
| **OpenSAGE** | C# ActionScript VM exposes engine callbacks and executes decoded instructions. [VM.cs](https://github.com/OpenSAGE/OpenSAGE/blob/588ac477367a0022adf29f20a084e8873014e6ce/src/OpenSage.Game/Gui/Apt/ActionScript/VM.cs#L10-L135). This is an APT UI VM, not SLRR’s TUFA. | Stock native-struct parity not established. Its installed-game BIG tests assert known entry count, name and size. [BigArchiveTests.cs](https://github.com/OpenSAGE/OpenSAGE/blob/588ac477367a0022adf29f20a084e8873014e6ce/src/OpenSage.Game.Tests/Data/Big/BigArchiveTests.cs). | Good example of separating parsers and real-asset corpus tests; original files are test prerequisites. |
| **Thyme** | Inspected README does not establish VM details. | README says its current implementation calls the original binary for unimplemented functions and that MinGW ABI differences prevent generated DLLs working correctly with the original. [README](https://github.com/TheAssemblyArmada/Thyme/blob/4b8e98779cecb1cc3d975e3971c05eed0562605b/README.md#L3-L33). | **Not a completed native cross-architecture port**: its architecture is explicitly unlike sl-remastered. Useful ABI warning, not a direct structural template. |

### SLRR ecosystem and discovery

- [EliasFD3S/slrr-reverse](https://github.com/EliasFD3S/slrr-reverse/blob/5c42f3b139aef4c7b9e0b6420c5704e22c0e6cd7/engine/Core/Jvm/tufa.cpp) supplies TUFA-related reconstruction; [RPAK implementation](https://github.com/EliasFD3S/slrr-reverse/blob/5c42f3b139aef4c7b9e0b6420c5704e22c0e6cd7/engine/Core/Platform/rpak.cpp) is another comparison point. Treat as independently authored hypotheses, not original source or an oracle.
- [nonamesex/io_invo_scx](https://github.com/nonamesex/io_invo_scx/blob/7bcc34e51a3cb40369cc28db2950ab27f9587078/README.md) documents SCX v3/v4 and SCY v3 import, incomplete material/bone support, and includes an [ImHex pattern](https://github.com/nonamesex/io_invo_scx/blob/7bcc34e51a3cb40369cc28db2950ab27f9587078/imhex/scx.hexpat). It is not evidence for SCX v5/v6.
- [SLRR_rev sources](https://github.com/nedobylskiy/SLRR_rev/blob/main/docs/sources.md) identifies both that importer and [Connor-Moyle’s importer](https://github.com/Connor-Moyle/blender-slrr-scx-importer), and distinguishes external reconstructions from original evidence. Its review flags incomplete/stub paths in slrr-reverse; independently verify any exact behavior before adopting it.
- Discovery lists inspected: [awesome-game-remakes](https://github.com/radek-sprta/awesome-game-remakes) and [game decompilations and reimplementations](https://github.com/alexgaoth/awesome-reverse-engineered/blob/main/categories/game-decompilations-reimplementations.md). These are discovery aids, **not primary evidence for technical claims**. The former points to [OSGameClones](https://osgameclones.com/).
- The specifically named `SLRR-JVM-compiler` repository was not positively identified in this research; do not attribute capabilities to it.



## 3. Missing configs and build / reference hygiene

### Section 3 research: configs, CMake, and reference guard

### Local evidence and constraints

- `CMakeLists.txt:1–25` requires CMake 3.20, sets arm64 and C++20, disables FP contraction, defines overlapping `CAR_FACTORY_SOURCES` and `NATIVE_REGISTRY_SOURCES`, and compiles the latter into `sl_data`.
- `CMakeLists.txt:58–61,148–151,234–255` recompiles `vm.cpp` and the native source list into multiple sanitizer executables. `CMakeLists.txt:196–225` adds car sources directly to tests that also link `sl_data`. These are concrete duplication candidates.
- **Sanitizer caveat:** `native_registry_test` adds sanitizer flags to its executable but links prebuilt `sl_data` (`CMakeLists.txt:71–75`); this does not retroactively instrument the library. A shared sanitizer library must itself compile with sanitizer flags.
- A monolithic test binary is **not** a drop-in consolidation: tests contain independent `main` functions, e.g. `tests/car_physics_test.cpp:10`, `tests/car_config_test.cpp:394`; process isolation, environment, working directory, and CTest names must be retained. Share compiled libraries first; only migrate individual test entry points after handling global-state and fixture differences.
- Measurement of all 236 `src/**/*.cpp` and `src/**/*.hpp` files: 58,511 lines; 2,313 exceed 80 columns, 885 exceed 100, 194 exceed 120; maximum 445. Most common nonzero indentation widths: 2 spaces (14,644 lines), 4 (9,845), 6 (6,692). Styles are mixed: `src/physics_core_pair.cpp:1–16` uses 2-space indentation and attached braces; `src/car_physics.cpp:1–5,32–34` demonstrates non-sorted includes and `std::string&`; `src/car_physics.cpp:57–59` demonstrates both `Type*` and `auto&`. **No exact formatter reproduces all existing files.** Preserve include ordering and existing long lines; use changed-line formatting rather than a bulk rewrite. The choice `ColumnLimit: 0` is a conservative policy, not evidence of an existing fixed limit.
- LLVM documents `.clang-format` as YAML with inherited base-style options and pointer alignment controls: https://clang.llvm.org/docs/ClangFormatStyleOptions.html.

### Proposed `.clang-format`

```yaml
---
Language: Cpp
BasedOnStyle: LLVM
IndentWidth: 2
TabWidth: 2
UseTab: Never
BreakBeforeBraces: Attach
ColumnLimit: 0
DerivePointerAlignment: false
PointerAlignment: Left
ReferenceAlignment: Pointer
SortIncludes: Never
AllowShortFunctionsOnASingleLine: All
AllowShortIfStatementsOnASingleLine: WithoutElse
AllowShortLoopsOnASingleLine: true
NamespaceIndentation: None
```

This is a **proposed compromise**, not an assertion of exact equivalence. In particular, some source files use `auto *root`, while others use `auto* body`. Formatter version compatibility, actual diff size, and IDE behavior remain untested.

### Proposed `.clang-tidy`

```yaml
---
Checks: >-
  -*,
  bugprone-narrowing-conversions,
  bugprone-sizeof-expression,
  performance-no-int-to-ptr
WarningsAsErrors: ''
HeaderFilterRegex: '(^|.*/)src/.*'
FormatStyle: file
CheckOptions:
  bugprone-narrowing-conversions.WarnOnIntegerNarrowingConversion: 'true'
  bugprone-narrowing-conversions.WarnOnIntegerToFloatingPointNarrowingConversion: 'true'
  bugprone-narrowing-conversions.WarnOnFloatingPointNarrowingConversion: 'true'
```

Run selectively against the generated compilation database, not as an assumed clean global gate. Explicit casts can silence the narrowing check, so it is not a substitute for validating 32-bit file layouts, pointer provenance, signedness, or value ranges. Documentation: [narrowing conversions](https://clang.llvm.org/extra/clang-tidy/checks/bugprone/narrowing-conversions.html), [suspicious sizeof](https://clang.llvm.org/extra/clang-tidy/checks/bugprone/sizeof-expression.html), [integer-to-pointer casts](https://clang.llvm.org/extra/clang-tidy/checks/performance/no-int-to-ptr.html).

### Proposed `CMakePresets.json` compatible with the project’s 3.20 minimum

```json
{
  "version": 2,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 20,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "ninja-release",
      "displayName": "Ninja release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/preset-release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
        "CMAKE_JOB_POOLS": "compile_pool=6;link_pool=2",
        "CMAKE_JOB_POOL_COMPILE": "compile_pool",
        "CMAKE_JOB_POOL_LINK": "link_pool"
      }
    },
    {
      "name": "ninja-debug",
      "inherits": "ninja-release",
      "displayName": "Ninja debug",
      "binaryDir": "${sourceDir}/build/preset-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    }
  ],
  "buildPresets": [
    { "name": "release", "configurePreset": "ninja-release" },
    { "name": "debug", "configurePreset": "ninja-debug" }
  ],
  "testPresets": [
    {
      "name": "release",
      "configurePreset": "ninja-release",
      "output": { "outputOnFailure": true }
    },
    {
      "name": "debug",
      "configurePreset": "ninja-debug",
      "output": { "outputOnFailure": true }
    }
  ]
}
```

`cmake --preset ninja-release && cmake --build --preset release && ctest --preset release`. Pool sizes are suggested starting points, not measurements. Optional machine-local cache launcher: `cmake --preset ninja-release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` or `sccache`; only set when installed. Avoid forcing a cache binary for all users.

Official sources: [presets schema/version](https://cmake.org/cmake/help/v3.20/manual/cmake-presets.7.html), [Ninja job pools](https://cmake.org/cmake/help/v3.20/prop_gbl/JOB_POOLS.html), [compiler launcher](https://cmake.org/cmake/help/v3.20/prop_tgt/LANG_COMPILER_LAUNCHER.html). The launcher is initialized **at target creation**, and Ninja pools cap concurrency rather than guaranteeing speedup.

### Concrete CMake consolidation pattern

The safest first step is to define **two variants of the existing complete library**, retaining the actual `sl_data` source list verbatim in a new variable. This is a pattern, not a complete drop-in replacement of all 707 lines; explicit migration per target remains necessary.

```cmake
# Replace:
# add_library(sl_data STATIC <the existing complete source list>)
#
# With:
set(SL_DATA_SOURCES
    # Move every existing source from the sl_data add_library invocation
    # here verbatim, including ${NATIVE_REGISTRY_SOURCES}.
)

function(add_sl_data_variant target)
  add_library(${target} STATIC ${SL_DATA_SOURCES})
  target_include_directories(${target} PUBLIC src)
  target_link_libraries(${target} PUBLIC SDL3::SDL3 JPEG::JPEG PNG::PNG)
endfunction()

add_sl_data_variant(sl_data)
add_sl_data_variant(sl_data_asan)
target_compile_options(sl_data_asan PRIVATE
  -fsanitize=address,undefined -fno-sanitize-recover=undefined)
# Consumers inherit sanitizer runtime requirements on their link step.
target_link_options(sl_data_asan INTERFACE -fsanitize=address,undefined)

# Example: replace the direct source compilation of vm_test:
# - add_executable(vm_test tests/vm_test.cpp src/vm.cpp src/tufa.cpp
# -   ${NATIVE_REGISTRY_SOURCES} src/file_state.cpp src/game_file.cpp src/sdat.cpp)
# + add_executable(vm_test tests/vm_test.cpp)
# + target_link_libraries(vm_test PRIVATE sl_data_asan)
# Keep existing sanitizer flags on vm_test itself.
```

Before applying the illustrative replacement, **confirm all current direct sources are present in `SL_DATA_SOURCES`**, plus any test-only definitions or link requirements; otherwise add them explicitly or split a library at a genuine module boundary. Do not blindly combine `CAR_FACTORY_SOURCES` and `NATIVE_REGISTRY_SOURCES`: they overlap. For each target remove a direct source only when its chosen library supplies it; avoid both sanitized and unsanitized variants in one executable, duplicate definitions, or fixture-specific mock collisions. `sl_music`/`sl_video` dependencies and platform frameworks also require separate consideration. Linking an OBJECT library directly pulls all objects, whereas a STATIC archive generally extracts needed members: choosing th…

### Reference guard: exact diagnosis and safe remediation proposal

`tools/ref_guard.py:35–48` recursively hashes files and records symlink targets under **all** `ref/` (except `.DS_Store`); `:51–56` loads the TSV baseline; `:72–92` fails on any added, removed, or changed entry. `generate --force` (`:59–68`) overwrites the entire manifest and may bless unrelated modifications, so do **not** use it as a quick fix. `tools/check.sh:2–8` stops on guard failure because `set -e` is active. `README.md:22–28` promises all of `ref/` is guarded, and `docs/continuation.md:84–86` explicitly records that authorship of the added dumps is unknown and recommends moving only with approval.

Read-only comparison of `ref/analysis` against the manifest identified these unmanifested paths:

```text
analysis/StreetLegal_Redline.bak.exe.config-reader.kuna.c
analysis/StreetLegal_Redline.bak.exe.getConfigOptions.FUN_00427bf0.ghidra.c
analysis/StreetLegal_Redline.bak.exe.ghidra-functions.txt
analysis/StreetLegal_Redline.bak.exe.kuna-functions.txt
```

**Preferred proposal:** get the owner’s approval, preserve the four outputs under a project-generated, non-reference location such as `local/census/`, then remove those four additions from `ref/analysis/` only after verifying the copied bytes; modify generators to write outside `ref/`. Do not relax the guard, add a directory-wide exclusion, or regenerate the manifest to hide the symptom. If the files are instead intentionally accepted as immutable reference inputs, explicitly review their provenance and add only their four hash/size/path entries to the existing manifest; this changes the baseline and requires approval. Neither option guarantees `tools/check.sh` is wholly green: later build/tests may have independent failures. The brief reports four failing C…

No repository files were modified, and no checks were run.

[Some lines truncated to 768 chars. Use agent://PortToolingResearch.ConfigResearch:raw:147-147 to read line 147 whole; cut lines run to line 162, each read the same way]
### Exact guard remediation, proposed only

After the owner approves relocating the four unmanifested generated files listed above, preserve each and check byte identity before removing **only** that original. The shell below is a proposed future action, not executed in this investigation; first ensure the destination does not already exist and no process is writing the sources. It never changes `ref/raw/` or regenerates the manifest. `tools/ref_guard.py:35–48,59–92`.

```sh
set -eu
mkdir -p local/census/ref-analysis-recovered
for f in \
  StreetLegal_Redline.bak.exe.config-reader.kuna.c \
  StreetLegal_Redline.bak.exe.getConfigOptions.FUN_00427bf0.ghidra.c \
  StreetLegal_Redline.bak.exe.ghidra-functions.txt \
  StreetLegal_Redline.bak.exe.kuna-functions.txt
do
  src="ref/analysis/$f"
  dst="local/census/ref-analysis-recovered/$f"
  test -f "$src" && test ! -e "$dst"
  cp -p "$src" "$dst"
  cmp -s "$src" "$dst"
  rm -- "$src"
done
python3 -I tools/ref_guard.py verify
```

Do not run a bulk formatter over the repository. Existing agent instruction changes are outside this research-only task; inspect `AGENTS.md` and `.agents/skills` before proposing a separate change rather than claiming orchestration is broken without evidence.


## 4. Standard decompilation and porting tooling to adopt



1. **TUFA inspector modeled on openblack’s LHVM tool:** report class identity, method names, instruction offsets, branch targets, native identifiers and decoding failures, preserving the source byte offset. [LHVM tool source](https://github.com/openblack/openblack/blob/bd8827e683315ae5397893cc402e33548502d0a0/apps/lhvmtool/lhvmtool.cpp#L20-L140). [INFERENCE] Reuse existing sl-remastered parser rather than importing LHVM code.
2. **Tick-indexed deterministic comparison:** record initial state, ordered inputs, random state, simulation tick and named selected fields; emit a stable per-tick digest and retain snapshots around the first mismatch. OpenRCT2’s observed precedent is its own replay checksums and snapshots, not cross-engine differential execution. [Replay source](https://github.com/OpenRCT2/OpenRCT2/blob/20fbc0936d060dd927d7127d785c8090c2104a24/src/openrct2/ReplayManager.cpp#L50-L105), [snapshots](https://github.com/OpenRCT2/OpenRCT2/blob/20fbc0936d060dd927d7127d785c8090c2104a24/src/openrct2/GameStateSnapshots.cpp#L25-L105).
3. **Asset-corpus golden checks:** for owned stock data, assert selected archive entry counts, names, sizes and decoded metadata in addition to broad census; tie fixture expectations to identified asset versions. [OpenSAGE corpus test](https://github.com/OpenSAGE/OpenSAGE/blob/588ac477367a0022adf29f20a084e8873014e6ce/src/OpenSage.Game.Tests/Data/Big/BigArchiveTests.cs). [INFERENCE] Exact expected values must come from this project’s own stock corpus.
4. **32-bit layout safety:** use explicit integer-width disk types and endian conversions, independently assert confirmed offsets/size, and treat runtime host pointers separately. [DevilutionX loader](https://github.com/diasurgical/DevilutionX/blob/b325996a89c59eb0a680406b7ede3747011b9e08/Source/loadsave.cpp#L52-L160). Neither REGoth’s pointer wrapper nor this save loader is evidence of a ready-made `Ptr32` arena implementation.
5. **SCX visual cross-check:** compare selected v3/v4 meshes in the existing parser against the independently implemented Blender importer and its ImHex pattern; use original bytes as authority, not either decoder. [Importer](https://github.com/nonamesex/io_invo_scx/blob/7bcc34e51a3cb40369cc28db2950ab27f9587078/README.md), [pattern](https://github.com/nonamesex/io_invo_scx/blob/7bcc34e51a3cb40369cc28db2950ab27f9587078/imhex/scx.hexpat). License and version scope require separate review.

[Some lines truncated to 768 chars. Use agent://PortToolingResearch.ComparativeResearch:raw:10-10 to read line 10 whole; cut lines run to line 11, each read the same way]

## 5. Differential tracing and Rung 3 unblocker

### Findings for Section 5: tracing the stock collision walk

### What is established locally

- The stock closest-core walk is **`0x004ae9c0`**, not `0x004a7c10`: the existing oracle calls it as a nine-argument **`__cdecl`** function returning `float` (`tools/wine/core_pair_oracle.c:12,78–86`). `0x004a7c10` is the body integrator and is decompiled as `__thiscall` (`local/census/all.c:123216–123221`; `src/physics_body.hpp:241–243`). Do not apply the same detour signature to both.
- The port’s ground-face update routes both Box and Mesh through `closestPolyhedronCores`, and returns `Unsupported` if the predicted query has no result (`src/physics_contact_geometry.cpp:320–357`). That eventually becomes the reported step failure (`src/car_world_step.cpp:615–617`).
- **The precise cause is not established.** The solver can return `nullopt` for empty geometry, invalid feature indices or adjacency, missing selections, and other paths (`src/physics_core_pair.cpp:503–527,575–580,612–672`). Its loop-cap path instead returns distance `1e6` (`:699–706`). Mesh dispatch alone does not prove which branch fails.
- The native trace is **one record per port step**, with top-level keys `tick`, `f`, `wall`, `wdt`, `dt`, `pt`, `ok`, `pre`, `post`, `spd`, `asleep`, `ctl`, `wh`, and `col`, optionally `anom` and failure details (`src/car_world_step.cpp:798–910`). Wheel records currently emit **duplicate `cb` keys** for different values (`:867–879`); ordinary JSON parsers generally retain only one. Preserve the raw source and resolve this ambiguity in any comparison tooling.
- `State` is 0xd4 bytes; position, velocity, rotation, and angular velocity are at offsets `0`, `0x0c`, `0x40`, and `0x70` (`src/physics_body.hpp:145–169`). The oracle’s inferred shape/feature layout and cached feature pointers are in `tools/wine/core_pair_oracle.c:7–12`.

### Complete entry-hook sketch

The following is a **diagnostic companion compiled into the existing proxy**, not a claim that a raw walk call already maps to a port tick. It records actual call inputs without changing the x87 return value: a naked entry stub saves/restores flags, all GPRs, and the complete `FXSAVE` state, then jumps to the MinHook trampoline. It logs the raw state/core bytes and cached feature pointers as a separate JSONL event stream. It intentionally does **not** invent unavailable port-schema values.

MinHook’s upstream API creates a disabled hook and returns a trampoline; `MH_EnableHook` activates it. Its x86 support and explicit error codes are documented in the [v1.3.4 header](https://github.com/TsudaKageyu/minhook/blob/v1.3.4/include/MinHook.h). The existing proxy already logs x87 control words (`tools/wine/capture/d3d9_proxy.c:63–67,274–278,318–322`).

```c
/* stock_walk_trace.c; 32-bit MinGW, linked with MinHook v1.3.4.
 *
 * Integration: call trace_install_walk(expected, n) from a normal
 * post-loader-lock proxy initialization point BEFORE exercising physics.
 * expected must be independently obtained and verified bytes at RVA 0xae9c0
 * for the exact executable. Call once; do not call from DllMain.
 *
 * Output: stock-walk.jsonl in the process working directory.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "MinHook.h"

#if !defined(__i386__)
#error This diagnostic is specifically for 32-bit x86.
#endif

static void *walk_trampoline;
static SRWLOCK trace_lock = SRWLOCK_INIT;
static __thread int inside_trace;
static volatile LONG sequence;

/* pushal memory order at the saved ESP: edi, esi, ebp, saved esp,
 * ebx, edx, ecx, eax. The saved esp points at the preceding pushfl;
 * adding 4 gives the stack pointer on function entry.
 */
__attribute__((noinline, used))
static void trace_walk_entry(const uint32_t *regs)
{
    DWORD old_error = GetLastError();
    uint32_t args[10]; /* return address followed by nine cdecl arguments */
    unsigned char pose_a[0xd4], pose_b[0xd4];
    unsigned char shape_a_core[28], shape_b_core[28];
    uint32_t cache_a = 0, cache_b = 0;
    SIZE_T got;
    HANDLE self = GetCurrentProcess();
    uintptr_t entry_sp = (uintptr_t)regs[3] + 4u;
    unsigned short cw;
    FILE *f;

    if (inside_trace) {
        SetLastError(old_error);
        return;
    }
    inside_trace = 1;
    __asm__ volatile("fnstcw %0" : "=m"(cw));

    memset(args, 0, sizeof args);
    memset(pose_a, 0, sizeof pose_a);
    memset(pose_b, 0, sizeof pose_b);
    memset(shape_a_core, 0, sizeof shape_a_core);
    memset(shape_b_core, 0, sizeof shape_b_core);

    if (!ReadProcessMemory(self, (void *)entry_sp, args, sizeof args, &got) ||
        got != sizeof args) goto done;

    /* +0x20 is the core location in the oracle's shape wrapper. */
    if (!ReadProcessMemory(self, (void *)(uintptr_t)args[1], pose_a,
                           sizeof pose_a, &got) || got != sizeof pose_a)
        goto done;
    if (!ReadProcessMemory(self, (void *)(uintptr_t)args[2], pose_b,
                           sizeof pose_b, &got) || got != sizeof pose_b)
        goto done;
    if (!ReadProcessMemory(self, (void *)((uintptr_t)args[3] + 0x20),
                           shape_a_core, sizeof shape_a_core, &got) ||
        got != sizeof shape_a_core)
        goto done;
    if (!ReadProcessMemory(self, (void *)((uintptr_t)args[4] + 0x20),
                           shape_b_core, sizeof shape_b_core, &got) ||
        got != sizeof shape_b_core)
        goto done;
    if (!ReadProcessMemory(self, (void *)(uintptr_t)args[5], &cache_a, 4,
                           &got) || got != 4)
        goto done;
    if (!ReadProcessMemory(self, (void *)(uintptr_t)args[6], &cache_b, 4,
                           &got) || got != 4)
        goto done;

    AcquireSRWLockExclusive(&trace_lock);
    f = fopen("stock-walk.jsonl", "ab");
    if (f) {
        unsigned long seq = (unsigned long)InterlockedIncrement(&sequence);
        unsigned i;
        fprintf(f, "{\"event\":\"walk_enter\",\"seq\":%lu,"
                   "\"thread\":%lu,\"caller\":\"%08lx\",\"cw\":\"%04x\","
                   "\"shape_a\":\"%08lx\",\"shape_b\":\"%08lx\","
                   "\"feature_a\":\"%08lx\",\"feature_b\":\"%08lx\","
                   "\"out_a\":\"%08lx\",\"out_b\":\"%08lx\","
                   "\"out_n\":\"%08lx\",\"pose_a\":\"",
                seq, (unsigned long)GetCurrentThreadId(),
                (unsigned long)args[0], cw,
                (unsigned long)args[3], (unsigned long)args[4],
                (unsigned long)cache_a, (unsigned long)cache_b,
                (unsigned long)args[7], (unsigned long)args[8],
                (unsigned long)args[9]);
        for (i = 0; i < sizeof pose_a; ++i) fprintf(f, "%02x", pose_a[i]);
        fputs("\",\"pose_b\":\"", f);
        for (i = 0; i < sizeof pose_b; ++i) fprintf(f, "%02x", pose_b[i]);
        fputs("\",\"core_a\":\"", f);
        for (i = 0; i < sizeof shape_a_core; ++i)
            fprintf(f, "%02x", shape_a_core[i]);
        fputs("\",\"core_b\":\"", f);
        for (i = 0; i < sizeof shape_b_core; ++i)
            fprintf(f, "%02x", shape_b_core[i]);
        fputs("\"}\n", f);
        fclose(f);
    }
    ReleaseSRWLockExclusive(&trace_lock);

done:
    inside_trace = 0;
    SetLastError(old_error);
}

/* Do not put C statements in a naked function. EBX holds the saved-register
 * frame across the C call; the x86 ABI requires trace_walk_entry preserve it.
 * FXSAVE needs a 16-byte-aligned 512-byte region.
 */
__attribute__((naked))
static void walk_entry_stub(void)
{
    __asm__ volatile(
        "pushfl\n\t"
        "pushal\n\t"
        "movl %esp, %ebx\n\t"
        "subl $528, %esp\n\t"
        "andl $-16, %esp\n\t"
        "fxsave (%esp)\n\t"
        "pushl %ebx\n\t"
        "call _trace_walk_entry\n\t"
        "addl $4, %esp\n\t"
        "fxrstor (%esp)\n\t"
        "movl %ebx, %esp\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_walk_trampoline\n\t"
    );
}

int trace_install_walk(const unsigned char *expected, size_t n)
{
    HMODULE exe = GetModuleHandleA(NULL);
    unsigned char *base = (unsigned char *)exe;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *nt;
    void *target;
    MH_STATUS status;

    if (!base || !expected || n < 8 || n > 32) return 0;
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.SizeOfImage < 0xae9c0u + n)
        return 0;
    target = base + 0xae9c0u;
    if (memcmp(target, expected, n) != 0) return 0;

    status = MH_Initialize();
    if (status != MH_OK) return 0;
    status = MH_CreateHook(target, walk_entry_stub, &walk_trampoline);
    if (status != MH_OK) {
        MH_Uninitialize();
        return 0;
    }
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        MH_RemoveHook(target);
        MH_Uninitialize();
        return 0;
    }
    return 1;
}
```

Build this object with the same `i686-w64-mingw32-gcc` toolchain as the proxy and link the **x86 MinHook C sources and header**; adapt the existing proxy’s build command rather than deploy a second `d3d9.dll`. The exact expected bytes must be obtained independently from the chosen stock image; an RVA and a few bytes are not sufficient overall binary identity. In particular, check the complete executable hash and module identity before enabling. This code is **uncompiled and untested** here.

**Limitations and necessary next instrumentation:**

1. The entry logger alone does not record output distance, output points/normal, returned feature caches, feature adjacency, or port tick. A correct return observer needs an ABI-verified assembly return hook that preserves the x87 `ST(0)` return and all machine state; a plain C detour returning `float` can introduce a spill/rounding change. Stock calls may be nested or concurrent, so pair entries and exits by thread plus call depth/sequence.
2. Snapshot the **full feature graph**, including bounded vertex/feature/adjacency arrays, while their pointers are valid. The 28-byte core header alone gives pointers and counts, not the geometry required to reproduce the case. Validate ranges and impose limits before dereferencing.
3. Establish a separate verified physics-step boundary and stable car/body identity; `Present` count is not a physics tick. Buffer each stock walk event under its step, then derive matching `pre`/`post` records only after verifying stock object offsets and timing. Unknown values should be omitted or `null` in a documented extended schema, never zero-filled as if observed. Exact `cartrace-full.jsonl` parity is **not yet established**.
4. Logging is intrusive: file I/O, scheduling and `ReadProcessMemory` may affect timing. Run a control capture with logging disabled; use a bounded per-thread buffer and deferred writer for serious comparisons. FXSAVE preserves FP/SIMD state here, but not timing or all process-visible effects. Skipped/unreadable snapshots should ultimately be counted explicitly.
5. The code presumes the walk signature, shape-core layout, MinGW symbol spelling, and availability of FXSAVE as in the intended x86 environment. Confirm disassembly and actual caller behavior before relying on it. Installation must occur outside the Windows loader lock; see Microsoft’s [DLL best practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices).

**Differential procedure.** Reproduce the same initial car, assets, input and timestep; first compare stock/port pre-state, then the ordered walk invocations and their input geometries/caches, then returned feature pairs, distance and normals, then contact updates and resulting integration. Match by verified step boundary, body and stable geometry identity—not raw addresses or render frame. Add targeted reasons to each port `nullopt` return; replay the first divergent stock query in the existing Wine oracle with identical raw 32-bit inputs. A mismatch in pre-state precedes and invalidates attribution to the collision solver; matching pre-state with a port-only rejection identifies the specific unsupported path, not necessarily its final fix.

### x87 parity

- The existing oracle explicitly sets x87 control word `0x007f` around stock calls (`tools/wine/core_pair_oracle.c:60–64,78–88`); do not assume this is the live game’s physics control word. Capture it on the actual physics thread and distinguish it from the proxy’s Present-thread observations.
- Intel’s [Software Developer’s Manual, volume 1, chapter 8](https://cdrdv2-public.intel.com/825743/325462-sdm-vol-1-2abcd-3abcd-4.pdf) specifies the x87 control word’s precision/rounding controls and extended-format register stack. `0x007f` selects 24-bit significand precision for affected x87 arithmetic, round-to-nearest, and masked exceptions; it does **not** make every x87 operation equivalent to ARM float: extended exponent range, stores, intermediate lifetimes, exceptions, and operation ordering still matter.
- `-ffp-contract=off` prevents contraction but does not emulate x87 or force identical intermediate spills. For each captured problematic solver case, record input/output float **bit patterns**, CW and relevant status, and compare stock oracle against native port. Test values adjacent to zero and Voronoi clipping boundaries, plus overflow/underflow and near-ties; isolate where results first diverge before introducing emulation. Do not assert that precision explains the current mesh failure without a matching replay.
- If exact extended semantics prove necessary, a software IEEE extended implementation is a candidate, not a proven drop-in for every x87 instruction: [Berkeley SoftFloat Release 3](https://www.jhauser.us/arithmetic/SoftFloat-3/doc/SoftFloat.html) documents `extFloat80_t`, rounding and exception behavior. Confirm instruction-by-instruction and control-word behavior before adopting it.
### Explicit unresolved [INFERENCE] items

1. [INFERENCE] The changed branch may affect startup/control flow; its purpose and downstream effect are unproven.
2. [INFERENCE] The selected formatting compromise and Ninja pool sizes may fit this checkout; no format diff or performance measurement was run.
3. [INFERENCE] Per-tick record/replay and first-divergence analysis may locate the dealer failure; no matched stock/port trace or exact failing branch is yet available.
4. [INFERENCE] Captured stock feature graphs can be reconstructed in the existing oracle only after pointer layout, calling convention, object identity, and lifetimes are verified.
5. [INFERENCE] Software extended precision might help only if a demonstrated x87 mismatch requires it; it is not a diagnosis of the current failure.
6. [INFERENCE] A future REA project/archive integration can unify reads if implemented with verified identity, provenance and ownership guarantees; the present tool contract does not establish such a bridge.
7. [INFERENCE] Reusing the existing parser for a TUFA inspector and adding stock-corpus goldens are likely high-return; exact implementation and fixture values need project-specific evidence.
8. [INFERENCE] Cross-project organization patterns and selected checks can improve maintainability; they do not establish equal algorithms, native layouts, or deterministic behavior.

### Summary

The byte difference changes an x86 conditional branch, not a PE header. The recommended immediate path is an authoritative persistent Ghidra project plus separate digest-bound REA evidence, reviewed fixed-width layouts, shared sanitizer-instrumented build targets, and stock physics capture at the actual closest-core walk. The schema adapter, live return hook and REA project bridge remain unverified prerequisites, not completed tooling.

Source inventory: 41 distinct upstream URLs, plus the cited local project files; counts are links, not an assessment of independent evidence.
