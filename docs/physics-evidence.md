# Physics engine core: what is read, what is ported, what is not

Plain words first. Street Legal Racing has its own physics engine; the executable has no ODE or Bullet strings. The engine
is event-driven: every frame it adds up forces on each body, moves each body forward with a four-stage Runge-Kutta
integrator (RK4), checks the moved bodies for contact, and only when nothing collides does it "commit" the new state. This
page records the first slices of that engine that were checked against the stock executable, with the addresses, so the
size of the rest is a measured number. The code is `src/physics_body.hpp` and `src/physics_body.cpp`; the test is
`tests/physics_body_test.cpp`.

## Evidence sources and how they were checked

* The reference executable is `ref/raw/StreetLegal_Redline.bak.exe`, MD5 `e686099e7233bb7d7c461b1b35c5064c` (checked with
  `md5` at the start of this work).
* The Ghidra MCP server was not reachable in this session (no Ghidra window was open), so `get_program_info` and
  `read_memory` could not be used. Replacements, all read-only on the same MD5:
  * Decompiles come from the saved whole-program decompile `local/census/all.c` (and the saved `DecompileAt-*.log`
    files for the addresses that have one). No new decompile was run, so no address was decompiled twice.
  * Constants and data bytes were read straight from the executable file by walking its PE section table (a small script
    in the session scratch folder, not committed). Each constant below lists the address it was read from.
  * Where the decompile hides a `this` pointer or an argument (the old compiler passes `this` in a register, and Ghidra
    drops it from many calls), the disassembly was read with the macOS `objdump -d` on a copy of the executable placed in
    the scratch folder. This is how the exact argument order of the integrator, the force function and the mass-property
    routine were confirmed.
* Nothing here was compared with the running stock game.

## What the stock engine turned out to be

* **World step**: `004a5190(world, dt)`. It accumulates time and returns early below a minimum step of 1e-4 s
  (`005f09d4`), then: a force pass over all bodies (`004a3f90`); a pass over the list at world `+0x9c` (virtual call `+0xc`,
  a negative result removes the item); the integration of all bodies (`004a3bc0`, which calls the integrator `004a7c10`);
  a pass over the list at `+0xb8` (virtual call `+0x18`, zero removes the item); then a loop that ends after at most 20
  passes (`0x13 < count`). Each pass runs: gather and solve step `004a3900` (list `+0xb8`), contact update `004a2d10`
  (contact list `+0x118`), scene bounding update `00498dd0`, event dispatch `004a2540`, contact state transitions `004a35d0`,
  and integrates again (bodies already stepped are skipped by their step flag). When a pass finds nothing the step ends with
  `004a2b90` (drop stale pairs) and the commit `004a4ed0` for each body, adds `dt` to the world time and returns 1.
* **Integrator** (`004a7c10`): classic RK4 on a 0xd4-byte state record. Position and velocity use weights 1/6, 1/3, 1/3, 1/6
  (immediates `3e2aaaab`, `3eaaaaab`). Force, torque and angular velocity are constant over the step in stock (the derivative
  slots for them are never written), so the position update is exact for a constant force. Rotation is integrated as a
  quaternion (`dq = 0.5 * omega * q`), angular momentum gets a damping factor, then the quaternion is normalised, the rotation
  matrix and the world inverse inertia are rebuilt, `omega = invInertiaWorld * L` is clamped, and linear speed is clamped to
  150.
* **Bodies are double buffered**: a body holds two state records and a `current` index (`+0x20c`). Forces go into
  `state[current]`, the step writes `state[current ^ 1]`, and the commit flips the index and clears the accumulators.
* **Gravity** is a world vector `(0, -9.81, 0)` (`004a5580`, bits `c11cf5c3`); force on a body is `mass * g`. Linear drag is a
  force `mass * -0.005 * velocity`; angular drag multiplies the angular momentum by `1 - dt` (derived below).
