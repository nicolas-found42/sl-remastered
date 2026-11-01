# Transmission and clutch evidence — issue #19

Unfinished checkpoint: independent review found that installed-engine
`RPM_maxTorque` is not yet refreshed into `clutchRpm`. The constructor default
5000 is insufficient after stock `updatevariables`; see the follow-up below.

The Port now applies the stock automatic clutch independently of automatic
gear selection. Semiautomatic shifts no longer depend on throttle demand or
the automatic launch-speed threshold. Shift timer expiry follows the stock
controller ordering. This is control/drivetrain evidence, not complete driving
or frame-by-frame Parity.

## Authority and changes

The Stock Executable `ref/raw/StreetLegal_Redline.bak.exe` was read locally;
its MD5 is `e686099e7233bb7d7c461b1b35c5064c`. The copied Stock Decompile
`local/census/all.c` has SHA-256
`abc503a2166b4575df41baf338805c6ca7fba477606b05caa9c7817735ad415d`.
No reference executable or Script was changed.

- `FUN_00458c00`, all.c 66475–66508: transmission bit 0 selects automatic
  gear selection; bit 2 chooses semiautomatic; bit 1 disables automatic
  clutch. Every transmission command seeds automatic clutch history from
  the current clutch pedal. A repeated command also seeds that history.
- `FUN_0044dd70`, all.c 60585–60657: engaged-ratio, positive-mass, running
  engine gating; predicted engine speed; first-ratio/throttle-dependent
  clutch engagement; independent history and release/open rates. Constants
  were read from the identified PE image, including 005f1078 (33.3333359),
  005f107c (200), 005f1080 (0.125663713), 005f0ce8 (0.8), and 005f0ef0
  (RPM-to-radians conversion). The clutch RPM reference retains the stock
  `FUN_00446180` +0x10 default, 5000 RPM.
- `FUN_0044dc30`, all.c 60541–60582: semiautomatic forward-gear changes
  use driven-wheel contact slip, including coasting and slow/backward wheel
  motion. Negative forward-gear RPM selects first; it does not select neutral.
- `FUN_0044e690`, all.c 61287–61320: gear controllers, automatic clutch,
  timer processing, then pedal smoothing. Expired positive timers are cleared
  after the controllers, preserving the stock boundary frame.

The readable Steam Vehicle Script's constants are supplementary only. The
stock `Vehicle.class` and `OptionsDialog.class` were decoded locally to
corroborate the four UI choices and their values: automatic 1, semiautomatic 5,
manual with automatic clutch 0, manual clutch 2. Vehicle trees 20/21 use the
normal Config and command path. The native controller authority above is the
stock executable, not the Steam edition.

The existing physics exchange still consumes the real clutch and gear ratio;
the existing HUD still consumes `currentGear`. No separate HUD gear is set.
Gear changes emit cause/timer values once. Cartrace additionally retains ratio,
driven speed, displayed engine speed, and automatic clutch history.

## Checks and application Runs

Private evidence is under `local/issue-19/` in this worktree. Each `final-*`
Run records the executable/data/save identities, exact script, private save
copy, logs, screenshots and complete Cartrace. Executable SHA-256:
`b2c1d3ec30b37534498bbd36ee48b4519813003a430c8368078c30ec373723df`.

The source fixture is the original checkout's
`local/rapid-testing/baseline-data/save`, loaded/copied through the existing
Run recorder and normal splash/Main Menu/Free Ride entry. The Installed Game
is its `.scratch/2005-extract/Program Files/Street Legal Racing - Redline`.
Each Run preserves its source save. Reference access stays read-only; outputs
remain under `local/issue-19/`. Symlink/path refusal tests pass.

`tools/scripts/transmission-{automatic,semiautomatic,manual-auto-clutch,manual-clutch}.txt`
choose modes through the stock Options dialog, enter Free Ride normally, and
exercise the configured Up/Down, A/Z shift, and C clutch bindings. Held and
repeated shift requests, neutral, reverse, clutch hold/release and braking are
retained in the exact scripts. These are scripted bindings, not physical
keyboard-device acceptance.

| Run | Complete driving rows | Maximum m/s | Solver caps | Exit / source fixture |
|---|---:|---:|---:|---|
| automatic | 432 | 7.16 | 16 | 0 / unchanged |
| semiautomatic | 713 | 11.30 | 27 | 0 / unchanged |
| manual-auto-clutch | 784 | 11.99 | 33 | 0 / unchanged |
| manual-clutch | 796 | 11.59 | 31 | 0 / unchanged |

All final rows are valid and untruncated. All four modes observe gears 0, 1,
2 and 7 (reverse), and actual chassis motion. `final-summary.json` pairs each
screenshot frame with its Cartrace control/gear/motion values and retains
gear-transition logs. `mode.bmp` is a capture of the settings interaction,
not independent proof of the selected runtime mode; use the recorded native
mode and car state. Earlier trial Runs are retained separately, including
truncated rows before the existing #16 trace-buffer correction was reused.

`live-mode-change` enters the in-game menu with Escape, opens Options and
Game Options, changes transmission 1 to 5 on the same Car in gear 1, continues,
and drives again (mode-5 maximum 9.86 m/s). Its screenshot `resumed.bmp` shows
gear 2 and 030 km/h; the corresponding trace/log is retained. Earlier Escape checks incorrectly inferred dialog
presentation from final task snapshots. A longer Run captures the actual
in-game menu and settings flow. No hotkey fix was needed; temporary logs
were removed.

