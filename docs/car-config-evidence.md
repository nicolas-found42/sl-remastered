# Car configuration reader (`native car <path>.cfg`)

Plain words first. A car is built from a text file. Each line starts with a word such as `body`, `wheel` or `slot` and
is followed by numbers. The game looks the word up in a list of 107 names (the table at `0060e0f8`) and runs one small
function (a "handler") for it. `src/car_config.hpp` and `src/car_config.cpp` do the same thing but only store the
numbers as typed data (`CarConfig`). They do not build physics bodies, load meshes or draw anything. Where stock would ask
the engine to load a resource, the reader writes the request into `CarConfig::resources` instead.

Result on the real 2005 data: **all 39 car files parse (39 of 39)**, and all 107 directive handlers are read. The unit test
`car_config_unit` (`tests/car_config_test.cpp`) checks this on the real files.

## Where the evidence comes from

* The executable is `ref/raw/StreetLegal_Redline.bak.exe` (MD5 `e686099e7233bb7d7c461b1b35c5064c`). Every handler was read
  from `local/census/all.c`, the saved whole-program decompile of that file (made by `DecompileAll.java`, which ran on the
  read-only project of the `.bak.exe`; its sibling logs `DecompileAt-*.log` print `MD5: e686099e...` in their header).
  No new decompile was run, so no address was decompiled twice.
* The Ghidra MCP was **not reachable** during this work (`get_program_info` returned `BACKEND_UNAVAILABLE`), so the live
  MD5 check through the MCP could not be repeated. The saved logs are the evidence. Nothing was verified by running the
  stock game.
* Strings and tables were read as bytes from the executable with `tools/dump_directive_table.py` and
  `tools/decomp_annot.py` (both read-only). `tools/decomp_fn.sh <addr>` prints one function from `all.c`, and
  `tools/dh.sh <addr>...` prints it with the real scanf format strings filled in.
* `ref/` repositories (`SLRR_rev-main` etc.) were used only as leads for names; nothing is taken from them without the
  decompile.

## Which files are "car cfg files"

The 2005 data holds 448 `.cfg` files under `cars/`. Car resources are the `_main*.cfg` files: **39 files**, and they are
exactly the 39 cfg files that contain a `chassis` line. They sit in `cars/racers/*_data/scripts`,
`cars/fake_racers/*/scripts`, `cars/traffic/*/scripts` and `cars/misc/Police_V3/scripts`. All other `.cfg` files in
`cars/` are part files (`native part`, a different table at `00611368`, handled by `src/part_config.cpp`). The `native car`
payload lines themselves are not stored as readable text in the `.rpk` files we have, so the `_main*` rule comes from the
file names plus the `chassis` check.

`ref/raw` is a modified install, not ground truth. It has 40 `_main*.cfg` files: the same 39 names (24 of them differ
from the 2005 files) plus `cars/traffic/Wreck_data/scripts/_main_.cfg`. Running the reader on it parses 38 of 40. The two
failures are `_main_SunStrip_1_8_DVC.cfg` (line 155) and `_main_SunStrip_2_2_DVC.cfg` (line 156): a `slot` line in the mod
files has six numbers and no id (`slot -0.024 0.150 0.000 0.000 0.000 0.200 ; FR shock absorber`). Stock leaves the id of
that slot uninitialised, so the reader refuses it on purpose. The 2005 files have `... 302` on that line. The unit test
uses only the 2005 extract.

## Reading the lines (004b6520, 004b6340)

Mirrored in `tokenizeCarConfig`:

1. The file is read line by line (a line ends at `\n`; `00554d60`). Each line is copied into a 256-byte buffer. A line of
   256 bytes or more makes our reader fail (stock would run past the buffer); the longest real line is 188 bytes.
2. The copy is cut at the first `;`, then `#`, then `/`. That is why `//water object` and `; viztest` are comments, and also
   why a path written with a forward slash would be cut.