* **Collision**: no ordinary broad phase for bodies was found in the step. Pair distance is `004af970`, a closest-feature
  routine (it looks like V-Clip: features are vertices, edges and faces of convex polyhedra) over per-shape cores built by
  `004a5e80`: a sphere is one vertex (`004ac370`), the cylinders are a segment (`004ac440`), a box is an 8-vertex polyhedron
  (`004ac1c0`), a mesh is `004ac150`. The skin radius (`shape +0x10`) is added by the caller `004a2d10`, which also does
  a time bisection (up to 10 halvings) to find the first moment of contact.
* **Solver**: only the assembly step was read. `004a3900` walks the constraint list at world `+0xb8`; for each constraint with a
  nonzero row count (`+0x1c`) it calls virtual slots `+0x1c` and `+0x20`, sizes dense vectors and a matrix for the total row
  count (`00552a40`, `00552460`), copies each constraint's two row ranges (`+0x20`, `+0x28`) into world `+0x1f8` with `00552d00`,
  and calls virtual slot `+0x24` per constraint. The numeric solve (what turns the matrix into forces) was not found in that
  function; it is inside the virtual slots or a routine they call. The constraint classes' vtables were not located (see
  "Not done"), so whether this is a direct dense solve or an iterative sweep is still open.

## Slice 1: geometry prototypes and mass properties (verified from static reading)

`createBodyProto(mass, type, p4, p5, p6, mesh)` follows `0049ab60`. The decompile of the function is the saved whole-program
log (no separate log). Facts:

| Shape code | Meaning | Radius `+0x10` | Bound `+0x0c` | Extents `+0x14..+0x1c` | Parameters `+0x9c..` |
|---|---|---|---|---|---|
| 0 | empty (world roots) | 0 | 0 | 0 | none, zero inertia |
| 1 | sphere | p4 | p4 + 0.05 | all p4 + 0.05 | p4 |
| 2, 3, 4 | cylinder along X, Y, Z (collision core is a segment) | p5 | p4 + p5 + 0.05 | p5 + 0.05, the axis gets p4 + p5 + 0.05 | p4 (half length) |
| 5 | box (p4, p5, p6 are half sizes) | 0 | sqrt(p4^2+p5^2+p6^2) + 0.05 | p + 0.05 each | p4, p5, p6 |
| 6 | mesh geometry | | sqrt(max vertex^2) + 0.05 | | vertex list |
| 7 | vertex cloud body | | same as 6 (1.0 when the handle is zero) | | vertex list |

* The skin constant `0.05` is `005f0c98` (bits `3d4ccccd`).
* Prototype fields: `invMass` (`+0x14`, `1/mass`, 0 when mass is exactly zero), body-space inverse inertia (`+0x18`) and its
  inverse (`+0x48`), shape pointer (`+0x78`), linear drag `mass * -0.005` (`+0x80`, constant `005f1440`), angular drag
  `mass * -0.01` (`+0x84`, constant `005f0e54`), volume copy (`+0x88`), flags (`+0x9c`). A zero mass sets flags `0x5`
  (static); every prototype also gets `0x400` after the shape is linked, so a static box has flags `0x405`.
  **Note for the box code in `world_model`**: `PhysicsBox::geometryFlags` holds 5, the stock value is `0x405` (plus the
  `|1` from `004805b0`, so `0x405`). `stepEmptyPhysics` compares with 5; that is a project-internal convention, not the
  stock value, and should be left alone only on purpose.
* The shape starts with friction `0.6` and bounce `0.2` (immediates `3f19999a`, `3e4ccccd`, shape `+0x50`, `+0x58`).
* Inertia (`004a6010`) gives the inverse diagonal. Sphere `2.5 / (r^2 * m)`, volume `r^3 * 4.18879`. Cylinder: axis
  `2 / (r^2 * m)`, other axes `1 / ((r^2 * 0.25 + (2h)^2 / 12) * m)`, volume 0. Box: `3 / ((b^2 + c^2) * m)` per axis (the
  stock formula for half sizes), volume `8*a*b*c`. A zero mass gives zero matrices and leaves the volume at 0. The inverse
  is made by `0054e3b0` and `0054e510` (cofactor matrix, determinant, transpose).
* Vertex clouds use a stand-in sphere (`004a6350`): `2.5 / (m * r)` (r is not squared in the stock code; copied as is), and
  the prototype volume is then forced to 0.
