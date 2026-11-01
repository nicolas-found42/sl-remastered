# Fresh Stock Executable capture attempt, 2026-11-01

Issue #4 remains incomplete. A fresh built-in-only Wine launch reached an error
dialog before the Main Menu. No fresh menu-to-driving comparison or two-second
throttle observation was obtained. Historical successful captures remain qualified
evidence for their recorded configurations.

All paths below are relative to the original checkout at
`/Users/nicolas/Documents/github/sl-remastered`, not an implementer worktree.
Proprietary data, saves, captures, generated DLLs, and detailed manifests remain local.

## Actual application evidence

The disposable game copy and prefix are
`local/wine-runs/stock-external-20261101T093721Z/{game,prefix}`.
The evidence directory is `local/issue-1/ticket-4-evidence/`.
Its retained preparation, launch, observation, and cleanup scripts describe the exact
attempt. They are one-attempt records, not a replacement acceptance runner.

The game started at `2026-11-01T09:38:06.288911Z` under Wine 11.0.
The launch command used `explorer /desktop=slrr,800x600` with the disposable
`StreetLegal_Redline.exe`, from its writable game directory. The DLL override was
`d3d9,dinput8,mcicda=b;mscoree,mshtml=;winemenubuilder.exe=d`.
All inherited `SLRR_*` and `WINE*` environment variables were removed before the
recorded settings were applied. The prefix was freshly initialized, without a
source-prefix clone. The private game had none of the historical `d3d9.dll`,
`dinput8.dll`, or `mcicda.dll` replacements.

`windows.json` associates window 10429 with game PID 78121, title
`SL2 build 606 (Feb 7 2005 11:40:30)`, and bounds 212 by 152 at desktop position
294,221. The external window capture `stock-refusal.png` shows a red-X modal.
Message and button text did not render. The exact refusal text and cause remain
unknown. `diag.log` records build 606, distribution v2.2.1, but no error explanation.
No keyboard or mouse input was issued. No usable Main Menu, player Car, Chase
Camera, HUD, or control response was observed.

The supervisor timed out after 90 seconds. Cleanup used only this prefix's
`wineserver -k` and `-w`, both returning zero. The launch process returned 1 after
cleanup. The launch, explorer, and game PIDs were absent afterward, and Wine window
inventory was empty. `game-result.json` records 90.547 seconds including cleanup.
This is a failed startup attempt, not a gameplay or performance measurement.
Wineboot logs retain Vulkan, OLE, driver, and menu-builder diagnostics. Those
messages alone do not identify the game's failure.

`hardware.txt` lists only internal APFS devices and no mounted optical media.
The prefix has C: and Z: mappings, with no D: mapping. [INFERENCE] The missing disc
is a plausible startup blocker given the stock disc check and prior synthetic-CD
runs, but the unreadable dialog does not prove that diagnosis.

## Identities and preservation

The Stock Executable and disposable executable both have MD5
`e686099e7233bb7d7c461b1b35c5064c` and SHA-256
`7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`.
The clean extracted 2005 Installed Game supplied the data and its initial save.
The native benchmark fixture was preserved but was not copied into this stock
attempt. Normal stock entry for that fixture has not been established.

`preparation.json` records resolved launcher and wineserver paths and hashes,
`texture.dat`, executable, and native workload identities. The launcher symlink
resolves to `build/tools/wine/wine` inside the existing Wine build.
`game-data-manifest.json` is complete and covers 7,854 diagnostic data files.
`game-initial-full.json` additionally includes executables, logs, and saves.

Canonical path checks rejected overlap between the new run, its parent, the
evidence directory, and all protected roots. Source symlinks were rejected before
copying. APFS clone copy created distinct files; `copy-isolation.json` reports no
shared source/run inodes. Writable permissions applied only to the checked copy.

`preservation-before.json` and `preservation-after.json` contain equal complete
SHA-256 and symlink-target manifests. `preservation-result.json` confirms unchanged
Installed Game contents with 7,859 entries, the whole Reference Folder with 35,851
entries including the supplementary Steam tree, and both native source-save files.
No source fixture, reference data, executable file, or stock Script was modified.

## Comparison limits and remaining acceptance

The intended workload remains the normal Free Ride transition followed by Up down
at driving-ready +0.1 seconds, Up up at +2.1 seconds, and a capture at +2.2 seconds.
It matches the retained native workload's two-second throttle duration. That stock
workload was not executed here because startup never reached a usable menu.

Actual stock game resolution, input receipt, readiness latency, and throttle timing
are unavailable. The configured Wine desktop is not proof of back-buffer size.
The external modal screenshot is not a back-buffer capture. Stock frame counters
are unavailable. Stock random state remains uncontrolled; the native Forced Seed
does not establish a shared seed, Car, camera, or simulation state. No pixel,
frame-by-frame, cross-edition, authentic-disc, or audio Parity claim follows.

Fresh visible menu and controllable driving evidence remain required. Neither
this error capture nor historical proxy-based captures satisfy that fresh workload.
No Port behavior, Native evidence status, or Parity inventory row changed.
The primary verification here was the actual bounded Stock Executable attempt and
full preservation comparison. No production tooling changed and no synthetic
report-only tests were added. The parent integration owns the full project gate.

## Prepared fallback requiring separate review

No fallback was enabled. The following concrete candidate exists only under
`local/issue-1/ticket-4-evidence/review-candidate/`:

- `mcicda.dll` SHA-256 is
  `52e2ac3e940cbac39cfaaf788f2aeb45135c54319c3d2e7ece83f83a159c58e9`.
- Its existing source `tools/wine/synthetic_cd.c` SHA-256 is
  `8b4366579fdbd880b1f82d722aa8987beb8974b070a45ba98d3d46cf59cb6eff`.
- The existing metadata generator `tools/wine/synthetic_metadata.py` SHA-256 is
  `5138d7c8127dbdde5ba83e68831419e4837a4a9e8ce9eb8b762f55ae72f67882`.
- Generated `metadata.txt` SHA-256 is
  `402b32a142f0aad66eb24a5ffd10939eeceeb5a1bd6c222874915c27040bdf5c`.

The candidate was built with the installed `i686-w64-mingw32-gcc` using
`-shared -O2 -Wl,--kill-at`, the existing source, and `-lwinmm`. It was never copied
to the game or loaded. The local `SYNTHETIC-metadata.txt` records every value and
its origin. The table supplies 11 tracks, duplicates track 10 as invented track
11, and invents total duration 4,557,237 ms. It supplies no CD audio.

A separately reviewed attempt could load only this MCI CD driver replacement,
retain Wine's built-in D3D and DirectInput, and use ordinary external input and
window capture. Its `DriverProc` handles CD open, millisecond time format, track
count, track lengths, and total length. This changes in-process disc responses
and therefore requires review despite unchanged executable bytes. The proposal
includes a fresh isolated prefix/game copy, repeated preservation checks, a bounded
launch, no hooks or binary patches, no D3D or DirectInput proxy, and no walk trace.
It is an untested candidate, not a claim that the current startup error is resolved.
Any resulting capture would describe synthetic-disc Wine behavior and measured
external input timing, with disc authenticity, playback, input receipt, rendering,
and random-state limits retained. Review does not itself establish gameplay success.