The new automatic-clutch regression fails when its controller block is
removed (`runtime-red.log`), and passes after restoration (`runtime-green.log`).
`focused-final.log` passes Car runtime and real native command checks, including
mode-history changes while in gear. Additional semiautomatic cases cover
coasting upshift and slow backward wheel motion. Existing drivetrain tests
verify disconnected clutch versus driven-wheel exchange.

The final full build succeeds. `check-final.log` records 122/123 CTests passing;
`object_world_step_unit` still fails its existing true-sleep assertion at line
220. `object-sleep-red.log` reproduces it independently in 0.29 seconds.
No sleep threshold or test was weakened. The full gate therefore does not pass.

Remaining gate checks were run separately (`check-remainder.log`): Native
registry, Python integration/path/Run/snapshot/replay checks, startup and stock
handler checks, format and independent PTX comparisons pass; `sl_port` is arm64.
The driver's initial PTX invocation incorrectly treated shell redirection as
arguments; rerunning both readers with real redirection and `cmp` passes.

`native-evidence-final.log` still rejects the historical SHA-256 for unchanged
`tests/vm_test.cpp`. The two absent decompile artifacts were copied to local
analysis storage with their original hashes; the remaining historical hash
was not rewritten. No Native is promoted to matched by this work.

## Background presentation and remaining limits

Reuse of the existing #16 launcher changes provides hidden/nonfocusable
presentation, the macOS background-app hint before SDL initialization, no
fullscreen transition, and explicit native-intro bypass. Crash and screenshot
integration tests use that same route. Visible mode remains explicit.
The 4096-byte trace buffer and presentation identity in Run summaries are
also reused from #16. No alternative launcher or state constructor was added.

`foreground-check.txt` independently sampled AppKit's foreground application
1084 times across the four final Runs: zero samples had `startup_probe` in the
foreground. Run logs report hidden=1, nonfocusable=1, fullscreen=0. This does
not test fullscreen, native intro, physical audio, or physical input Parity.

Solver iteration caps remain in every final Run (table above), with larger
vertical motion during the extended brake/reverse scenarios. Strict Cartrace
acceptance is therefore unresolved; successful process exits do not erase
those diagnostics. Comparable stock application behavior for these extended
scenarios, complete collision fidelity, and the baseline sleep failure remain
open. This evidence establishes the executed transmission paths and local
regressions, not full application Parity or a green project gate.

## Sleep-failure diagnostic follow-up

The existing sleep failure has a 0.29-second unattended CTest reproduction.
A temporary test-side experiment clearing only the ground body's predicted
flags changed the final speed from 0.173 to 0.153 m/s but remained red
(`object-sleep-staticflag2.log`). The first attempt did not compile; its stale
CTest output was discarded. The corrected driver requires a successful build
and restores the pristine test in a `finally` block. No physics flag change
was retained.

Late-step instrumentation (`object-sleep-instrument1.log`) shows residual force
activity and angular motion above the stock commit thresholds; the test is not
merely missing propagation of an already-satisfied sleep decision. These are
observations, not a proved contact-lifecycle root cause. The debug lines and
test mutations were removed. No solver threshold or assertion was relaxed.

## Final regression and artifact reconciliation

`regression-fault-checks.log` confirms that independently omitting mode-history
reset and restoring the former semiautomatic throttle gate each makes its new
regression fail. The restored production code passes both focused tests
(`regression-restored-test.log`). The restored executable SHA-256 equals the
four final application Runs' executable identity exactly.

After the earlier Native-integrity refusal above, the current VM and native
dispatch tests were executed again and passed (`native-artifact-source-tests.log`).
The current-source artifact checksum was reconciled, retaining the prior
checksum in `previous_sha256` and the complete earlier manifest in private
analysis storage. `native-evidence-reconciled.log` now reports six existing
matched entries and zero artifact errors. This corrects current artifact
identity; it does not promote any Native or attest proprietary decompile origin.
The append-only decision record retains both identities. The baseline sleep
failure and strict collision acceptance remain unresolved, so issue #19 is
not claimed fully resolved here.

`runtime-boundaries.log` additionally passes the stock clutch overrun branch,
invalid first-ratio refusal without a partial update, held input at the top
gear boundary, direction changes without release, repeated downshifts through
neutral/reverse, the reverse lower boundary, and reverse-to-neutral-to-first.
These supplement the actual application scenarios and are not new Parity
claims.

## Independent review: unresolved installed-engine refresh

A stronger independent review found a concrete P2 gap: `clutchRpm` remains
5000, while stock `FUN_00447b30` copies DynoData `RPM_maxTorque` (+0x1c0)
into prototype +0x10 after the Script's `updatevariables` callback
(all.c 57152; field identification 79497). Without dynodata it writes zero
(57117). The reachable Port seam is the `updatevariables` native command in
`src/natives/game_type.cpp`; slot 0x191 selects the installed engine, with
chassis-owned dynodata fallback only when that slot does not exist.

This lifecycle wiring and a fractional-engagement regression using a peak
other than 5000 are not implemented in this checkpoint. The existing dyno
calculation is approximate; this review does not establish its stock Parity.
The reviewer otherwise found the inspected clutch branches, command-history
reset and timer ordering consistent with their cited stock routines.

Jev's checkpoint gate verifies all five factual evidence claims, but escalates
the patch (safe_to_apply 0.18, test-gap confidence 0.43). Together with the
engine-refresh finding, sleeping-box failure and recorded solver caps, this
precludes claiming #19 complete. The user requested committing/merging the
unfinished checkpoint with these errors stated in the commit message.