* The Mesh code (6) needs `004aa330`, a 107-line decompile not read yet: `massPropertiesSupported` is false for a
  dynamic mesh body. Static meshes and meshes with a zero handle work.
* Matrix helpers `0054e1b0`, `0054e1d0`, `0054e4a0`, `0054e9c0`, `0054e510`, `0054e3b0`, `0054eda0` and quaternion helpers
  `0054f760`, `0054f810`, `0054fb50` (a 9-multiply product, cross-checked against the Hamilton product in the test).

## Slice 2: body state, forces, integrator, commit

* `State` mirrors the 0xd4 record (field offsets are `static_assert`ed): position `0x00`, velocity `0x0c`, position shift `0x18`
  (added to the position at the end of each step), time `0x24`, force `0x28`, step flags `0x3c`, rotation matrix `0x40`, angular
  velocity `0x70`, angular momentum `0x7c`, torque `0x88`, quaternion `0x94`, world inverse inertia `0xa4`. Bytes `0x34..0x3c`
  are not read by anything ported.
* `addForce` is `004a6600` (force added; torque += offset x force). `accumulateWorldForces` is the first part of `004a3f90`:
  the gate is "body active, not asleep, prototype not static, and (child body or nonzero inverse mass)"; it clears step flag
  bit 0 of the other buffer, then adds drag, gravity (unless the no-gravity flag) and buoyancy (`-(volume * 1.2928) * g`).
  The rest of `004a3f90` (water plane and clip planes through `004b14f0`, `004b1580`) is not ported.
* `integrate` is `004a7c10`, three branches: RK4; the no-rotation Euler branch (body flag `0x100`); the simple branch
  (also prototype flag `0x200`: no rotation, no damping, just position and velocity). The child-body branch (body flag `4`,
  placing a body relative to its parent through scratch globals at `006439e0..`) is refused.
* Angular damping factor is `invMass * (mass * -0.01) * dt * 100 + 1`, which is `1 - dt` up to rounding.
* `commit` is `004a4ed0`: gate, smoothed force (`activity = 0.9 * activity + 0.1 * force`), the rest test (next velocity^2
  below 0.1, angular velocity^2 below 0.001, `invMass^2 * torque^2` and `invMass^2 * activity^2` below 0.01; constants
  `005f08e8`, `005f09cc`, `005f0c20`), the buffer flip, accumulator clear and world inertia refresh. Going to sleep calls `004a7a80`,
  which is not read; the port reports `WentToSleep` and does not flip.
* `World::stepFreeBodies` is the sequence above for free bodies only (time accumulation, forces, integrate, commit). It is NOT
  the full `004a5190`.

## Slice 3: collision primitive

`sphereCoreClosest` is the `0x101` case (both shapes are one-vertex cores) of `004af970`: both core vertices
`(0,0,0)` are moved to the world with `004b1460` (`pos + R * v`), `normal = a - b`, `distance = |normal|`, and the normal is
divided by the distance when it is positive. The check `004ad6c0` (is a vertex inside a face region of the other feature)
returns 0 when the other feature has no faces, which is the case for a sphere. `pairDropped` is the first test of `004a2d10`:
drop when the distance is exactly 0 or greater than `r1 + r2 + 0.05`.

Not covered: every other feature pair (`0x102`, `0x103`, `0x201`, `0x202`, `0x203`, `0x302`, ...), the clip routines
(`004ad0a0`, `004ace20`, `004ad220`, `004af3a0`, `004af4f0`, `004af630`), the polyhedron builders, and the rest of `004a2d10`.
Box-versus-plane is not a separate primitive in this engine as far as read: a plane would be a shape type, and the shape
codes above contain none; the existing `physics_box_ray` (`00498240`) is a ray test for picking, not contact generation.

## Verified from static reading versus not compared

| Item | Static reading | Compared with the running stock game |
|---|---|---|
| Shape and prototype fields, mass properties | yes (decompile plus disassembly) | no |
| Integrator operation order and constants | yes (disassembly of `004a7c10`, `004a9000`, `004a9180`, `004a8f00`) | no |
| Rounding | the port uses float, the stock used x87 with 80-bit intermediates and stored floats; the last bits can differ | no |
| Rest test, commit | yes | no |
| Sphere core distance | yes | no |
| The tests | closed-form double-precision values (free fall is exact for RK4 with constant force; a constant-omega quaternion) | not a stock run |