3. `strtok` with the delimiters space, CR, LF and tab takes the first word. A line with no word is skipped.
4. If the word equals `eof` (any case) reading stops (`004b6340` returns 1).
5. The word is compared case-insensitively (`005d7190`) with the names in the table, in order. **A word that is not in the
   table is skipped without any message.** `CarConfig::ignored` records each such line.
6. The handler is called with the line buffer. Handlers scan from `buffer + strlen(buffer) + 1`, that is, one character after
   the end of the first word. We keep exactly that in `CarConfigLine::args`. It matters for `%c`, and `rawKey` keeps leading
   blanks because `render`/`light` and `driver`/`controller` compare the whole first argument with `strcmp`.

`src/part_config.cpp`'s tokenizer (`parsePartConfig`) was **not** reused: it removes leading blanks from the arguments, which
loses the "one character after the key" rule. The test checks that both tokenizers agree on ordinary lines.

## The game's own number reader (00553890)

Handlers do not use the C library. They call the game's own `sscanf` (`00551240` forwards to `00553890`), mirrored by
`stockScan`:

* `%f`: digits, an optional `.` and digits, **no exponent and no leading `+`**. A digit must follow a `-`. The value is built
  in 32-bit float arithmetic, one digit at a time (weights 0.1, 0.01, ... as floats). Over all 22,658 decimal numbers in the
  39 real files it matches `strtof` exactly 19,549 times and is at most 2 units in the last place away otherwise (for
  example `0.00006`). We reproduce the float steps in order; whether the original x87 code rounds each step exactly the
  same way is not proven (no disassembly was checked).
* `%d` and `%u`: decimal, or hexadecimal when the text starts with `0x` (`00553ce0`). `0x` needs at least one hex digit.
  `-0x10` reads only `-0` and leaves `x10`. The value wraps to 32 bits (`0xFFFFFFFF` is -1). `%x` requires the `0x`.
* `%s`: skips blanks, then stops at a blank, at the width (`%8s`) or at the character that follows `%s` in the format. An
  empty word still counts as a converted item, and stock writes a NUL through its pointer.
* `%#` asks the routine to return the number of bytes read instead of the number of conversions. `colors`, `lods` and
  `torque` use it to walk through a list.
* Any other literal character in the format must match (the `,` in `ext_camera`, the `:` in `torque`).
* On failure the routine returns what it converted so far, and the handler keeps whatever was in its variables.

## What the reader does when stock would read garbage

Many handlers pass fewer destination pointers than the format has conversions, or leave stack variables uninitialised when
a number is missing. Stock then reads or writes unpredictable memory. The reader refuses those lines (the parse fails with
the line number, the directive and its handler address) instead of inventing a value. Cases:

* `body`, `type`, `camera`, `wheel`, `slot`, `bolt`, `arm`, `hub`: not enough numbers.
* `instant_center` with more than 6 numbers, `osd_abs/asr/hbrake/emergency` with more than 4 numbers (the format also has
  a `%8s` and only five pointers exist).
* `nocollision` or `buoy` right after a `render`/`light`/`deformable` line (they write through the null geometry pointer of
  a render record).
* `slottype`/`slotflags` before the first `slot` (stock writes through a null pointer).
* `ext_camera` before any `driver`/`controller` (stock writes before the first driver record).
* `torque` with a count above 5 (stock allocates five entries), `lods` with a count above 15 (it would overwrite the next
  car field), a negative `sfx_horn` index, a `colors` list shorter than its count.

Stock behaviors that are not errors are copied: lines over capacity are dropped (32 body/render records, 16 wheels, 17
wings, 16 cameras, 8 rollbars, 4 drivers, 16 materials, 16 engine sounds per kind) and appear in `ignored`; handlers that
need a body, a slot or a wheel do nothing when none exists yet and also appear in `ignored`.

## Lines that stock skips

