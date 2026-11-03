# Native car factory: where the Free Ride path stops

Plain words first. After the scripted click on Free Ride the game now shows its real "PLEASE WAIT / LOADING..." screen
(`.scratch/shots/fr-loading-screen.png`), runs `GameLogic.loadDefaults`, picks a random demo car and then asks the
engine to build that car. The engine builds a car from a text file (`native car <path>.cfg`). Our code has no
car builder yet, so the probe stops there with an explicit message. This page records what the car builder is, so the
size of the job is a measured number and not a guess.

All addresses are in the stock executable `StreetLegal_Redline.bak.exe` (MD5 `e686099e7233bb7d7c461b1b35c5064c`, confirmed
in the header of each saved `local/census/DecompileAt-*.log`). Everything below comes from those saved decompiles or from
`local/census/all.c` (the saved whole-program decompile) plus bytes read from the executable. Nothing here was verified by
running the stock game.

## How the flow reaches the car

1. `MainMenuDialog.osdCommand` (tree 23 of `sl/Scripts/game/MainMenu.class`, class 1) handles `CMD_FREERIDE`: it shows the
   loading screen, calls `GameLogic.loadDefaults`, sets `GameLogic.gameMode = GM_FREERIDE`, picks
   `GameLogic.getVehicleDescriptor(VS_DEMO)` and sends `CMD_LOAD`.
2. `CMD_LOAD` builds `Player.car = new Vehicle(GameRef, id, colorIndex, optical, power, wear, tear)`, the seven-argument
   constructor (`Vehicle.class` tree 11, source line 68).
3. That constructor calls `GameRef.create`. The script resource it names has the payload
   `script cars\racers\Baiern_data\scripts\Baiern_CoupeSport.class` and
   `native car cars\racers\Baiern_data\scripts\_main_CoupeSport.cfg`. The car differs from run to run
   (the demo car is random), the device is always `car`.
4. `GameRef.create` (`src/natives/game_type.cpp`) refuses with:
   `unsupported script native device 'car' (script resource 0x00150006, payload ...)`.

## What the stock car builder does (read, not yet implemented)

* Factory `00442500`: allocates a `0x2e98`-byte object (vtable `005f0f18`), initialises 17 sub-records of one kind,
  four of another, a 0x90-byte array and further blocks, links the node, and reads Config values
  (`camera_look_ahead`, `camera_go_behind`, `camera_go_above`, `camera_distance`, `camera_FOV`, `target_angle_weight`).
  The first car ever built also runs `004427e0`, which registers 63 script natives
  (`Chassis.getTorque`, `WheelRef.setPos`, `SfxTable.addItem`, ...).
* Initialiser `00446180` (1625 lines of decompile): writes about 150 default values into the object, loads six
  resources from `frontend.rpk`, `cars.rpk` and `sound.rpk`, and then reads the car's `.cfg` through the line dispatcher
  `004b6520` with the directive table at `0060e0f8`. The same dispatcher already drives the part configuration in
  `src/part_config.cpp`.
* The directive table has 107 directives. The handlers add up to about 4,700 lines of decompile. The list is at the end of this page.
* The first lines of every real car file are physics, not data. In the Baiern CoupeSport file: `material`, `compound`,
  `chassis`, `body ... sphere`, then a dozen `body ... <mesh>.scx` lines whose collision meshes become physics geometry
  (`body` handler `0046bb80` calls `0049ab60` for sphere, box, cylinder, capsule and other shapes and `0049a990` for
  a mesh), `click`, `deformable`, `camera`, `steering`, `seat`, `pedals`, gauges, `enginepower`, `ratios`, `wing`.
  So no real car file can be parsed to the end without the physics geometry layer.

## Size of what is missing

Measured on the saved whole-program decompile (`local/census/all.c`), counting functions and lines by address range:

| Area | Address range | Functions | Decompile lines |
|---|---|---|---|
| Car, chassis, wheels, engine natives | `0043xxxx`-`0044xxxx` | 295 | 21,608 |
| Parts, part devices, misc game | `0045xxxx`-`0047xxxx` | 468 | 32,722 |
| Physics natives, geometry, bodies | `0048xxxx`-`0049xxxx` | 403 | 23,072 |
| Physics core (joints, contacts, integration) | `004axxxx`-`004bxxxx` | 358 | 21,273 |

The executable contains no strings from a known physics library (no ODE or Bullet names), so this is the game's own
engine. The script side adds 98 natives in `java.game.parts` and `java.game.Vehicle` (WheelRef 35, Part 29, Chassis 23,
DynoData 5, Vehicle 3, SfxTable 3), all still `unread` in `docs/natives.tsv`.

Estimate, in decompile lines to port, not in hours: about 65,000 to 70,000 lines across roughly 1,500 functions, of which
the car cfg directives and geometry prototypes are the first 10,000 or so. In C++ that is likely 15,000 to 25,000 lines
plus tests. The track is a second, separate job: the scene scripts create static trimesh geometry, markers and triggers
through the same physics layer, and the car has to be placed on it and stepped before a driving frame exists.

Suggested order, each step testable against the real 2005 files:

1. Physics geometry prototypes (`0049ab60` shapes, `0049a990` mesh, mass properties) with the part-style prototype
   ownership already used for part configs.
2. The car cfg directive handlers, starting with the data-only ones, one saved decompile each.
3. `00442500` plus `00446180` as the type-8 loader for `native car`, with the 63 registered natives.
4. Runtime instantiation of the car (compound bodies, wheels, joints), then a fixed-step integrator.
5. The track scene, then rendering the car's meshes at its body poses.

