# Career dialog and fixture evidence, 2026-11-01

Issue #6 reuses the Career implementation already present at `8b218d9`.
This change supplies repeatable cancellation, character Escape, normal load,
and cold creation scenarios, plus a background harness mode requested during
verification. It changes no Native or stock Script. The existing
paced `rung2-new-career.txt` remains the positive creation scenario.

## Reproduce at the application boundary

Build `startup_probe`, then use the existing Run recorder. Supply absolute
paths to the Installed Game and Reference Folder; keep outputs and fixtures
outside both. Start with an empty, local save directory:

```sh
mkdir -p local/career-empty-save
SDL_AUDIODRIVER=dummy SLRR_FORCE_SEED=555 SLRR_CAR_TRACE=1 \
  python3 -I tools/parity/run_native.py local/career-create \
  --background --probe build/startup_probe --game "$GAME" --reference-root "$REFERENCE" \
  --save local/career-empty-save --script tools/scripts/rung2-new-career.txt
python3 -I tools/parity/save_snapshot.py \
  local/career-create/data/save local/career-fixture
SDL_AUDIODRIVER=dummy SLRR_FORCE_SEED=555 SLRR_CAR_TRACE=1 \
  python3 -I tools/parity/run_native.py local/career-load \
  --background --probe build/startup_probe --game "$GAME" --reference-root "$REFERENCE" \
  --save local/career-fixture/save --script tools/scripts/rung2-load-career.txt
```

Use a new output directory for each invocation. The load scenario expects one
Career in the fixture and selects its first row. Confirm the LOAD CAREER list,
selected filename, Garage identity/money and empty Car Lot, then compare the
saved `carrer/CareerSix-1/main` bytes with the preserved fixture. Copying the save
alone does not establish entry and does not restore a scene checkpoint.

Run `rung2-cancel-name.txt` and `rung2-character-escape.txt` against the empty
fixture. Repeat `rung2-new-career.txt` against the verified Career fixture for
the duplicate-name case. Use `rung2-new-career-cold.txt` against the empty fixture
for normal intro playback and timed menu entry (default process timeout 120 s).
Observe outcomes below; a zero exit alone is insufficient.

## Authority and identities

Private evidence is retained under this worktree's `local/issue-6/`, including
exact inputs, executable and data identities, source-save snapshots, writable
saves, logs, screenshots, camera telemetry and `report.json`. None is committed.
The original checkout is `/Users/nicolas/Documents/github/sl-remastered`.
The Installed Game is its `.scratch/2005-extract/Program Files/Street Legal
Racing - Redline`; the explicit Reference Folder is its `ref/`. Local links
allow existing read-only corpus checks; application writes use separate Run
roots checked by the existing canonical path guards.

- Stock Executable `ref/raw/StreetLegal_Redline.bak.exe` MD5:
  `e686099e7233bb7d7c461b1b35c5064c`.
- Fresh `startup_probe` SHA-256:
  `e60509fd324b506502b4ba810bcf5d5e5d48157345854761b382db42976284d4`.
- Complete prelaunch game-data manifest SHA-256:
  `5c4fb9d07be180f743c2e07bea01ab7fd70e9b328e09e98bf4ab45c7ce21bcfc`.
- Created and normally reloaded Career file, 10,859 bytes, SHA-256:
  `84b7b57bc16724fcab5654f2d1321f9831a783b1623b679ed9a6f1f0129b6959`.

The unchanged stock `MainMenu.class` tree `1:60`, source lines 917–936,
governs NEW CAREER: nonempty name requester, then character chooser, defaults
and autosave. Its character dialog result is ignored; Escape continues creation.
`StringRequesterDialog.class` tree 2, line 37, rechecks nonempty input.
`PlayerSetupDialog` trees `2:9`/`2:10` in `MainMenu.class` select the portrait
and assign `player.character`. `GameLogic.class` trees 99/101 write/read that
ResourceRef and money; tree 103 initializes a new Career; tree 98 uses the
name/club directory. Local decoded trees are retained with the evidence.
No Steam finding was adopted and no new Stock Executable visual comparison
was performed. Existing Native statuses remain unchanged.

## Actual Runs and observations

The initial visible Runs use unchanged stock data, dummy audio and forced seed
555. Empty-save creation uses stock Config defaults: 800×600 render, fullscreen desktop
1512×949 backing, as recorded in `run.log`. This differs from older 640×480
Runs; no pixel comparison or performance claim follows.

