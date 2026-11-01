# Free Ride controls and camera evidence, 2026-11-01

Ticket #5 remains open pending the contact/visual acceptance review below. The
native application now consumes the stock seat camera zoom command, correcting
the cropped close view. This is a stock-2005 camera prerequisite, not a change
to the target edition or a full Parity claim.

## Reproduction and authority

`tools/scripts/free-ride-controls.txt` enters Free Ride through the real Main
Menu with the established zero-offset readiness prefix. It captures the loaded
Car, accelerates, steers left, releases throttle, holds the brake, then releases
it. The final observation is retained even when it exposes a failure. The
legacy cold-start scenario remains covered by the existing ticket #3 Runs.

Private Runs and `report.py` are retained at the original checkout's
`local/issue-1/ticket-5-evidence/`. Each directory contains exact scenario,
executable SHA-256, game-data manifest, copied initial save, metadata, result,
input events, screenshots and full Cartrace. `report.py` pairs each saved frame
with its final physics step, rather than the first of several steps in a frame.
Proprietary inputs and generated images remain local.

The Installed Game remains `.scratch/2005-extract/Program Files/Street Legal
Racing - Redline`; the source save is
`local/rapid-testing/baseline-data/save`, both in the original checkout. All
Runs use separate writable roots, an explicit absolute Reference Folder,
640×480 visible presentation, dummy audio, `SLRR_FORCE_SEED=555` and full
Cartrace. Source-save verification reports unchanged bytes. Game-data manifest
SHA-256 is `5c4fb9d07be180f743c2e07bea01ab7fd70e9b328e09e98bf4ab45c7ce21bcfc`.
The independently re-read Stock Executable MD5 is
`e686099e7233bb7d7c461b1b35c5064c`. No Steam behavior was substituted.

Stock Decompile `FUN_004386a0` (local `all.c:46091-46101`) advances camera zoom
as `(target-current)*speed*dt+current`, retaining float grouping without a new
factor clamp. `FUN_00438b60` (`all.c:46748-46760`) publishes it to the render
camera FOV. Previously the Port recorded zoom commands but never advanced or
published them. The real seat pose event in `camera-observe` establishes
current FOV 34.38 and target 75.00; this is not a hypothesis based on framing.
The original stock Scripts and configuration are unchanged.

## Executed behavior

`baseline-controls` exits zero after 15.85 process seconds, but its Car is large
and cropped during driving. `camera-observe` isolates the missing zoom state.
`zoom-controls` repeats the exact controls scenario after the zoom fix, exits
zero after 15.55 seconds and retains all six captures. Its camera trace begins
at FOV 36.8125 and reaches 74.9999 against the actual target 75. The steering
and stopping captures show the whole Car and a view of the road ahead.

For `zoom-controls`, screenshot/physics pairs are:

| Capture | Frame / final tick | Speed m/s | Controls and observed result |
|---|---:|---:|---|
| ready | 97 / 5 | 0 | Normal City entry with Emer Nonus Street GT |
| accelerating | 126 / 176 | 10.45 | Throttle 1, gear 2; position advances along the road |
| steering | 131 / 206 | 10.91 | Steering 1; visible heading change, HUD 039 km/h / gear 2 |
| braking | 158 / 368 | 0.21 | Throttle 0, brake 1; Car slows near its stopping position |
| stopping | 183 / 518 | 0.45 | Brake remains 1; low-speed movement remains |
| stopped | 200 / 620 | 0.44 | Brake released, gear 1; residual movement remains |

The 620-step trace reaches 11.01 m/s, has a maximum within-step speed gain of
0.42 m/s and does not fall below its starting height. The Car's total movement,
control transitions, wheel contacts and HUD speed/gear support functional
acceleration, steering and braking. Residual speed must be distinguished from
stock idle creep and contact jitter before claiming a stable stop. A low speed
or a still image alone does not establish the stop criterion.

The final unchanged scenario is `final-controls`: exit 0, 15.77 process seconds,
source save unchanged, 632 physics steps, maximum speed 11.27 m/s and maximum
within-step gain 0.42 m/s. Captures pair to final physics rows as follows:

