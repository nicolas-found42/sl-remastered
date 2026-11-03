# Owned Car persistence investigation

US10 and US13 remain implemented but unverified across their full scope. The
requirement status lives in `full-port-1.0.0-requirements.md`; this note records
the causal evidence and the next boundary.

The first purchase completed with $4,419 remaining after a $15,581 Duhen
purchase. Its 2,239-byte PlayerCar0 contained a zero chassis resource ID. Reload
exited normally but created no Car and rewrote that file as 72 bytes. See
`E-OWNED-RELOAD`; process exit zero did not mean persistence succeeded.

The installed older GameType.class initializes GII_TYPE to integer 6. Part.save
tree 58 calls getInfo with that constant and writes the resulting GameRef;
Part.createFromFile cannot create resource zero. The Port implemented the type
query as 0xe8 instead. Its comment cited the driver's command entry (+0x38),
whereas the native getInfo wrapper 0047ddf0 dispatches through +0x3c. The actual
Part handler is 004769e0 and Car handler 0045c510. Their selector 6 returns the
prototype resource identity at +0xc. Raw REA results are retained under
`local/verify/issue53-career-journeys-20261102/getinfo-rea-*.json`; that initial
investigation's lease expired before close and is not admission-qualified
acceptance evidence. The independently renewed bounded reference investigation
must retain its own completed lifecycle and source binding.

Production now handles selector 6 and preserves zero for a missing identity
instead of inventing a native-device category. Public native-call regressions
using real Baiern Car and door resources fail before the change and pass after
it (`type-query-red-v1`, `type-query-green-v1`). Permanent file-write/type-query
events expose the live ID, prototype ID and saved pack-relative ID.

`E-TYPE-RELOAD` binds a subsequent actual purchase and reload to the same probe
SHA-256, `4c50e3c06d5f1822872d0fbc1de5c6b5687dff3f981f08154699ca40e1928678`.
The purchase writes nonzero prototype IDs. Reload creates the Duhen chassis and
parts, then exits 1 while recursively installing muffler_08 slot 1 onto a
detached Duhen stock exhaust pipe slot 2. Both parts have click-only geometry;
the public subassembly regression reproduces `unsupported compound root` for
the pipe's shape 7 and flags 0x24. Normal dealer construction had installed the
pipe on the Car first, so it did not exercise this detached assembly order.

The next same-build pair (`E-CURSOR-RELOAD`, probe
`8ac0198a8e10243eb24d22ff37b9a0948b935df0bedd1725ea77369d3fdde098`)
passes detached installation after admitting Hull roots, but reload exits 1 at
`cursor mesh triangle index unavailable`. Generic compound finalization cleared
click vertices, retained their triangle indices and replaced the Hull with a
Mesh bound. Stock bounds helper 00498f50 instead skips root shape 7; decoded
498f5d compares 6 and 498f73 jumps to the default epilogue. The retained outer
switch confirms that this differs from handling Hull children inside a Box/Mesh.
The complete admitted decoded session and screening are bound in the audit;
its 49a01b endpoint was an interior address, not return-instruction evidence.
The newer equivalent and actual Stock recursive-load observation remain open.

Production now preserves Hull bounds while retaining mass, recentering and
inertia updates. A real-resource public cursor test picks the same pipe face
before and after muffler installation: post-install picking fails before the
fix and succeeds afterward at the same world contact. Five focused tests pass,
including the malformed cursor-index refusal. An earlier test fixture used the
wrong optional click-body member and is retained separately, not as a product
failure. Next: repeat purchase/save/reload on one current build after the shared
capture finalization fix.
Actual Stock transaction and persistence pairing, complete saved-field semantics,
corruption controls and the remaining lifecycle Journeys are still required.

The next pair (`E-OWNED-CURSOR-FIXED`, probe
`7534205f840150e36dc9963c29703ae8b50cee19a706c12667b4640634a43c1b`)
completes purchase and ordinary reload with exit 0. The raw reload audit checks
normal movies, exact input script, ordered Garage/Exit/Yes/finalizer logs, private
PCM, focus and save lineage. All four save files are byte-identical afterward;
PlayerCar0 remains 2,309 bytes with SHA-256
`5487bcb4da2ff332327decdc40369a14c41f2decab2c5e9993b8a45defdbf5ad`.
The restored scene has one Car and the rendered Garage shows it with $4,419.
Both captures finalize under the corrected prepaid hash path without changing
the five-second deadline or any budget.

This is a bounded native persistence result, not full acceptance. The purchase
frame displays Prestige 90/175 and reload displays 90/139 despite identical save
bytes. Vehicle.getPrestige tree39 calls Chassis.currentPrestige and its race
multiplier; Part.currentPrestige tree67 caches prev_prestige and computes from
attachedParts, native wear/tear and script weights. No cause or fidelity decision
has yet been established. Actual Stock purchase/reload must determine whether
this difference is inherited or a Port defect. Materials and meshes remain
visibly incomplete and are tracked under US29.

`E-SUBTREE-OWNER` records an explicit missing production path: mounting a
preassembled parent registered only that parent with its Car. Native owner
links and repair omitted descendants; detached Part registries were also absent.
Newer source confirms child GameRef callbacks, prior-owner unregister before
preorder registration, and skipping refresh for a self-owned root. Real-resource
regressions fail before each correction and pass after it, including public
remove/reinstall to reach self ownership. Five focused tests pass. The initial
self-refresh fixture started with null ownership and did not reproduce the bug;
its unexpected pass is retained separately. This is not yet the cause of the
observed prestige difference: current-build ordinary reload remains required.
Subtree-preserving removal is a separate explicit gap still being implemented.

`E-SUBTREE-REFRESH` contradicts current persistence: diagnostic reload from the
retained old-build fixture exits0, but 11 installation requests are rejected and
PlayerCar0 shrinks from2309 to1761 bytes. Prestige becomes90/151. The first
rejection is SunStrip front-left strut at Car slot121, flags16, after the new
synchronous updatevariables callback. Static leads show Stock command only
sets a dirty flag; its frame refresh consults the script suspend_update field.
These semantics are being qualified and implemented; current ownership change
is not accepted as a complete persistence fix. The earlier native byte-identical
pair remains historical evidence for its exact prior build.