Six words used by the real files are **not in the table at `0060e0f8`**, so stock ignores them:
`compound`, `endcompound`, `chassis` (all 39 files; these are comments left by the vehicle importer, the executable does
not contain these strings at all), `intdamage` (12 files) and `sfx_loop` (2 files; both exist only in the part table at
`00611368`), and `stcokpart` (2 files, `_main_GTB.cfg` and `_main_GTLE.cfg`, line 424/425: a typo for `stockpart`, so the
`R wing` stock part is never requested in stock either). The earlier note in `docs/car-factory-evidence.md` listing
`compound`/`chassis` among the car's first lines is correct as a description of the files, but these lines have no
effect.

## What each directive does

Counts are files / lines in the 39 real car files (`python3 -I tools/car_cfg_census.py`). A directive with `0 / 0` is not
used by any real car file, so it was transcribed from the decompile only and checked by the synthetic tests in
`tests/car_config_test.cpp`, never against a real file (56 directives).

| # | directive | handler | files / lines in real cars | what the typed result keeps |
|---|---|---|---|---|
| 0 | `click` | `0046b950` | 24 / 24 | click shape (file or id) + position (`CarConfig::click`) |
| 1 | `body` | `0046bb80` | 39 / 348 | body record: pose, mass, shape word (`CarBodyRecord`, max 32) |
| 2 | `bone` | `0046c280` | 39 / 39 | flag 0x400 on the last record, root body = last |
| 3 | `noclick` | `0046c2d0` | 39 / 348 | flag 0x200 on the last record |
| 4 | `nocollision` | `0046c310` | 24 / 48 | flag 0x100 (+ geometry flag 8); refused after a render record |
| 5 | `buoy` | `0046c360` | 24 / 24 | geometry flag 0x800; refused after a render record |
| 6 | `render` | `00443de0` | 39 / 269 | render record, tagged when it follows `steering` or `wheel` |
| 7 | `light` | `00443de0` | 0 / 0 | same as render, load type 0xe |
| 8 | `deformable` | `0046bfa0` | 39 / 39 | render record, never tagged |
| 9 | `shadow` | `0046c1a0` | 0 / 0 | shadow resource + pose |
| 10 | `flags` | `0046d0c0` | 15 / 173 | ORs value & 0x1ff into the last record |
| 11 | `lod` | `0046d180` | 9 / 177 | two 4-bit levels into flags bits 16-23 |
| 12 | `lods` | `0046d290` | 9 / 9 | list of distances (+ 1.0 end marker rule) |
| 13 | `colors` | `0046d330` | 15 / 184 | count + list of ints on the last record |
| 14 | `category` | `0046ced0` | 38 / 38 | int (default 2) |
| 15 | `slot` | `0046c3a0` | 29 / 1773 | `CarSlot`: pose + id |
| 16 | `bolt` | `0046c4e0` | 0 / 0 | bolt on the last slot |
| 17 | `attach` | `0046c6a0` | 24 / 757 | attachment rule on the last slot |
| 18 | `compatible` | `0046c770` | 14 / 349 | (resource, value) on the last slot |
| 19 | `slotdmgmode` | `0046c840` | 24 / 408 | slot damage mode |
| 20 | `slotdeform` | `0046c8a0` | 24 / 54 | slot deform factor |
| 21 | `slottype` | `0046cb30` | 24 / 792 | slot type (same field as slotflags) |
| 22 | `slotflags` | `0046cb30` | 0 / 0 | same field as slottype |
| 23 | `slotmove` | `0046ccd0` | 0 / 0 | slot move: mode, angle, axis |
| 24 | `deform` | `0046cf10` | 0 / 0 | float (default 1) |
| 25 | `mesh` | `0046ce60` | 39 / 39 | load request type 5 |
| 26 | `texture` | `0046cdf0` | 25 / 25 | load request type 7 |
| 27 | `logo` | `0046d400` | 0 / 0 | load request type 7 |
| 28 | `flap` | `0046c8f0` | 29 / 129 | 15-number flap on the last slot |
| 29 | `flapsfx` | `0046ca70` | 0 / 0 | two sound ids on the last slot flap |
| 30 | `camera` | `00443b00` | 25 / 122 | camera (max 16); the first one also sets default seat/pedals |
| 31 | `type` | `00442f90` | 39 / 158 | starts a wheel record: mass, shape word, size (max 16) |
| 32 | `friction` | `004432b0` | 0 / 0 | wheel friction pair |
| 33 | `spring` | `004431c0` | 39 / 158 | wheel spring triple (<=0 replaced by 0.1/0.2/0.05) |
| 34 | `wheel` | `004433e0` | 39 / 158 | wheel pose and four factors; resets bonepos/arm/hub/instant defaults |
| 35 | `wheelbones` | `004438d0` | 24 / 96 | marks the bone sphere |
| 36 | `bonepos` | `00443a00` | 24 / 192 | wheel bone position |
| 37 | `wheelpars` | `00443310` | 15 / 15 | 9 car-wide wheel numbers |
| 38 | `skid` | `00443390` | 0 / 0 | 3 skid numbers |
| 39 | `wheeldeform` | `004439b0` | 0 / 0 | wheel deform |
| 40 | `arm` | `004435b0` | 0 / 0 | wheel arm (7 numbers) |
| 41 | `hub` | `00443690` | 0 / 0 | wheel hub (10 numbers) |
| 42 | `instant_center` | `004437a0` | 0 / 0 | two points (+ wheel position) |
| 43 | `steering` | `00443ea0` | 25 / 25 | body line + remembers it as the steering body |
| 44 | `sw_radius` | `00443ef0` | 0 / 0 | steering radius pair x0.01 |
| 45 | `steeringradius` | `00443ef0` | 0 / 0 | same as sw_radius |
| 46 | `maxsteer` | `00444260` | 39 / 39 | two numbers |
| 47 | `rpm_idle` | `004442b0` | 0 / 0 | one number (stock stores x0.1047) |
| 48 | `drag` | `00444310` | 0 / 0 | 4 numbers |
| 49 | `wing` | `004443c0` | 24 / 91 | wing (max 17) |
| 50 | `center` | `00444570` | 0 / 0 | 3 numbers |
| 51 | `rollbar` | `00443a60` | 15 / 30 | rollbar (max 8) |
| 52 | `linked` | `0046d8a0` | 0 / 0 | int triple on the last record, flag 0x4000 |
| 53 | `sfx_engine_up` | `00445760` | 0 / 0 | engine sound (max 16) |
| 54 | `sfx_engine_down` | `00445890` | 0 / 0 | engine sound (max 16) |
| 55 | `sfx_exhaust` | `004459c0` | 0 / 0 | engine sound (max 16) |
| 56 | `sfx_gearbox_fwd` | `00445af0` | 0 / 0 | sound id + float |
| 57 | `sfx_gearbox_rev` | `00445b80` | 0 / 0 | sound id + float |
| 58 | `sfx_wreck` | `00445c10` | 0 / 0 | sound id |
| 59 | `sfx_horn` | `00445c80` | 1 / 4 | sound id + volume in one of 4 slots |
| 60 | `sfx_gearchange` | `00445d50` | 0 / 0 | two sound ids |
| 61 | `mass` | `00443f50` | 0 / 0 | one number |
| 62 | `enginepower` | `00443f90` | 39 / 39 | 6 numbers (+ copy of the first) |
| 63 | `ratios` | `00444080` | 39 / 39 | gear count + 7 numbers |
| 64 | `torque` | `004440e0` | 0 / 0 | count + rpm:value pairs (max 5) |
| 65 | `rearendratio` | `00444150` | 39 / 39 | one number |
| 66 | `brakepower` | `00444190` | 39 / 39 | two numbers |
| 67 | `clutch` | `00444200` | 0 / 0 | one number |
| 68 | `driver` | `004445f0` | 0 / 0 | driver record (max 4); also sets the default driver |
| 69 | `controller` | `004445f0` | 39 / 39 | driver record (max 4) |
| 70 | `ext_camera` | `00443d40` | 39 / 39 | 11 numbers on the last driver |
| 71 | `nodriver` | `00444710` | 0 / 0 | does nothing in stock |
| 72 | `name` | `00444730` | 24 / 24 | does nothing in stock |
| 73 | `seat` | `00444740` | 24 / 24 | 6 numbers |
| 74 | `pedals` | `00444810` | 24 / 24 | 6 numbers x3 blocks |
| 75 | `shifter` | `00444990` | 0 / 0 | 6 numbers |
| 76 | `key` | `00444a60` | 0 / 0 | parsed and dropped by stock |
| 77 | `handbrake` | `00444ae0` | 0 / 0 | parsed and dropped by stock |
| 78 | `cockpit_rpm` | `00444b60` | 24 / 24 | gauge: pose, resource, 4 numbers |
| 79 | `cockpit_speed` | `00444cb0` | 24 / 24 | gauge: pose, resource, 4 numbers |
| 80 | `osd_gauge` | `00444e00` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 81 | `osd_rpmpin` | `00444e80` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 82 | `osd_spdpin` | `00444f20` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 83 | `osd_nospin` | `00444fc0` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 84 | `osd_damage` | `00445060` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 85 | `osd_gear` | `00445120` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 86 | `osd_speed` | `004451b0` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 87 | `osd_rpm` | `00445260` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 88 | `osd_nos` | `00445310` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 89 | `osd_turbo` | `004453c0` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 90 | `osd_mileage` | `00445470` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 91 | `osd_abs` | `00445520` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 92 | `osd_asr` | `004455b0` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 93 | `osd_hbrake` | `00445640` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 94 | `osd_emergency` | `004456d0` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 95 | `osd_damage_slot` | `004450e0` | 0 / 0 | OSD element: id, numbers, optional color and text |
| 96 | `stockpart` | `0046d970` | 28 / 624 | stock part id (load type 8) |
| 97 | `material` | `00445e00` | 39 / 39 | 3 ids + float (max 16) |
| 98 | `damage` | `0046cf50` | 12 / 12 | one number (first of two) |
| 99 | `damagedef` | `00445ef0` | 39 / 39 | does nothing in stock |
| 100 | `velfactor` | `00445fc0` | 0 / 0 | two numbers |
| 101 | `turn` | `00445f00` | 0 / 0 | 3 numbers (stored negated) |
| 102 | `roll` | `00446000` | 0 / 0 | 3 numbers |
| 103 | `pitch` | `00446050` | 0 / 0 | 3 numbers |
| 104 | `steerhelp` | `004460a0` | 34 / 34 | 6 numbers |
| 105 | `cheat_control` | `00446100` | 0 / 0 | 5 numbers |
| 106 | `notify` | `00446160` | 1 / 1 | sets a flag |