| Capture | Frame / final tick | Speed m/s |
|---|---:|---:|
| ready | 100 / 3 | 0.00 |
| accelerating | 131 / 170 | 10.67 |
| steering | 136 / 200 | 11.18 |
| braking | 164 / 368 | 0.27 |
| stopping | 190 / 524 | 0.35 |
| stopped | 208 / 632 | 0.00 |

The final 20 consecutive steps have speed zero, sleeping state 1 and fixed
position after controls are released. No sleep gate or physics number changed.
This demonstrates a sustained stop in this Run; the earlier same-input Run's
residual motion remains recorded, and repeatability beyond these Runs is not
assumed. The steering image shows HUD `040` km/h in gear 2; HUD updates precede
the frame's physics steps, so the final Cartrace row may already show the next
gear. The final image shows HUD `000` in gear 1. Camera FOV again approaches
74.9999 with the full Car and road ahead visible.

The final correction-attribution trace has 17 capped world steps. Only tick 38
has any primary-Car candidate, and that candidate is present only in the first
pass and receives no direct correction. No capped step has a nonzero primary
correction in any recorded slot or its final pass. Fifteen have nonplayer direct
corrections in their final pass; two end with zero corrections on pass 20.
Thus count 20 alone includes a naturally terminating final pass, and the
observed caps are not evidence of a player-car solver failure in this Run.
All world caps remain tagged and rejected by the strict checker. This does not
settle loose-object contact Parity or prove wall collisions.

## Acceptance limits and diagnostics

The corrected `cartrace_check.py` rejects these Runs for `iteration_cap`.
The previous checker read `tags` while production emits `anom`; it also let an
empty trace, a nonfinite speed and a first-step speed jump pass. Five file/CLI
regressions reproduce those failures on the old checker and pass after repair.
Earlier pass claims are corrected in the rapid Run and benchmark evidence.

Contact diagnostics previously described the selected player Car while
aggregating contacts from every root, including loose objects far from it.
`col.player_n` and `col.player_imp` now attribute candidates and impulses to the
primary Car's compound. `player_corrected` and `world_corrected` record actual
direct-corrector nonzero returns for each pass. As with the prior arrays, slots
0–6 describe the first seven passes and slot 7 is overwritten with the latest
pass. A candidate or an old accumulated impulse is not evidence that a
correction was applied on that pass. Constraint-row batch forces are separate
from these direct-corrector counts.

The earlier `zoom-controls` Run has 22 capped world steps, four containing
primary-Car candidates. This does not establish that the player caused the
cap. Stock `FUN_004a5190` (`all.c:121096-121150`) itself has the count-20
successful-commit escape. Its existence does not prove the Port's frequency or
contact lifecycle matches stock. Caps remain visible and the strict checker
remains red while their relevance is investigated. The known independent
`object_world_step_unit` true-sleep failure is not weakened or hidden.

Remaining explicit Gaps include incomplete collision traversal/special body
pairs and wheel feature paths, camera collision/stock framing comparison,
full HUD/damage fidelity, driver rendering and time-of-day comparison. The final `gaps.txt` explicitly
retains unsupported mesh directives, HUD damage-color/glyph refresh, incomplete
spatial factory collision coverage, special-body pairs and tire rejection when
wheel prediction changes. The final Run has one `err` line: the mirrored stderr car-creation request
from the wrapper-enabled `SLRR_TRACE_CAR_CREATION=1` diagnostic. Its source
`src/game_creation.cpp` also emits the same request as a Note and explicitly
explains the stderr mirror. The Car is subsequently created and driven; this
line is not a refused creation. No other `err` line occurs. The
six-digit `000000` is the odometer, and `x1000` is the tachometer unit caption;
they are not evidence of frozen speed or 1000 RPM. Physical keyboard/audio
support and Stock Executable frame comparisons are outside these scripted
Runs. No Native evidence status or broad Parity status is promoted.

The native probe builds. Targeted camera factory, CarWorldStepper and multi-root
physics CTests pass, as do the five checker regressions. The parent integration
owns the full repository gate; this work does not report the known baseline
119/120 result as wholly green.
