# Wheel / suspension / tire drive evidence (issue #18)

Worktree `../slrr-wt-18` (branch `codex/issue-18-wheels`), probe sha256
`2b9babe7c9ab1485...` (full hash in the run's `metadata.json`), save fixture
`local/verify/parity-stock-fresh-save/save` (unchanged, `source_save_unchanged:
true`), game data manifest status complete, seed 235, `SLRR_CAR_TRACE=1`.

Scenario `local/issue-18/wheels-controls.txt` (rapid Free Ride): throttle 0.1-10.5 s,
steer right 6.1-8.1 s and left 8.3-10.3 s at speed, brake 10.6-13.6 s, second
accel/brake burst, stop. Run `local/issue-18/run2` -> recorder dir
`run/20261101T114531Z-4898`, exit 0, 817 ok cartrace steps.

Change in this ticket: cartrace `wh` per-wheel entries gained `sa` (applied
steering angle, rad), `ls` (longitudinal slip), `aa` (slip angle), recorded from
the live wheel state each step (`src/car_world_step.cpp`); `docs/run-log.md`
field list updated. Focused units green: car_world_step, car_wheel_runtime,
car_wheel_geometry, wheel, wheel_tire_force, physics_suspension.

Measured agreement (run2 cartrace-full.jsonl):

- Wheel rotation vs Car motion: during coast at speed > 3 m/s on loaded wheels,
  `abs(w)*0.315 - spd` mean 0.42 m/s, p95 1.07, max 1.11 m/s (rolling without
  gross slip). Under full throttle the driven rear wheels (drive=1, wheels 2/3)
  spin up to `|w| = 117 rad/s` vs rolling 45 rad/s at 14 m/s (longitudinal slip
  up to 2.6) — wheelspin under power, recorded as observed dynamics.
- Steering: front wheels 0/1 `sa` spans -0.762..+0.762 rad with correlation
  0.989 against the sampled steer control; rear wheels `sa` = 0 (steer=0 in
  wheel extras). Heading responds oppositely for left vs right input.
- Suspension: length `l` spans 0.211..0.705 m (settings min 0 / rest=max 0.705
  from stock parse); load `F` spans 0..20103 N. During left steer at ~12.8 m/s the
  right-side wheels unload (F=0, l at 0.705 full droop) while left side carries
  6.5-8.0 kN — lateral load transfer visible.
- Tire response: tire step accepted on loaded contacts (sel `R` with retained
  witness is the documented same-face refresh path); 91 of 2084 loaded wheel-steps
  rejected (4.4%), all with a logged reject reason; airborne wheels (F=0)
  correctly carry no tire force. Slip angle `aa` up to +/-0.72 rad on loaded
  wheels during steering at speed.

Stock corroboration: setter record writes and the 11-coefficient curve /
slip model are the ones compared against the stock executable in
`docs/wheel-comparison.md`; suspension/contact corpora exact-original per
`docs/physics-evidence.md` (suspension 10 cases) and the driverdyn stock
evidence ids in `local/handoff-20261101/driverdyn-final-report.json`.

Residual differences (kept unresolved, not silently accepted): simultaneous-
contact coupled-matrix factorization and complete stock cached feature edgewalk
on convex wheel faces are not implemented (driverdyn runtime notes); no
frame-by-frame stock wheel-trace parity exists, so wheelspin magnitude under
full throttle is recorded as observed, not matched.
