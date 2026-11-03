# Camera controller reconciliation (issue #53 US09/US28/US32)

The old `camera deactivate does not unlink the camera from stepping` run-log
message was an incorrect hypothesis. Deactivation unlinks the **controller**,
clears the free-camera flag, and leaves camera world registration and unconditional
FOV interpolation intact. Older inline unlink at 0043a932 and newer
0048c0db→00488080 agree. The production native now implements that operation.

Static reference evidence is retained under
`local/verify/issue53-camera-deactivate-20261102/`: snapshots and REA call ledgers
bind exact PE bytes to their SHA-256 identity. The older `.bak.exe` SHA-256 is
`7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`;
newer `ref/version 2-3-1/StreetLegal_Redline.exe` is
`f44fb88a4335c657e20701cdb84afb8d1d85f7c3aceb93d30113e6e6e44fefc2`.
Their embedded versions are 2.2.1.0 and 2.3.1.0; official provenance and Build
606/940 identification remain separate unresolved requirements. REA sessions
were closed; Ghidra lacking a procedure record at an older address is a provider
coverage limitation, not proof of an address mismatch.

Newer frame 004897b0 consumes axes 7/8/10 for follow/manual motion and axes
1–8/10/11 for free mode; it scales manual angular input by FOV and interpolates
FOV in either mode. This now runs through real PlayerInputState consumers in
`GameWorld::advanceCameraControls`, rather than an unused parser or a helper
that substitutes axes. Unsupported controller samples fail with the causal axis
and input diagnostic before publishing any camera's frame state.

Newer physical callback 0048a430 applies roll, manual local accumulators,
and height before its mode branch 0048a73d. Free mode applies world `-velocity`
only when squared speed exceeds 0.001, then jumps to 0048bb39, skipping automatic
follow and pitch limiting. Camera-local manual force and torque rotate via the
camera basis. `cameraFollowForce`, the isolated world seam, and the production
GameWorld/multi-root callback now support free cameras without follow links.
Height uses the look reference's y, or zero if no look reference exists, per
0048a63d–0048a6a8.

Activation 0048c017–0048c0c6 binds the controller, sets the free flag, unlinks both
follow targets, and resets the render root bone relative to the current physics
reference through 004ec540/004e84c0. It preserves the body's world pose. The
production command uses the existing physics-bone attachment seam.

The retained red results distinguish missing implementation from acceptance:
`free-force-red.json` records exit 18 from rejecting free mode;
`free-callback-and-activate-red.json` records passing physical callback regressions
and two real native activation assertion failures before activation was added.
These are static and unit/world-seam evidence, not paired actual-game parity.
The parent's whole-game Journey is still required. Nonfree single-target follow,
non-direct target variants, and unrepresented plane/angle branches remain explicit
boundaries; none is justified by these tests as complete behavior.

## Actual Career static-ground integration divergence

The subsequent combined Career create failed at frame390 with `multi-root:
unsupported camera callback`. The retained run's camera trace identifies
0xffff0474; run.log1012/1015 sets both targets to131194. Slot2 names
`misc\\garage.rpk`; its same131194 ground factory enters ground_parameters and
body_node at374–386. The binder already resolves that factory's immutable
static body, but multi-root target validation allowed only a stepped root or
asleep body. Ground intentionally belongs to neither category.

`static-ground-red.json` records regression exit42 before the correction.
Validation now additionally permits a valid, unattached static body outside
stepped roots; an awake unregistered dynamic body remains refused without
advancing camera/world state. The ground target's pose, velocity and force stay
unchanged while the camera moves. `static-ground-result.json` binds three
passing focused tests; `static-ground-bindings.json` binds exact source/binary
bytes and the original failing log. This is a causal unit/world-seam repair,
not a claim that the whole Career Journey or paired Stock/Port test has passed.
