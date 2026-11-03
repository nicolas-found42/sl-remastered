# Car prototype implementation

The live car factory is incomplete. The `car` creation paths now call a shared
prototype preparer, which retains actual resource links and builds collision
and wheel prototypes. It does not mark a parsed configuration as a live car.

Stock `005461a0` creates the native prototype on the type-8 script resource.
Stock `0053a2a0` later creates a separate live instance. `ScriptNode::car` retains a partial prototype only after its geometry stage is ready.
The script remains unloaded and no live instance is returned.

`00546070` calls `00536820` to look up and link a resource. It does not decode its
payload. The first implementation decoded default resources too early and
failed on missing `sound/wav/engine/SIKIT.WAV`. That behavior was removed.
Resource demand and decoding belong to subsequent stock loading/use paths.

`00445e00` treats the first material ID as a runtime ID, defaults it to
`system.rpk:3` when omitted, and remaps its two sound IDs through the source
pack. Zero IDs create empty links. `0046bfa0` asks for type 14 with a conditional
type 13 fallback for normal render records; `light` asks directly for type 13.
The configuration reader previously reversed those request types.

The default resource sequence is read from `0046e320` and `00446180` in
`local/census/all.c`. Sound-table operations follow `00442310`, `00442350`, and
`004423e0`. Geometry follows `0046bb80`, `0049ab60`, and `004b5c30`.

`car_runtime_test` executes preparation on the real Baiern DevilSport config.
It checks that an incomplete factory creates no live world node or loaded
script. Its last passing run reached prototype finalization after creating
body and wheel prototypes. The two mesh-mass oracle cases now match all 26
compared words in `local/verify/mesh-mass-oracle/comparison.txt`.

The first application run reached the new loader, exposed the missing default
material flags, and then exposed premature sound decoding. Fresh application
verification after the link-order and geometry corrections reaches
`car prototype finalization and live instance creation unsupported`. Evidence is
`local/verify/native-parity-20261028/free-ride-geometry.stderr`.

Natives, camera configuration, finalization, live instances, physics registration,
and full car dynamics remain unresolved. Nothing here marks a native matched.

## Current detached physical construction

`buildDetachedCarPhysics` centralizes the actual creation path and corpus checks.
It owns the separate part compound, car root and suspension/helper bodies. The
part's designated `bbone` supplies its attachment offset and world pose for wheel
construction, as in0044a250. Wheelbone, hub and a_arm bodies use flags0x8020.
`finalizeCompound` reuses the shared0049a050 mass and bounds calculation for both
part and car roots, retaining core vertices for nested compounds. The original
four mass/finalization cases remain exact after extraction.

All39 complete car configs construct detached physics. The base Chassis.cfg has
no body and the bounded builder rejects it; three Turbo cfg references are
absent. See local/verify/native-parity-20261028/car-physics-corpus.tsv. Resource
shape types6 and7 are loaded, and0049a990 caller mass is separate from resource
mass. Click-specific flags are applied by the click call site. Shared raw-shape
identity and untested resource types remain fidelity gaps.

No game instance succeeds yet. Wheel joints, complete collision/contact paths,
scene objects, script natives and world registration remain required.

## Control and drivetrain step

`CarRuntimeInstance::advanceControls` retains the stock manual shift latch and
shift timers and applies pedal/clutch smoothing. It includes the low-speed
automatic selection branch; the moving automatic branch fails explicitly until
the contact-dependent driven-wheel slip is available. `advanceDrivetrain`
applies the supported starter, low-idle, running-engine, clutch and brake
branches to persistent wheel rotational state. It rejects cuts, nitrous and
damage branches rather than treating them as successful simulation.

Neither method is yet scheduled by the world: the live collision/contact path
must populate wheel velocity and transfer data and provide the current physics
flags and engine parameters. These methods do not establish playable driving.

## Issue #19 transmission reconciliation

Earlier detached-control/world-wiring status above is historical. The current
control path is scheduled by CarWorldStepper, includes moving automatic and
semiautomatic shifts, and now applies stock automatic clutch independently.
See `tools/parity/transmission-evidence.md` for stock anchors, executed Runs,
regressions and unresolved solver/full-gate limits. No Native status is promoted.
