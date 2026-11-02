# Street Legal Racing: Redline, Apple Silicon Port

A native macOS arm64 port of the 2005 game Street Legal Racing: Redline. It runs the game's own scripts and data and aims to behave exactly as the original executable does.

## Language

### The port and its authority

**Port**:
The native arm64 macOS build in this repository. It is a behavioral re-implementation guided by the stock decompile, not an instruction-level translation.
_Avoid_: remaster, remake, emulator

**Stock Executable**:
An identity-bound executable from either official game version in the Reference Folder. Both official versions validate the single Port. Shared behavior is authoritative; differing behavior is inventoried and reconciled using newer official behavior by default while retaining older-only features. Incompatible choices require explicit resolution under the full Port specification.
_Avoid_: original exe, retail build, base game

**Mod Executable**:
The community-modified executable shipped beside the stock one. Kept only for comparison, never as an authority.
_Avoid_: patched exe

**Stock Decompile**:
The whole-program Ghidra decompilation of the stock executable, where unnamed functions are called `FUN_<address>`. Static evidence for what the identified Stock Executable does; it must retain its build identity and does not itself demonstrate runtime behavior.
_Avoid_: census (also names unrelated corpus counts)

**Parity**:
The state where the Port gives the same observable result as the Stock Executable under the same declared inputs and starting conditions, including consistent Stock quirks. Evidence is bounded to the behavior, reference identity and variants actually compared; approved deviations remain separate. For cross-version differences, acceptance follows the recorded reconciliation decision rather than claiming equality to contradictory Stock behaviors.
_Avoid_: 1:1 match, compatibility

**Wine Capture**:
A recording of the stock executable running under Wine. Observes behavior of an identified Stock Executable under declared conditions. Differences from static evidence require investigation of instrumentation, environment and version identity rather than assuming either proves the whole behavior.
_Avoid_: reference run, oracle

**Reference Folder**:
The immutable `ref/` directory holding the stock install and reference repositories. Never modified.
_Avoid_: ref data, assets folder

**Installed Game**:
The extracted game install tree the port actually runs, and the source of ground-truth scripts.
_Avoid_: game files, extract

### Scripts and data

**Script**:
Compiled game logic shipped with the game, in the Tufa class format. The port runs the original scripts unchanged.
_Avoid_: mod, source

**Tufa**:
The compiled class file format the stock scripts use.
_Avoid_: bytecode file, class dump

**Script VM**:
The port's interpreter for stock scripts.
_Avoid_: engine, runtime (too broad)

**Native**:
A function the stock executable exposes to scripts. The port implements each one separately; the registry tracks which are matched against stock.
_Avoid_: builtin, intrinsic, host call

**Pack**:
A resource archive (`.rpk`) mounted into a numbered **Slot**.
_Avoid_: archive, bundle, asset file

**Slot**:
The numbered position at which a **Pack** is mounted; resource IDs refer to packs by slot.
_Avoid_: pack index

**Resource**:
An item stored in a **Pack** and addressed by a resource ID: a model, texture, sound, font, ground, or car description.
_Avoid_: asset

**Resource Node**:
A live, typed element of the world tree created from a **Resource**. Its numeric type is the stock type (for example 1 object, 2 car body, 3 render instance, 12 camera).
_Avoid_: entity, scene object, game object

### Game modes and places

**Free Ride**:
The Main Menu entry that loads the **City** and puts the player in a car to drive immediately.
_Avoid_: test drive, sandbox

**Career**:
A player's saved progress: name, money, owned cars and garage. Created by NEW CAREER and persisted to a save file.
_Avoid_: profile, campaign

**City**:
The open drivable world: ground, roads, traffic and sectors. Both **Free Ride** and races happen here.
_Avoid_: map, level, track (the stock scripts also call it a track)

**Garage**:
The career hub scene, from which the player reaches the **Car Lot**, the **Used Car Dealer**, parts handling and the city.
_Avoid_: home, lobby

**Car Lot**:
The garage screen listing the cars the player already owns. Empty on a new career.
_Avoid_: dealer, showroom

**Used Car Dealer**:
The garage screen where the player browses and buys cars for sale.
_Avoid_: car lot, market, shop

**Night Drag Race**:
A drag race started from the **City** after dark.
_Avoid_: night race (ambiguous with other night events)

### Cars

**Car**:
A drivable vehicle assembled from a car description and its installed **Parts**: one root body, wheels and part bodies.
_Avoid_: vehicle (the stock script class of that name also covers non-car actors)

**Part**:
A removable component installed on a **Car** at a **Part Slot**.
_Avoid_: upgrade, mod

**Part Slot**:
A position on a **Car** that accepts a specific kind of **Part**, defined by the car's configuration.
_Avoid_: mount, socket

**Used Car**:
A car offered at the **Used Car Dealer**, bought into the player's **Career**.
_Avoid_: second-hand car, listing

**Paint**:
The per-body colour a car's colour scheme applies to its render model. A car without it appears as bare chrome.
_Avoid_: skin, tint, texture

**Chase Camera**:
The camera that follows the player's car from behind during driving.
_Avoid_: follow cam, third-person view

**HUD**:
The driving overlay: speedometer, gear indicator, minimap and status lamps.
_Avoid_: dashboard, overlay

### Evidence and progress

**Run**:
One launch of the port, scripted or live, with its own output directory of logs and screenshots.
_Avoid_: session, test run

**Gap**:
A stock behavior the port has not implemented, recorded explicitly in the run output. A gap is never hidden or faked.
_Avoid_: stub, TODO, workaround

**Cartrace**:
Per-physics-step telemetry of a car in a **Run**: position, velocity, controls, wheels and contacts.
_Avoid_: car log

**Rung**:
A small, measurable behavior contract needed to establish Parity, with declared variants and explicit dependencies on other Rungs. A Rung is verified only by retained evidence covering its assertions; Rungs form a dependency graph.
_Avoid_: phase (the plan's phases are a separate list), stage, fixed milestone

**Journey**:
A reachable player activity spanning several Rungs, such as creating a Career or buying a Used Car. Journey coverage records which constituent behaviors and variants have been proved.
_Avoid_: rung (a Journey contains several Rungs)

**Parity Proof**:
Retained evidence that a declared Rung or Journey comparison passed for identified Stock and Port Runs. Missing observations, unproved repeatability or uncovered variants cannot count as a successful Parity Proof.
_Avoid_: passing screenshot, parity score

**Forced Seed**:
A fixed random seed set for a Run. It controls only the affected random stream and does not by itself establish deterministic execution or comparable scheduling.
_Avoid_: deterministic mode