| Directory / Run | Observed result |
|---|---|
| `create` / `20261101T194759Z-32264` | Full `CareerSix` name, next portrait visibly changes, Garage WELCOME with $20,000, empty Car Lot; exit 0, no `err` events. |
| `load` / `20261101T194819Z-32572` | Initial scenario missed the list row; remained in LOAD CAREER despite exit 0. This is failed entry evidence. |
| `load-fixed` / `20261101T194854Z-33121` | Correct row at normalized y=0.335 selects CareerSix-1 and enters Garage normally; same identity, $20,000, empty Car Lot; file re-saves byte-identically. |
| `cancel` / `20261101T194838Z-32898` | Empty OK leaves requester open; typing `Abc` then Cancel returns to Main Menu, with no Career file. |
| `character-escape` / `20261101T194925Z-33581` | Escape from chooser creates complete `Esc-1`, $20,000, default portrait; follows stock ignored-result rule. |
| `duplicate` / `20261101T194911Z-33365` | Fresh process creates the same name over the existing fixture; only the same Career main file remains, byte-identical. |
| `traversal` / `20261101T194934Z-33697` | `../esc` resolves to in-root `save/esc-1/main`, retains literal player name and $20,000, enters Garage. |
| `escape-root` / `20261101T194949Z-33925` | `../../esc` fails with `File.open(write) path escapes data root`; exit 1, seven error/stack events, no files in the Run's data root. |
| `cold` / `20261101T194955Z-34017` | Normal intros play; timed menu route creates the identical Career file and reaches Garage/Car Lot; exit 0, no `err` events. |

Every recorded Run reports `source_save_unchanged: true`. The reusable fixture
was copied from `create` using `save_snapshot.py`, then demonstrated through
normal `load-fixed` entry before reuse for `duplicate`. Fixture manifest
comparison and byte comparison establish preservation of the source files.
The original checkout's older source saves were not used as writable roots.

The stock save sequence plus SDAT header places the selected ResourceRef at
0x10 and money at 0x14; the length-prefixed name begins at 0x24. Independently
decoded saves show `CareerSix`, money 20000 and portrait `0x000100db`, preserved
across load, duplicate and cold creation. Character Escape retains default
portrait `0x000100da`. Empty ownership is corroborated by the Garage welcome,
empty Car Lot image and absence of saved Car files, rather than a process exit.

Screenshot/log pairs for `create` are name frame 217, original/changed portrait
262/310, welcome 403, Garage 459 and Car Lot 626. At frame 343 the same Run logs
`$20000` and `CareerSix racing in Green Slip/20`. For `load-fixed`, list/selection
are frames 64/111, welcome 220, Garage 273, Car Lot 426; identity/money are logged
at frame 93. No player Car exists in these scenes, so no driving Cartrace is
claimed. Jev screened and audited the host screenshot transcription; this
checks consistency of text records, not pixels or Stock Executable Parity.

## Residual differences and checks

In-root dot resolution reuses the existing stock-corroborated file behavior
(`FindFile.first` 00487cb0 → 0054cf50, File.open 00485440 → 0054caf0,
File.exists 00486170 → 0054c170; local Stock Decompile evidence). Refusing
out-of-root names is a deliberate containment deviation, not stock validation.
The existing dangling-symlink refusal is also retained. The scenarios do not
establish complete Windows filename compatibility.

Camera single-target follow and a missing world texture remain explicit in
`gaps.txt`. Garage rendering fidelity, physical input/audio and subsequent
Career Rungs are unverified here. No Native status or full Parity claim is
promoted by these observations.

The fresh build passes. Focused `file_state_unit`, `game_file_unit`,
`native_file_unit` and `probe_script_unit` pass. `tools/check.sh` runs the full
CTest suite: 122/123 pass; the existing `object_world_step_unit` fails its
sleeping-body assertion at line 220. No production code changed in this patch.
The gate therefore does not pass. Remaining checks are recorded separately in
`local/issue-6/check-remainder.log`; the local Native evidence manifest has a
stale SHA-256 for `tests/vm_test.cpp`, retained as a failure rather than rewritten.
All other separately executed remainder commands pass: recorder, screenshot,
path-safety, replay, snapshot and Run tests; Native inventory comparison;
startup trace; corpus checks; and the arm64 executable check. Independent C++
and Python PTX inventories agree on all 127 files. The complete command statuses
are retained in `check-remainder.json`. These results do not turn the failed
full gate into a pass.

## Decision history

2026-11-01: Reuse current Career code after actual creation/load Runs; deliver
missing repeatable scenarios and evidence instead of repeating earlier fixes.
Retain failed load attempt; correct only the list-row input after observing it.
Keep source fixtures local and verify normal entry before reuse. Record all
gate failures and safety deviations. Jev MCP selected this scope with probability
0.97 and no contradicted requirement checks; its judgment is advisory.

The corresponding row was appended to the local `docs/decisions.tsv` and the
Driver selection and profile row of `docs/parity-checklist.tsv` now points to
these Runs. Those inventories are intentionally gitignored. The exact new
decision row is preserved in `career-decision.tsv` for transfer to another
local ledger without publishing the historical evidence corpus.

