# Wheel variable refresh and setter aliases, 2026-11-02

Status: production pre-script reset and shared17-value tyre state implemented; focused Port controls pass. Full drive/Journey parity is unverified. The parent owns refresh dispatch and post-script configuration; this slice did not link startup_probe or run a game capture.

## Reference evidence

Older448430 clears six fields at wheel+15c/160/164/168/16c/170, then copies0x44bytes prototype+2e50 to wheel+1e8. Config wheelpars overrides slots0,2,4,11..16; see historical all.c56983..57045,53009 and56214. Existing older native registrations52733..52747 and docs/wheel-comparison.md bind aliases correctly: Sliction0, Friction2, Stiffness4, MaxLoad11, Frictn_x12, LoadSmooth14. The initial investigation message swapped MaxLoad/Frictn_x/LoadSmooth handler labels; that was corrected before changing production, and existing pure Wheel aliases/assertions were retained.

Fresh newer REA evidence under the unchanged registered8MiB root local/verify/issue53-texture-rea-20261102:

- wheel-refresh-newer-v1: exact115-byte reset/copy block4aece0,68-byte default table5da8b0, exact registration/name ranges, and seven selected tiny native setter instruction bodies. Holder66187 automatically queried/retained/closed/released exit0 within180seconds; exact sequence timestamps retained.
- wheel-refresh-review-v1: all payloads PASS except registration and second name range REVIEW. Manual review found only fixed code/string bytes and provider metadata boilerplate; no directive followed, no SKIP or reroll.
- wheel-refresh-decoder-v1: all five raw ranges equal immutable PE bytes, source SHA checked before/after, bounded LLVM supplements; result0. Seven instruction-body records independently bind the same newer subject SHA.

Newer SHA256 f44fb88a4335c657e20701cdb84afb8d1d85f7c3aceb93d30113e6e6e44fefc2. Reset block evidence ev_b0ff2720995a6fc0045a9f72aa465d417358c69ea99ca3dde439b4685bf4a800 zeros all six fields and copies17Float32 values from prototype+2ec4 to wheel+1e8. Default table evidence ev_9079b9007b1529a3cdb0042c55eda08c3ac2ae4d66cdcc9a32f4a67d4fab3e55 is:

`1.4,0,1.49,0,15.2,0,-0.0,0,-1,0,0,8000,1,0.015,0.4,0,0`.

Only index6 differs in bits from the older default table60e068: newer uses negative zero. The implementation preserves that newer value. Newer setter4b6a90 (ev_75a42bfd4ed038d4b002da58cf141256f31e292fc16983a5e8f143b58b746544) accepts indices0..16 and writes wheel+1e8+4*index. Named bodies4b6fd0/4b6420/4b7050/4b6950/4b6500/4b6870 preserve the same alias map. Friction writes coefficient2 and four separate derived values at64/68/6c/70; later indexed writes must not recompute those derived values.

## Production behavior

CarWheelExtraState now owns one canonical Wheel tire. All seven public WheelRef setters update it, and CarWorldStep consumes that actual object directly rather than copying16 coefficients and overwriting indexed writes from independent named fields. The existing pure Wheel setter implementations and their numeric aliases remain unchanged.

resetCarWheelVariables(CarRuntimeInstance&,const CarConfig&,string&) lives in car_runtime.cpp. The parent dispatch calls it after suspend_update passes and before the actual script callback. It restores17 prototype coefficients, clears frictionScale(+15c), frictionWearRate(+160), existing brakeThermal wear/rate(+164/168), and offset-named16c/170 values. It preserves derived friction, brake temperature, drive factor, radius and other state outside this reset block. Missing configured wheel records produce an explicit error before mutation. A zero-wheel fixture legitimately resets no wheel records.

The former host initial named defaults are retained by CarWheelExtraState's constructor in the canonical Wheel representation. This is compatibility before the first refresh, not a new claim about Stock creation defaults; qualified prototype defaults are installed at refresh.

## Controls and retained failures

All retained runs are under local/verify/issue53-reconciliation-20261102:

- wheel-native-alias-red-v1: preserved compile failure from an incorrect shared_ptr fixture assignment, corrected to unique ownership.
- wheel-native-alias-red-v2: build0, expected/actual CTest8, fails17-coefficient assertion against the old16-value production representation.
- wheel-refresh-final-v1/v2: build0,4/5 pass; unrelated new registry callback fixture first lacks class initialization then remove() method. Its owner corrected the fixture; production allocation/lifecycle checks were not relaxed.
- wheel-refresh-final-v3: reaches wheel fixture but setters correctly ignore a stack receiver not owned by VmArena. The fixture was corrected to an arena-owned reference; no setter assertion was weakened.
- wheel-refresh-final-v4: build0,5/5 pass (wheel,game_type_native,car_runtime,car_wheel_runtime,car_world_step); all ten recorded source hashes match before/after, including parent dispatch/application integration.

The runtime fixture exercises actual public native setters, both alias orders, coefficient16 influencing calculated loaded tyre force, preservation of derived friction after indexed coefficient2 writes, all17 configured defaults including negativezero, six cleared reset values, preservation of unrelated state, and a post-reset script setter replacing defaults. The parent's actual VM callback/suspend controls also execute in car_runtime_unit. Pure Wheel existing alias/bounds assertions remain intact.

Further source-restoring reset fault controls were deferred while the parent and physics agent finish the actual load/typed-error path; they are not claimed as executed. Root owns full integration gate and game captures.

## Remaining behavior

This is a reset/alias correctness slice. Tyre wear accumulation and the16c/170 pair's full simulation effects are not newly implemented. Existing brake wear and friction scaling have actual consumers; previously unmodeled rates/pair remain explicitly offset-named until the native Part post-refresh contribution callback is integrated. Indexed parameter preservation does not establish complete tyre-force parity. The parent-owned larger gaps include post-script dyno/config/aero copying and approximate DynoData.calcDyno. No full US17/US29 or release acceptance is asserted.