Stock object layout used in the notes: the car is a part object extended with a car record at `+0x2e48`. The body list
(`+0x128`, 0x74 bytes each, count at `+0xfa8`) holds `body`, `steering`, `render`, `light` and `deformable` records in
file order; `noclick`, `bone`, `flags`, `lod`, `colors`, `linked`, `nocollision` and `buoy` change the last one. Slots live
in the list at `+0x1164`; `attach`, `bolt`, `compatible`, `slot*`, `flap*` change the last slot. Wheel records
(`+0x1f44`, 200 bytes each, count at `+0x1f40`) start at `type`; `friction`, `spring`, `wheel`, `wheelbones`, `bonepos`,
`wheeldeform`, `arm`, `hub` and `instant_center` change the last wheel. All handlers return immediately when the object's
initialisation counter at `+0x120` is not zero, so only the first reading of a file has any effect; the reader models
that first reading.

Two tagging rules are worth knowing (`00443de0`): a `render` or `light` line that directly follows `steering` gets the
flag `0x8000` and the steering body index in the top byte, and one that directly follows `wheel` gets `0x1000` and the
wheel index in the top byte. `deformable` records are never tagged.

## Defaults

Fields keep the defaults written by `00446180` and `0046e320` when a line does not set them (`enginePower`, `brakePower`,
`clutch`, `rearEndRatio`, `maxSteer`, `steeringRadius`, `skid`, `steerHelp`, `wheelPars`, `category`, `deform`, `damage`,
...). The first `camera` line also writes default seat, pedal and shifter positions (`00443b00`); a later `seat`, `pedals` or
`shifter` line replaces them. Two details of that default are inferences: the decompile calls three-float setters
(`00551c70`, `00551c60`) on pointers it does not show, so `seat[3..5] = (0, 0.3, 0)` and the zero tail of the pedal blocks
and the shifter assume the next member of each record. No real car file has a `shifter` line, so for the 25 cars that have
a `camera` the shifter value in `CarConfig` is this derived default. `Police_V3` has cameras but no `seat` or `pedals`
line either, so it uses the derived seat and pedals too.