## Background verification requested by the owner

The owner requested continuing without focus/fullscreen interruptions and
without waiting through intros or the splash. `run_native.py --background`
and `SLRR_PROBE_BACKGROUND=1` now keep the existing Cocoa window hidden and
host-windowed, suppress host cursor changes, and allow its normal frame loop.
Stock Config fields remain unchanged. Startup movies are omitted in this
harness mode; the existing rapid scripts still skip the splash using normal
Escape after its cancel hotkey becomes ready. `tools/check.sh` defaults its
probes to this mode. It does not validate visible fullscreen or cursor behavior.

Trying `SDL_VIDEODRIVER=dummy` first failed explicitly at stock device setup:
`Controller.user_Add: physical input unavailable for device 0 axis 200`.
No synthetic device was substituted. The original probe also rejected the new
`--background` flag with exit 2 before implementation (`background-red`).

`hidden-create` / `20261101T200416Z-46276` and `hidden-load` /
`20261101T200429Z-46353` both exit 0, retain the original Career hash above,
and report unchanged source fixtures. Menu readiness is 0.622 / 0.589 process
seconds. Both log `hidden=1 fullscreen=0 inputFocus=0` and render at 800×600.
Hidden creation welcome/Car Lot captures are frames 420/675; normal hidden load
welcome/Car Lot are 295/548. Images still show CareerSix, $20,000 and an empty
Car Lot. Their executable SHA-256 is
`f5b63c0ba8313e97a0f036a649aaeda92686b9f6a7764d2260f2589dd1342e8b`.
An independent AppKit/CoreGraphics observer sampled 1,395 times during these
Runs: zero foreground probe samples and zero onscreen probe windows. This is
bounded observation, not a guarantee about all future host configurations.

The final build also gives the no-activation hints override priority and
refuses before SDL initialization if they cannot be set; SHA-256 is
`cd9688d8ed523a61cb51344a9af220ca51fe9b4ffd1ea9de917d2bafaefb24c3`.
`tests/native_background_test.py` passes on that build for both flag and
environment entry, verifying real menu readiness, rendered capture, unchanged
fixture, and hidden/windowed/unfocused telemetry. The recorder's four existing
unit tests also pass. The regression is now in `tools/check.sh`.

`check-background.log` records the repeated full gate after this code change:
120/123 CTests pass. Two extra failures (`audio_unit`, `music_unit`) occur
because a globally supplied `SDL_AUDIODRIVER=dummy` prevents their own
`SDL_SetHint` calls from succeeding. Both pass when rerun without that global
override (`audio-env-recheck.log`); the tests select dummy audio themselves.
The existing `object_world_step_unit` failure remains. The separately executed
background remainder (`check-background-remainder-summary.log`) passes every
command except the same stale Native evidence hash. No visible application Run
was launched after the owner's no-focus request. Jev selected the hidden Cocoa
approach with probability 0.96 and no contradicted requirement checks.
After the final hint guard build, `ctest-final.log` records 122/123 passing
without the global audio override; only the same sleeping-body assertion fails.


2026-11-01 handoff at the owner's request: commit and merge unfinished work.
The stale Native evidence identity is now reconciled locally: the recorded
VM test hash exactly matched f090ebb, while integration 8c1c3f3 changed its
null-receiver expectation. The current three VM checks pass and the strict
Native evidence audit reports six matches with zero artifact errors. The
reviewed identity delta is recorded in career-evidence-refresh.json; no Native
status or proprietary artifact was changed.

The existing object_world_step_unit remains unresolved: the box stays supported
at approximately y=0.278 but does not meet the stock sleep gate. Added test
output exposes angular velocity and support activity without changing any
assertion. Experimental cached-contact changes passed isolated geometry tests
but made the original box fall through the floor; those changes and temporary
debug logs are excluded from the merge. Their patch and diagnostic logs are
preserved with the private local evidence for follow-up. This is a partial
handoff, not full issue #6 acceptance or a clean full-suite claim.

Final handoff validation (`resolution/handoff-check.log`) rebuilt the tree and
ran all 123 CTests: 122 pass, with only the original sleeping-body assertion
failing. `tools/check.sh` stops at that failure; later checks were not reached
in this invocation. A separate strict Native audit again reports six matches
and zero artifact errors (`resolution/handoff-audit.log`).

Merge validation against the newer shared main: retained its default-hidden
recorder, non-focusable window flag and explicit no-intro mode while combining
the environment override and cursor/activation guards. The merged probe builds;
the real background regression passes, including environment override of
`--visible`. All four recorder tests pass. A merge-only renderer prerequisite
regression was reproduced and fixed: the constructor/init regression now passes
with the global background policy. These focused checks do not constitute a
new full-suite pass on the combined main.
