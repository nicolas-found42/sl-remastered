# Phase 2 findings

This page records what phase 2 found about the data formats. Each result names the command that reproduces it.

## Exit check

Command: `tools/phase2_check.sh`, which `tools/check.sh` also runs. It compares the C++ loaders with independent Python readers and compares file counts with the filesystem.

| Format | On disk | C++ opens | Python opens | Known stale | Compared |
|---|---:|---:|---:|---:|---|
| RPK archives | 80 | 80 | 80 | 0 | totals and sorted entry listing digest |
| SCX models | 3,028 | 3,028 | 3,028 | 0 | 3,028 lines and sorted material listing digest |
| SCY models | 21 | 21 | 21 | 0 | 21 lines |
| Class files | 1,501 | 1,500 | 1,500 | 1 | 1,500 lines |
| SDAT saves | 11 | 11 | 11 | 0 | 11 lines and sorted entry listing digest |

The RPK digest is SHA-256 over sorted lines containing relative archive path,
hex-encoded alias bytes up to the first NUL, and payload size. Duplicate entries
remain in the listing. The readers independently produce the listing; equality
checks reader agreement, not a pinned golden corpus or payload contents. On
this install the digest is
`ed454f85e227d09f393d261169a7793a3b2e4a9f612f1d6a0e985cbc9be45f0c`.

The SDAT digest covers each unsaved record's ordinal, kind and size, and each
pack record's ordinal, raw path bytes and path length, scoped by relative save
path. It compares the readers rather than pinning a golden save. This install's
digest is `c62a7993ec3db84321b2985cdaa08ca5154a5e564879332148dc27520c506453`.

The SCX digest compares each decoded material's name bytes and serialized
record size, scoped by model path. An absent name is empty; version 4 uses the
complete chunk size and version 3 uses its material record size. Mesh and
non-material chunk contents are not covered by this digest. The digest for
this install is `8dd71a018c9b5c20c8d123e44e3e2c5e7305f0429ca8affd92553dbb8de193e9`.

## This install against the plan

`PLAN.md` quotes counts from a larger install. This machine has a smaller one, so the plan's numbers cannot be reproduced here.

| Item | Plan | This machine |
|---|---:|---:|
| RPK archives | 147 | 80 |
| Class files | 2,921 | 1,501 |
| SCX models | 6,032 (non-empty) | 3,028 |
| SCY models | 35 (SLRR_rev installation.md) | 21 |
| SDAT files | 139 (SLRR_rev vehicle-serialization.md) | 11 |

The class files on this machine are all one compiler version, `0x11ae1`. SLRR_rev lists two more, `0x24f5c` and `0x24f9d`, which come from the larger install. Nothing here tests those versions.

## Class files (TUFA)

Loader `src/tufa.cpp`, independent reader `tools/tufa_census.py`, unit test `tests/tufa_test.cpp`. The layout was checked against the stock executable's class chunk dispatcher `FUN_00411b20`, its constant pool reader `FUN_00407660`, and the size method of each record type.

- A file is a run of classes. Each class is the chunks `TUFA`, `CONS`, optionally `FILD`, optionally `MTHD`, `CLSS`, `TREE`.
- `FILD` is two lists of 16-byte records. SLRR_rev does not read it. This install has 3,531 field records.
- `MTHD` is two lists of 20-byte records. 3,591 records.
- The stock constant reader accepts kinds 0 to 5 and 7. It sends kind 6 to its unknown-kind error, although the SLRR_rev notes list kind 6. The corpus has no kind 6.
- Tree node width comes from a table at `0x005f0914` in the executable. For ops `0x00` to `0x2e` it matches SLRR_rev's list of 28 wide ops. Past `0x2e` the table runs into other data, so the loader rejects those ops. The highest op in the corpus is `0x2d`.
- Totals: 1,546 classes in 1,500 files, 102,711 constants, 5,816 trees, 320,522 tree nodes.
- One file does not load, `system/Scripts/render/osd/Dialog.class`. It is dated 2004, it is compiler version `0x108f5`, and its constant table has an entry the stock reader also rejects. The game's real dialogs are the 12 files in `osd/dialog/`, and all of those load. You decided to report it as a known stale file. The census names that exact path and fails if any other file fails or that count changes.

Not done. This is the container only. The meaning of the tree ops belongs to phase 3.

## SCY models

Loader: the SCX loader in `src/scx.cpp`. Independent reader `tools/scy_census.py`.

The stock executable loads `.scy` with its own class (`CRT_MESH_SCY`, vtable `0x005f2ce8`) and its own function `FUN_00503b00`. That function reads only the materials and skips 8 bytes after each one without reading them. For an SCX mesh those 8 bytes are the vertex count and the triangle count.

All 21 files are version 3 and hold 449 materials in total. Every skipped tail is zero, so the SCX loader and the stock SCY loader read the same thing. The C++ census fails if a `.scy` file ever holds a vertex or a triangle.

## SDAT saves

Loader `src/sdat.cpp`, independent reader `tools/sdat_census.py`, unit test `tests/sdat_test.cpp`. Checked against the stock file open routine `FUN_00485440` and its resource list reader `FUN_00485190`.

- The header is 8 bytes. The stock routine reads only the type byte. Our loader also checks the magic and the version.
- This install has 11 SDAT files. All are type 2. 3 sit under `save/` and 8 are cars, plus one script, `DestructionTools/Scripts/Muscle`.
- Type 1 has no real file here. The unit test covers it with a built file, and nothing proves it against the game.
- 156 unsaved-resource records, all kind 7 and all empty, and 61 pack records.
- The stock reader reads a payload only for kind 7. A record of any other kind with a nonzero size would put it out of step, so the loader rejects that.

Not done. This is the container only. The fields inside the payload belong to whichever Java class saves them, so they come in phase 3.

## Open gaps

- The corpus has no type 1 SDAT, no class file of another compiler version, and no SCY of version 1, 2 or 4.
- The stock tree op table was read for ops `0x00` to `0x2e` only.
- None of this has run inside the game. It is a comparison with the executable's code and with a second reader.