## Not done (and what blocked it)

* Solver entry: located (`004a3900` plus virtual slots) but the constraint classes (contact and joint objects) and their
  vtables were not found. A scan of the data section for vtables with entries in `0049xxxx` to `004bxxxx` found only the four-entry table at `005f1444`
  (`004a59f0`, `004a5a70`, `004a5b00`, `004a5b80`, used by the prototype object) and unrelated tables. The next step is to find the
  allocation sites that insert into the lists at world `+0xb8` and `+0x118` (constraint and contact objects).
* `00498dd0` looks like a scene-node bounding update rather than collision: it grows a parent's sphere or box over its children
  (`004988a0`) and re-parents nodes between cells using the sphere and box tests `0054b850`, `0054ba20` (`00498b10`). The body contact search that feeds `004a2d10` lives elsewhere (candidates: `004a2820`,
  `004a35d0`, `00426800` events).
* Parent-relative (child) bodies, sleep (`004a7a80`),
  compound bodies (flag `0x10` averaging in `004a3bc0`), the water-plane contact section of `004a3f90`, mesh inertia (`004aa330`),
  the shape polyhedron builders, and the whole joint set.

## How to wire this into `world_model` and the natives (not edited here)

1. `PhysicsRef.createBox` (`004805b0`): keep calling the existing logic, and additionally keep a
   `physics::createBodyProto(0.0f, 5, hx, hy, hz, nullptr, err)` result (type 5, mass 0) next to `PhysicsBox`. That gives the
   correct bound radius, extents and flags `0x405`.
2. `world_model` `GameWorld::stepEmptyPhysics` stands in for `004a5190`. It should build one `physics::World` and push a
   `Body*` per dynamic type-2 node; `stepFreeBodies(dt)` replaces the empty step for free bodies. Until contact detection
   exists, a dynamic body must still stop the bounded path when it could touch another body or a static box.
3. `PhysicsRef.create` (`004807f0`) and `createSphere` (`004806f0`, uncalled so far) map onto `createBodyProto` with type 1.
   `getPos` (`00480b00`) reads `state[current].pos`; `setMatrix` (`00480920`) writes the position and rotation of both buffers.
4. The car cfg `body` directive (`0046bb80`) calls `createBodyProto` for sphere, box, cylinder and mesh shapes. Hand it the
   vertex list decoded from the `.scx` collision mesh through the `meshVertices` argument (stock decodes through `004b5c30`).
5. CMake: one `add_executable(physics_body_test ...)` / `add_test(NAME physics_body_unit ...)` pair was appended at the end of
   `CMakeLists.txt`. `src/physics_body.cpp` is not yet in `sl_data`; add it there when `world_model` starts using it.

## Orientation creation increment, 2026-10-28

`quatFromMatrix` now follows stock `0054ff20`. `createBody` accepts an optional
orientation, converts it to a quaternion, regenerates its rotation, refreshes
world inertia, and copies the initialized state to the second buffer. This is
the sequence at `0049b17f` through `0049b1bc` in the saved
`local/census/DecompileAt-0049b0b0.log`. The conversion uses the saved whole-program
decompile. The original instruction bytes were read separately with
`xcrun llvm-objdump`; the output is
`local/verify/native-parity-20261028/0054ff20.asm`.

The branch with a nonpositive trace selects the largest diagonal, then uses the
integer index cycle `{1, 2, 0}`. Ghidra's decompile incorrectly prints those
integer indices as tiny float values. The instruction listing confirms their
integer use. The conversion does not normalize the quaternion.

`physics_body_unit` passed after a successful rebuild. It exercises identity,
half turns on all axes, positive and nonpositive traces, both initialized
buffers, and the inertia-axis swap of a rotated nonspherical box. These are
algebraic checks. A later isolated original-code comparison found that the
running Wine game's graphics thread uses x87 mode `007f`. The native orientation
functions now use float arithmetic with contraction disabled, and match all
words in 11 deterministic cases in that mode. Other precision modes differ.
`tools/wine/math_oracle.sh` reproduces the check. This does not establish the
precision of other game threads or of a Windows run.