The default sound set, the default torque curve and all 19 OSD element defaults of `00446180` are **not** transcribed.
OSD elements only record the numbers the file gives.

## Honest limits

* Everything here is static reading of the saved decompile plus bytes of the executable. Nothing was compared against a
  running stock game, and the Ghidra MCP was offline.
* Resource ids are stored as written. Stock remaps them through the current pack with `00544590`
  (`remapResourceId` in `src/resource_id.cpp` already mirrors it); apply it when the resource is actually loaded.
* Stock creates the collision shapes (`0049ab60`, `0049a990`) when it reads `body`, `type`, `click`, `wheelbones`. If the
  mesh fails to load, stock removes the body again. We assume success. The test confirms that all 309 collision-mesh `body` lines
  point at files that exist in the extracted data (on macOS the file system ignores case, so a wrong letter case
  would not be caught).
* Some values are scaled by factory-set car fields: the camera FOV by `+0x127c`, `ext_camera` number 1 by
  `+0x127c` and number 11 by `+0x1278` (set from the Config keys `camera_FOV` and `camera_distance` in `00442500`). We keep the raw
  numbers.
* `steering` also recomputes angles from the new body's matrix (`00551c90`); that result is not stored here.
* The x87 rounding of `%f` is not proven bit-identical (see above).
* The scanner mirrors the lines of `004b6340`; the file loader `0054c300` turns `/` in the path into `\`. The final line of a
  file without a trailing newline reads one byte past the buffer in stock; all real files end with a newline.

## How to wire it (not done here)

`src/natives/game_type.cpp` already creates a native part prototype for `native part` (it calls
`initializePartPrototype`). The same place should get a `car` branch: read the file named by the script resource's
`nativeConfigPath`, call `parseCarConfig(text, config, error)`, and keep the `CarConfig` on the type-8 resource node next to
`ScriptNode::part` (a new `std::unique_ptr<CarConfig> car` field in `src/world_model.hpp`; that file and
`game_type.cpp` belong to other workers, so this was not edited). The factory step (`00442500` then `00446180`) would then
turn `CarConfig` into live objects: apply `resources` with the current `ResourceIdMapping`, create the body shapes from
`bodies`/`wheels`, add the default sound set when an engine sound list is empty, and call the registered car natives.

## How to run it

```
cmake -S . -B local/build-cfg -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build local/build-cfg --target car_config_test
(cd local/build-cfg && ctest -R car_config_unit --output-on-failure)
python3 -I tools/car_cfg_census.py          # directive counts over the 39 real files
tools/dh.sh 0046bb80                         # one stock handler with its format strings
```