## Directive table at `0060e0f8`

| directive | handler | decompile lines |
|---|---|---|
| `click` | `0046b950` | 127 |
| `body` | `0046bb80` | 208 |
| `bone` | `0046c280` | 15 |
| `noclick` | `0046c2d0` | 15 |
| `nocollision` | `0046c310` | 19 |
| `buoy` | `0046c360` | 15 |
| `render` | `00443de0` | 28 |
| `light` | `00443de0` | 28 |
| `deformable` | `0046bfa0` | 116 |
| `shadow` | `0046c1a0` | 48 |
| `flags` | `0046d0c0` | 28 |
| `lod` | `0046d180` | 34 |
| `lods` | `0046d290` | 48 |
| `colors` | `0046d330` | 45 |
| `category` | `0046ced0` | 24 |
| `slot` | `0046c3a0` | 68 |
| `bolt` | `0046c4e0` | 117 |
| `attach` | `0046c6a0` | 54 |
| `compatible` | `0046c770` | 54 |
| `slotdmgmode` | `0046c840` | 32 |
| `slotdeform` | `0046c8a0` | 25 |
| `slottype` | `0046cb30` | 35 |
| `slotflags` | `0046cb30` | 35 |
| `slotmove` | `0046ccd0` | 53 |
| `deform` | `0046cf10` | 24 |
| `mesh` | `0046ce60` | 32 |
| `texture` | `0046cdf0` | 32 |
| `logo` | `0046d400` | 32 |
| `flap` | `0046c8f0` | 73 |
| `flapsfx` | `0046ca70` | 40 |
| `camera` | `00443b00` | 76 |
| `type` | `00442f90` | 141 |
| `friction` | `004432b0` | 26 |
| `spring` | `004431c0` | 46 |
| `wheel` | `004433e0` | 80 |
| `wheelbones` | `004438d0` | 65 |
| `bonepos` | `00443a00` | 26 |
| `wheelpars` | `00443310` | 26 |
| `skid` | `00443390` | 26 |
| `wheeldeform` | `004439b0` | 24 |
| `arm` | `004435b0` | 40 |
| `hub` | `00443690` | 47 |
| `instant_center` | `004437a0` | 45 |
| `steering` | `00443ea0` | 14 |
| `sw_radius` | `00443ef0` | 32 |
| `steeringradius` | `00443ef0` | 32 |
| `maxsteer` | `00444260` | 25 |
| `rpm_idle` | `004442b0` | 30 |
| `drag` | `00444310` | 40 |
| `wing` | `004443c0` | 79 |
| `center` | `00444570` | 33 |
| `rollbar` | `00443a60` | 39 |
| `linked` | `0046d8a0` | 41 |
| `sfx_engine_up` | `00445760` | 64 |
| `sfx_engine_down` | `00445890` | 64 |
| `sfx_exhaust` | `004459c0` | 64 |
| `sfx_gearbox_fwd` | `00445af0` | 37 |
| `sfx_gearbox_rev` | `00445b80` | 37 |
| `sfx_wreck` | `00445c10` | 32 |
| `sfx_horn` | `00445c80` | 43 |
| `sfx_gearchange` | `00445d50` | 39 |
| `mass` | `00443f50` | 24 |
| `enginepower` | `00443f90` | 46 |
| `ratios` | `00444080` | 27 |
| `torque` | `004440e0` | 38 |
| `rearendratio` | `00444150` | 24 |
| `brakepower` | `00444190` | 31 |
| `clutch` | `00444200` | 28 |
| `driver` | `004445f0` | 56 |
| `controller` | `004445f0` | 56 |
| `ext_camera` | `00443d40` | 34 |
| `nodriver` | `00444710` | 12 |
| `name` | `00444730` | 9 |
| `seat` | `00444740` | 44 |
| `pedals` | `00444810` | 66 |
| `shifter` | `00444990` | 44 |
| `key` | `00444a60` | 35 |
| `handbrake` | `00444ae0` | 35 |
| `cockpit_rpm` | `00444b60` | 64 |
| `cockpit_speed` | `00444cb0` | 64 |
| `osd_gauge` | `00444e00` | 38 |
| `osd_rpmpin` | `00444e80` | 47 |
| `osd_spdpin` | `00444f20` | 47 |
| `osd_nospin` | `00444fc0` | 47 |
| `osd_damage` | `00445060` | 38 |
| `osd_gear` | `00445120` | 45 |
| `osd_speed` | `004451b0` | 53 |
| `osd_rpm` | `00445260` | 53 |
| `osd_nos` | `00445310` | 53 |
| `osd_turbo` | `004453c0` | 53 |
| `osd_mileage` | `00445470` | 53 |
| `osd_abs` | `00445520` | 43 |
| `osd_asr` | `004455b0` | 43 |
| `osd_hbrake` | `00445640` | 43 |
| `osd_emergency` | `004456d0` | 43 |
| `osd_damage_slot` | `004450e0` | 25 |
| `stockpart` | `0046d970` | 56 |
| `material` | `00445e00` | 49 |
| `damage` | `0046cf50` | 30 |
| `damagedef` | `00445ef0` | 9 |
| `velfactor` | `00445fc0` | 24 |
| `turn` | `00445f00` | 35 |
| `roll` | `00446000` | 25 |
| `pitch` | `00446050` | 25 |
| `steerhelp` | `004460a0` | 25 |
| `cheat_control` | `00446100` | 25 |
| `notify` | `00446160` | 12 |