The mesh integral and inertia implementation now matches all 26 compared words
for a tetrahedron and a translated box under the same explicit mode. The
comparison executes original `004aa330` code with allocator adapters in a
private Wine helper. `tools/wine/mesh_mass_oracle.sh` verifies code bytes and
records inputs, raw integrals, outputs, and differences. This is function-level
evidence, not a game simulation comparison.

## Remaining physics scope

Decompile lines counted from `all.c` by address: the physics core `004axxxx` to `004bxxxx` is 358 functions and 21,273 lines;
this work covered the integrator, the body state helpers, the shape builder and a handful of helpers (about 900 lines,
roughly 4 percent of that range). The big pieces left are the constraint classes and the dense solver (`004b2xxx` to `004b4xxx`),
the closest-feature routines (`004ac xxx` to `004af xxx`, about 4,000 lines), contact tracking and event handling
(`004a2xxx`, `004a3xxx`), the joint types, and the natives `0048xxxx` to `0049xxxx` (about 23,000 lines, mostly script wrappers).
A reasonable working estimate for a car that can sit on a static track mesh and be stepped is 12,000 to 16,000 more
decompile lines read and ported.

## Current bounded integration, 2026-10-28

The earlier missing-source/build statements above are historical. physics_body
and the new compound, topology, contact, suspension and friction modules are now
in sl_data. GameWorld owns a physics::World clock but still rejects dynamic
native-device world stepping rather than silently omitting it.

Native macOS Wine is the primary original-code comparison backend. Docker Wine
produced different arithmetic and is not interchangeable. The mesh mass corpus
comparison now matches309 real body meshes plus2 synthetic fixtures in volume,
center and the nine defined inverse-inertia values. The original caller's matrix
scaling0054e7e0 is part of the original oracle; omitting it falsely reported mass
differences. Uninitialized matrix padding is excluded explicitly.

Topology box/tetrahedron/point/segment comparisons caught and corrected a face
normal reversal, float operation order, and missing point/segment features.
Original004aa560 initializes its table before each oracle run. Contact, suspension
and compound stepping comparisons include nested/rotated attachments. The typed
RelativeBodyPose retains quaternion and matrix together through setOrientation.
Sleeping/wake lists, special bounding-root behavior, general polyhedron closest
queries, off-center/rotating direct friction and the full world collision/solver
loop remain incomplete. No isolated comparison marks these whole systems done.

## Car registration and contact discovery, 2026-10-30

The car compound and stock-ordered part, root and wheel helper resource nodes
are registered with `GameWorld`; suspension bodies without stock scene nodes
do not receive invented ones. The empty and camera-only steps reject
registered cars before advancing time. The parent-face filter includes stock
`004a0aa0` swept box and mesh bounds, but that filter alone does not discover
all contacts and is not connected to a live car step.
Dynamic `PhysicsRef.setMatrix` updates the body pose and linked swept bounds;
it rejects an attached bone when the required update queue is unavailable.
Focused native-camera tests cover both paths.

`CarRuntime` now holds persistent wheel rotation state with transactional
`advanceControls`/`advanceDrivetrain`: stock manual shift timing, low-speed
automatic selection, starter, low-idle compensation, clutch exchange and
brakes operate on real `CarRuntime`/`CarWheelRuntime` values; car creation
initializes the wheel fields. Moving automatic shifts, damage, intermittent
cuts and nitrous fail explicitly. World invocation still requires the
contact scheduling below; runtime transition tests were added but the methods
have not run inside a live frame.

Stock broad-phase pairing uses per-sector proxy endpoints and directional pair
updates (`004b3c40`, `004b4250`, `004b3850`). Its swept bounds, reparenting,
resource loading and pair lifecycle are not yet represented. Wheel contacts,
constraint solving and drivetrain scheduling remain required before a car can
be stepped safely. Focused car, creation and resource-world tests passed; a
live car trajectory has not been verified.
