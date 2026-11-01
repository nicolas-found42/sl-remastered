# Stock Executable capture evidence, 2026-11-01

The fresh MCI-only experiment below reaches the Main Menu, loads Free Ride, and
shows a player Car responding to a measured two-second Up hold. It preserves the
Stock Executable, source data, and fixtures. The configuration uses separately
reviewed synthetic CD metadata and supports only the qualified observations below.
The earlier built-in-only failure remains part of the evidence.

## Earlier built-in-only attempt

At the end of the earlier attempt, issue #4 remained incomplete. A fresh built-in-only Wine launch reached an error
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

## Candidate prepared after the earlier failure

The earlier attempt did not enable a fallback. It prepared this candidate under
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


## Fresh MCI-only experiment

A separate parent review authorized the exact driver and metadata listed above for
one isolated runtime experiment. Its retained text is
`local/issue-1/ticket-4-mci-evidence/exact-artifact-review.md`.
The review did not authorize D3D or DirectInput proxies, walk detours, binary
patches, or other hooks. None were used.

The fresh private copy and prefix are
`local/wine-runs/stock-mci-only-20261101T094733Z/{game,prefix}`.
All observations, commands, input helpers, manifests, and proprietary captures remain
in `local/issue-1/ticket-4-mci-evidence/` in the original checkout. The previous
`ticket-4-evidence/` directory and its failure report remain unchanged.

`prepare-stock4.py` starts from the clean extracted 2005 Installed Game, rejects
source symlinks and overlapping paths, creates a distinct APFS clone copy, and
replaces only the private executable with the Stock Executable. The source save is
the clean extraction's save state. The preserved native fixture was not copied.
`copy-isolation.json` confirms no shared source/run inodes.

The private executable has MD5 `e686099e7233bb7d7c461b1b35c5064c` and SHA-256
`7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`.
The exact reviewed `mcicda.dll` and `metadata.txt` hashes match the candidate above.
`preparation.json` also identifies the driver source, generator, texture data,
resolved Wine launcher, wineserver, native workload, and external helpers.
`game-data-manifest.json` covers all 7,854 diagnostic data files.
`game-initial-full.json` records the initial copy before the explicit driver addition.
The separately identified driver is the only added game-side executable artifact.

### Configuration and loaded-module evidence

`launch-stock4.py` retains every Wine command, timeout, and result. It removes
inherited `SLRR_*` and `WINE*` variables, initializes a new prefix, maps D: to a new
private empty `synthetic-disc/` directory, and sets that private drive to `cdrom`.
There is no physical disc or CD audio. The prefix, D: mapping, and game copy are private.

The game launched at `2026-11-01T09:49:43.077332Z` with
`explorer /desktop=slrr,800x600` from the private game directory. The overrides were
`d3d9,dinput8=b;mcicda=n;mscoree,mshtml=;winemenubuilder.exe=d`.
`SLRR_SYNTHETIC_CD_METADATA` named the private reviewed metadata through Z:.
`WINEDEBUG=err+all,+loaddll` retained loader evidence.

`game-run.log:162` identifies built-in `d3d9.dll`.
Lines 190-191 identify the private `mcicda.dll` loaded as native, then unloaded.
The log contains no `synthetic-cd` per-query diagnostics. Specific returned values
and successful metadata consumption therefore remain unobserved. The evidence
establishes a successful Stock Run under the reviewed MCI-only configuration, not
a trace of each disc-check response. No private D3D or input DLL existed.
The compiled out-of-process `window-text.exe` helper was prepared but never run.

Wine reports missing Vulkan support and media-splitter registration errors, plus
an undefined D3D state. The captures establish visible menu and driving behavior
despite those messages. They do not establish correct video or audio playback.
`diag.log` records build 606, distribution v2.2.1, and normal exit.

### Observed workload and measured input

`windows-game.json` identifies window 10462, PID 84106, title
`SL2 build 606 (Feb  7 2005 11:40:30)`, and logical bounds 800 by 600 at 0,33.
External `screencapture -l10462` images are 1600 by 1200 pixels. The full HUD is
visible. These are scaled external window images, not back-buffer captures.
`capture-inventory.json` records each capture's hash, dimensions, and completion time.

`stock-start.png` shows the Main Menu. The ordinary external CGEvent helper
activates that verified PID and clicks the visible Free Ride target at desktop
483,189. `menu-click.log` records down/up at monotonic 151490.170365 and
151490.253026. `stock-after-click-delayed.png` shows the loading screen.
`stock-ready-check.png`, completed at `09:50:12.252830Z`, shows the City, a player
Hauler's SuperDuty Extra 750, a usable external camera, minimap, and HUD at `000/N`.
This is an observed readiness upper bound about 29.18 seconds after launch and
12.803 seconds after click release. The exact first ready frame is unavailable.

`throttle-workload.py` captures the stationary player Car again and establishes an
external workload origin at monotonic 151521.114574, UTC `09:50:30.309423Z`.
The requested schedule matches `tools/scripts/free-ride-fast.txt`: Up down at
+0.1 seconds, release at +2.1, and final capture at +2.2.
`throttle-input.log` and `observation-result.json` retain actual post times:

| Event | Seconds after workload origin |
| --- | ---: |
| Up down | 0.154189 |
| Up up | 2.159256 |
| Final capture start | 2.205032 |
| Final capture completion | 2.307159 |

The posted hold lasted 2.005067 seconds. Activation and scheduling added 54.189 ms
to the requested key-down offset and 59.256 ms to key-up. Capture started 5.032 ms
late and took 102.126 ms. The displayed frame's exact presentation time is unknown.
This is measured ordinary input with recorded jitter, not exact input determinism.

The sequence `stock-driving-ready.png`, `stock-throttle-0.6.png`,
`stock-throttle-1.1.png`, `stock-throttle-1.6.png`, and
`stock-driving-throttle.png` shows the player Car moving relative to the City,
wheel smoke, a usable following view, and HUD progression `000/N`, `000/N`,
`010/1st`, `023/1st`, `033/1st`. This observable control response establishes more
than event posting alone. No game-memory input hook or Cartrace was used.
Stock frame counters remain unavailable.

Normal Escape input opens the in-game menu. External clicks then select Exit to
Main Menu, Exit Game, and Yes. The retained exit captures and input logs record
that path. `game-result.json` reports return code zero before cleanup, no timeout,
and 117.748 seconds including cleanup. Private-prefix `wineserver -k` and `-w`
return zero. `windows-after.json` contains no Wine windows.

### Preservation, verification, and claim limits

Complete `preservation-before.json` and `preservation-after.json` match.
`preservation-result.json` confirms unchanged Installed Game contents with 7,859
entries, the whole Reference Folder with 35,851 entries, and both native fixture
files. Source executable identity remains unchanged. No reference, Steam data,
source fixture, stock Script, or Port behavior changed.

The actual changed-path verification is this normal menu-to-driving Run and its
full source-preservation comparison. Only this evidence document changed in the
repository. No production code or synthetic report-only test was added. The parent
integration retains responsibility for the full project gate.

This Run supports qualified Stock menu, loading, player Car, camera, HUD, and
external-input observations under the reviewed MCI-only configuration. Stock random
state is uncontrolled. The player Car, camera, and state are not shared with the
native Forced Seed. The native fixture's Stock normal-entry compatibility remains
unverified. No authentic disc, CD playback, audio, exact timing, back-buffer pixel,
shared-seed, cross-edition, or frame-by-frame Parity claim follows. The earlier
unreadable modal's text and cause remain unknown.
