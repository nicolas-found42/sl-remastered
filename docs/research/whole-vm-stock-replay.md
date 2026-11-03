# Whole-VM Stock record/replay candidate

Research date: 2026-11-01. Primary-source investigation, with read-only local
inventory. **No QEMU installation, image download, VM/container/game launch,
desktop interaction or audio playback was performed.** This is a candidate route
toward the requested deterministic actual Stock/Port Parity, not a replacement
for that goal or a claim that Parity is complete.

## Finding

QEMU documents deterministic whole-system record/replay for **AArch64**: guest
memory, hardware state, clocks, screen and external inputs can be replayed from
the initial machine state and a nondeterministic-event log. Its implementation
requires **one virtual CPU, TCG, and instruction counting**. Current source
explicitly blocks multiple CPUs and rejects hardware virtualization with
`-icount`; multithreaded TCG is also incompatible. Therefore an ARM64 Linux VM
containing the qualified ARM64 Hangover userspace is a technically credible
experiment, but **the actual Stock/Hangover workload has not been qualified under
it**. [QEMU record/replay](https://www.qemu.org/docs/master/system/replay.html),
[startup checks](https://github.com/qemu/qemu/blob/master/system/vl.c),
[TCG instruction counting](https://www.qemu.org/docs/master/devel/tcg-icount.html).

**[INFERENCE]** Moving the Linux kernel, Wine/Hangover, wowbox64, renderer, media
decoders, PulseAudio and controller inside that boundary closes a different gap
from a late main-image IAT hook: the recorded machine starts before Stock exists,
and kernel scheduling and all guest instructions participate in the replay.
This can repeat **one actual VM-observed Stock execution** without replacing
Stock clocks with 60 Hz values or fixing its game RNG. It cannot retrospectively
record the existing native-container run, reproduce its multicore schedule, or
give the native Port the same schedule automatically. QEMU synchronizes external
events by executed-instruction counts; it does not record every deterministic
memory access as a separate event. [Execution replay design](https://www.qemu.org/docs/master/devel/replay.html).

The existing backend qualification reaches both complete intro movies, Main
Menu, New Career's name dialog, Cancel, and Main Menu again, with native
DirectInput transitions and private nonzero PCM. It used four container CPUs
and 3 GiB, and completed in 66.310 wall seconds. That is evidence for the
**native ARM64 container**, not a throughput or functionality measurement for
outer TCG. Its synthetic disc-check shim has no actual CD-track audio, and it
uses disclosed graphics/input observer proxies. Replaying these components
does not remove those limitations or establish an untouched-process control.
Stock's executable/data can remain unchanged; existing shims and observers must
stay in provenance. [Qualified backend and evidence](stock-headless-implementation.md),
[adapter contract](../../local/verify/stock-arm64-adapter/README.md).

## Exact boundaries and configuration constraints

| Boundary | Source-backed rule and consequence for this experiment |
|---|---|
| CPU / accelerator | `-smp 1 -accel tcg,thread=single`. No HVF, KVM or MTTCG. Host core count does not provide parallel guest CPU execution for this replay. The guest can still create many Linux/Wine threads, scheduled onto that one vCPU. [Startup checks](https://github.com/qemu/qemu/blob/master/system/vl.c), [TCG implementation](https://github.com/qemu/qemu/blob/master/accel/tcg/tcg-all.c). |
| Time / `icount` | `-icount shift=auto,rr=record,rrfile=...` is QEMU's documented starting form. Fixed `shift=N` assigns each instruction `2^N` virtual ns; `auto` adjusts speed toward host time. This is not cycle-accurate hardware timing. `sleep=on` is the default; `sleep=off` jumps idle time to the next timer and is incompatible with `shift=auto` or `align=on`. Keep the same settings for replay. [Invocation](https://www.qemu.org/docs/master/system/invocation.html#hxtool-9). |
| RTC / other clocks | A fixed `-rtc base=2026-11-01T00:00:00,clock=vm` controls VM RTC origin and avoids an advancing host wall clock there. It does not replace every guest clock with that RTC. QEMU distinguishes virtual, host and virtual-real-time clocks and records nondeterministic reads/checkpoints. Kernel/Wine time domains still execute normally. [RTC options](https://www.qemu.org/docs/master/system/invocation.html#hxtool-9), [clock/checkpoint design](https://www.qemu.org/docs/master/devel/replay.html#timers). |
| Randomness | `qemu_guest_getrandom` records generated results and returns recorded bytes during replay. `virt` defaults to random DTB seeds; choose `dtb-randomness=off` for a simpler boot configuration, and omit optional entropy devices initially. This is VM configuration, not a forced Stock seed. Guest `/dev/urandom` state, persisted seed files and Stock TLS RNG state belong to the same initial disk/memory plus execution. Do not assume every optional entropy backend is replay-safe merely because one helper is. [Random implementation](https://github.com/qemu/qemu/blob/master/util/guest-random.c), [virt DTB settings](https://www.qemu.org/docs/master/system/arm/virt.html#machine-specific-options). |
| Board / CPU identity | Pin a versioned `virt-<release>` board and a named AArch64 CPU such as `cortex-a72`, then qualify that exact combination. `virt` defaults to a 32-bit CPU; a 64-bit model must be specified. `max` features can vary with QEMU versions. Pin QEMU build, machine properties, CPU, kernel, initrd and command line, rather than promising replay between arbitrary builds. [virt platform](https://www.qemu.org/docs/master/system/arm/virt.html). |
| Disks / firmware | Put `blkreplay` between **every enabled block image** and its virtual controller. Start from the same complete disk state; the rr file is not a complete archive of disk contents. Include root, data, removable media and writable firmware/NVRAM if present. Prefer direct Linux kernel/initrd boot and one disk for the first experiment to avoid firmware state and device hotplug. [Block replay](https://www.qemu.org/docs/master/system/replay.html#block-devices), [block completion design](https://www.qemu.org/docs/master/devel/replay.html#block-devices). |
| Initial snapshot | `rrsnapshot=init` creates a full initial VM snapshot in recording and restores it in replay. Use private persistent qcow2 overlays, omit each drive's temporary `snapshot` flag, and retain all backing files. QEMU snapshots include CPU, RAM, devices and writable disks; at least one writable nonremovable qcow2 device is required. **Do not use global `-snapshot`: current startup source adds a replay blocker for it.** [Replay snapshots](https://www.qemu.org/docs/master/system/replay.html#snapshotting), [image snapshots](https://www.qemu.org/docs/master/system/images.html#vm-snapshots), [option blocker](https://github.com/qemu/qemu/blob/master/system/vl.c). |
| External disk changes | A host-shared folder, 9p/virtiofs, vhost-user backend or directly mutable media is outside the simple disk model. Omit these from the initial qualification. Seal guest dependencies/game copies before recording; export artifacts after stopping rather than adding a live filesystem side channel. This restriction is an **experiment design inference**, not a claim that QEMU publishes a universal blacklist. [Device determinism requirement](https://www.qemu.org/docs/master/devel/replay.html#virtual-devices). |
| Serial / commands | Serial input is automatically recorded. Keep the same number of serial ports; their backends may differ between record and replay. `virt` provides PL011 UARTs. A resident guest controller can accept a framed command stream and send XTEST events into its private Xvfb. A replay should receive recorded serial input, not a second live command stream. [Serial replay](https://www.qemu.org/docs/master/system/replay.html#serial-ports), [virt devices](https://www.qemu.org/docs/master/system/arm/virt.html#supported-devices). |
| Keyboard / mouse | QEMU records supported virtual keyboard/mouse events, but QMP `input-send-event` targets QEMU guest input devices. It does not establish delivery into an unrelated Xvfb server. Retain the already qualified XTEST path inside the guest, controlled over serial or by a fully guest-resident script. DirectInput return-state evidence remains the consumption check. [QMP input routing](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html#command-QMP-ui.input-send-event), [adapter](../../local/verify/stock-arm64-adapter/README.md). |
| Network | Specify `-net none`. If networking is later necessary, configure every backend explicitly with its own `filter-replay`; replay suppresses real backend interaction and injects logged packets. Do not depend on NTP, downloads, host service responses or interactive SSH in the first sealed run. [Network replay](https://www.qemu.org/docs/master/system/replay.html#network-devices). |
| Emulated audio | If adding a QEMU sound card, record/replay must use identical audio hardware definitions. QEMU logs output progress and input samples; output PCM is recomputed by the guest/device path rather than archived wholesale by `EVENT_AUDIO_OUT`. A WAV backend writes to a private file. Card/backend presence alone is insufficient to claim waveform equality. [Audio replay](https://www.qemu.org/docs/master/system/replay.html#audio-devices), [audio implementation](https://github.com/qemu/qemu/blob/master/replay/replay-audio.c), [WAV backend](https://www.qemu.org/docs/master/system/invocation.html). |
| Display / devices | `-display none` prevents a host window while preserving configured emulated graphics. `virt` has no default graphics device and supports PCI/PCIe, virtio transports, RTC and PL011. There is no source-backed promise that every combination of devices is replay/snapshot-safe; QEMU specifically warns of incomplete snapshot support in some devices, including USB. Use the smallest device set and test it. [Display options](https://www.qemu.org/docs/master/system/invocation.html#hxtool-3), [virt devices](https://www.qemu.org/docs/master/system/arm/virt.html), [snapshot limitations](https://www.qemu.org/docs/master/system/images.html#vm-snapshots). |

For the first Stock experiment, **no emulated GPU, sound card, mouse or keyboard
is required**: Xvfb's pixels, llvmpipe rendering, file/intro decoding, Pulse's null
sink and `parecord` all stay inside guest memory/disk. This is an **[INFERENCE]**
from the qualified backend architecture, to be proven again under TCG. Explicit
`-display none -audio none`, no passthrough, and no host display/audio/HID mounts
keep the host private. `-audio none` here selects a silent QEMU backend; it must
**not** become a reason to disable Wine audio, skip media, omit game mixing, or
substitute silence in the guest. Preserve the explicit Pulse null sink and its
48 kHz stereo PCM monitor capture. [Backend evidence](stock-headless-implementation.md),
[QEMU audio/display options](https://www.qemu.org/docs/master/system/invocation.html).

QMP `screendump` captures an emulated display, so it cannot substitute for Xvfb
screenshots when no virtual GPU exists. Capture actual Xvfb pixels in the guest.
[QMP screendump](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html#command-QMP-ui.screendump).

## Local inventory and deployment tradeoff

Read-only commands found `brew` at `/opt/homebrew/bin/brew`, but neither
`qemu-system-aarch64` nor `qemu-img` on `PATH`, in `/opt/homebrew/bin` or in
`/usr/local/bin`. Standard `/opt/homebrew/share/qemu`,
`/usr/local/share/qemu`, `~/Library/Caches/qemu` and `~/.cache/qemu` were absent.
A filename-only depth-one check of `~/Library/Caches/Homebrew/downloads` found
no QEMU or Debian ARM64/qcow2 candidate. This is a scoped inventory, **not** a
claim that no disk image exists anywhere on the Mac. No broad private-file
search, daemon/container inventory, install, download or launch was used.

`uname -m` returned `arm64`; `sysctl` returned 18 logical CPUs and
51,539,607,552 bytes (48 GiB) host RAM. The existing headless research records
Docker's Linux VM as 8,317,267,968 bytes (about 7.75 GiB); it was not reconfigured
or restarted here. [Prior Docker measurement](stock-headless-implementation.md).

| Placement | Practical implication |
|---|---|
| Native ARM64 macOS QEMU CLI | Preferred **experiment candidate**: one outer TCG VM can use a bounded portion of host RAM without fitting inside Docker Desktop's 7.75 GiB VM. It remains software CPU emulation, even on the same ISA; HVF cannot accelerate rr. No GUI is required. Existing container image would have to be imported into the sealed guest or its exact userspace reconstructed and requalified. |
| ARM64 QEMU inside an ARM64 Docker container | Also a candidate because TCG does not require nested KVM/HVF, but the guest RAM, QEMU overhead/code cache, outer Linux VM and other containers share Docker's 7.75 GiB. It adds deployment and disk-I/O overhead without restoring guest CPU parallelism. Do not accidentally choose an amd64 QEMU image and add Rosetta/QEMU user translation. |

**[INFERENCE]** A 5 GiB guest on native macOS, with one vCPU and a separately
bounded QEMU process, is a reasonable first resource envelope for the previously
3 GiB game container plus Linux/controller overhead. A 4 GiB guest inside Docker
would leave under 3.75 GiB for all QEMU/outer-VM/other-container overhead and may
be tight; neither figure is a measured minimum. Reserve through the existing
resource pool and avoid concurrent experiments. Preserve llvmpipe's disclosed
thread settings initially; changing them changes the observed execution and
requires fresh qualification.

Expect materially lower throughput than the native four-CPU qualification:
Stock's x86 code is translated by wowbox64 into ARM64, which outer TCG executes,
while **all** native Wine, Mesa, decoder and Linux code is also TCG-emulated.
Do not invent a slowdown multiplier, real-time promise or fixed completion
deadline from the 66.310-second native result. Measure guest virtual progress,
wall time, resident memory, rr size and checkpoint cost in bounded stages.
Keep host stop/cleanup responsive and cap each stage. A successfully repeatable
but prohibitively slow route remains a real cost finding, not a Parity result.

Debian's official Bookworm index provides
`debian-12-generic-arm64.qcow2` and SHA512SUMS. It is a VM-disk candidate, unlike
the qualified container filesystem, which is not by itself a bootable Linux VM.
Pin a dated image/checksum and verified kernel/initrd before a future download;
`latest` is a discovery link, not reproducible identity. No image checksum was
validated or image downloaded here.
[Official Debian image index](https://cloud.debian.org/images/cloud/bookworm/latest/).

## Minimal experiment, with explicit stop conditions

These are future steps, not commands executed by this investigation.

1. **Seal a bootable baseline.** Select and pin a QEMU build, versioned `virt`
   model, named AArch64 CPU, kernel/initrd, private qcow2 disk and complete backing
   chain. Import the exact qualified ARM64 image where practical; retain digest,
   package/runtime hashes, Stock source hashes, disc fixture and proxy/shim
   provenance. Put controller, Xvfb, Pulse configuration and all dependencies in
   guest storage before recording. Use one guest disk and direct kernel boot;
   validate the real root UUID and required virtio drivers. No host shared folder
   or network is needed once sealed.
2. **Shell-only rr qualification.** Before Stock, record a short guest probe
   covering boot, child threads, sleep/time reads, RNG, asynchronous disk writes
   and the chosen serial controller. Replay that same recording twice from the
   same initial snapshot on fresh private copies. Compare guest outputs and
   states at shared instruction counts. A replay mismatch, unsupported-device
   error, snapshot failure, deadlock, OOM or unacceptable throughput stops this
   route before a game run. A shell probe passing does not qualify Stock.
3. **Actual complete media/menu run.** Record a fresh VM execution of the actual
   Stock backend through both full intro movies and the observed menu/dialog/
   Cancel/menu path, with native input-consumption evidence, actual screenshots
   and nonzero PCM. No intro skip, forced dt, replacement RNG, no-op audio or
   substitute reference frames. Start rr before Linux/Stock startup; do not use
   the late API-tape boundary as the initial checkpoint. The exact x86 emulator
   and CPU-feature decisions now need fresh evidence under TCG.
4. **Replay the same run twice.** Retain immutable record files and their initial
   snapshot/backing chain. Use separate writable copies for each replay, restore
   `rrsnapshot=init`, and supply no fresh control commands. Compare the actual
   rendered pixels, normalized PCM sample bytes/counts, control observations,
   game-state observations and declared terminal boundary. QMP `query-replay`
   supplies instruction counts; `replay-break` can stop at a future count and
   `replay-seek` can restore a preceding snapshot. Record final count, completion
   marker and all synchronization/errors, rather than treating process exit0 as
   sufficient. [QMP replay controls](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html#command-QMP-replay.query-replay).
5. **Extract and challenge.** After stopping and releasing file locks, read
   private copies of guest disks with an offline read-only filesystem tool;
   retain complete backing chains and hash extracted screenshots, PCM and trace
   files. A guest-prepared serial manifest can supply checksums and framed small
   artifacts. Never mount or alter the original evidence image while QEMU uses
   it. Challenge the verifier with a changed rr/input/initial-state/artifact
   digest, and require explicit invalidation rather than a silent new baseline.
   An additional independent record may be replayed twice too; two independent
   records need not match each other because their external inputs can differ.

Illustrative configuration shape only; placeholders deliberately prevent it
being mistaken for a tested launcher:

```text
qemu-system-aarch64
  -machine virt-<PIN>,gic-version=2,dtb-randomness=off
  -cpu cortex-a72 -smp 1 -m 5120 -accel tcg,thread=single
  -kernel <PINNED-ARM64-IMAGE> -initrd <PINNED-INITRD>
  -append "root=UUID=<VERIFIED-ROOT-UUID> console=ttyAMA0"
  -nodefaults -display none -audio none -net none
  -rtc base=2026-11-01T00:00:00,clock=vm
  -icount shift=auto,rr=record,rrfile=<PRIVATE>/stock.rr,rrsnapshot=init
  -drive file=<PRIVATE>/boot.ovl.qcow2,format=qcow2,if=none,id=img-direct
  -drive driver=blkreplay,if=none,image=img-direct,id=img-blkreplay
  -device virtio-blk-device,drive=img-blkreplay
  -chardev socket,id=control,path=<PRIVATE>/control.sock,server=on,wait=off
  -serial chardev:control
  -qmp unix:<PRIVATE>/qmp.sock,server=on,wait=off
```

Replay changes `rr=record` to `rr=replay` and uses fresh copies of the recorded
disk/snapshot bundle. Serial may become one file backend to capture output;
keep one port and let rr inject its recorded input. QEMU's example uses
`blkreplay` with an IDE disk; the virtio-blk adaptation above is an **unrun
configuration candidate**, and snapshotting through that graph must pass step2.
Device help and pinned-build behavior must be checked before creating the
concrete launcher. No optional virtual GPU/audio/USB device is a prerequisite
for the guest-private pipeline.

## What an honest proof and Port projection still require

A successful experiment can honestly claim: **this exact Stock workload,
inside this exact single-vCPU VM/software configuration, reproduced these
declared observations and machine checkpoints from this rr/snapshot bundle on
two replays**. It cannot prove all Stock executions repeat, original x86 hardware
timing, actual CD audio, full gameplay or Stock/Port equality. A VM that boots
without replay failure but drops media frames, produces silent audio or never
reaches the actual dialog fails the workload requirement. A viewport screenshot
and one checksum do not establish complete memory/state equality; name the
state regions/checkpoints and completeness limits actually compared.

The rr file is an **ARM64 machine execution artifact**, not the Port's replay
format. QEMU's GDB view is of the ARM64 Linux CPU/kernel/native/JIT instruction
stream; a PE32 Stock address is not automatically a QEMU guest program counter.
Mapping Stock objects, TLS ownership, x86 emulated registers and wowbox64 JIT
state for observation remains implementation work. External instruction-count
stops can permit read-only investigation without the existing PE32 crossprocess
mutation/initial-breakpoint mechanism, but are not a ready-made x86 debugger.
This is an **[INFERENCE]** from the two-layer execution architecture, not tested
debugger integration. [QEMU reverse-debugging boundary](https://www.qemu.org/docs/master/system/replay.html#reverse-debugging),
[qualified Hangover architecture](stock-headless-implementation.md).

Port projection still needs the following from the **actual replayed Stock
consumers**, preserving raw bits, initialization and consumption order:

- Clock domains/epochs and each consumed dt, wait/completion and input result;
  consumer identity and thread incarnation/role; the total ordering needed by
  the chosen cross-port seam, including overlap rather than fabricated ticks.
- RNG state and TLS ownership at initialization and consumption, with every
  relevant stream rather than a single forced seed.
- Effective DirectInput states/failures/reacquisition and message/input delivery,
  plus file/enumeration and media-completion results where behavior depends on
  them; XTEST coordinates or serial arrival alone are not consumption tapes.
- Comparable per-step world/menu/media state, rendered frames and private PCM,
  with first-divergence reporting and explicit omitted observations.

Those requirements are documented in
[Stock replay boundaries](stock-replay-boundaries.md). Whole-VM replay makes
repeated investigation of one complete run plausible; it does not establish
that projection contract or repair the Port. A new in-guest observer changes
the recording and must be disclosed and recorded from startup. Adding one only
during replay can change the instruction stream and invalidate replay. Prefer
read-only introspection while stopped at known counts, or establish an observed
recording and an independently qualified control. Never promote observer-only
repeatability into untouched Stock or cross-port schedule equivalence.

## Verification and remaining uncertainty

Firecrawl retrieved QEMU's official docs, official QEMU source mirror and
Debian's image index. Substantive selected source text was screened by Jev as
`pass` before use. Failed/rate-limited/404 fetches supplied no evidence. A
targeted Jev claim check verified all ten core QEMU claims, zero contradicted or
unsupported, all automatic; support ranged 0.93–1.00 and confidence 0.89–1.00.
Retained selected evidence and exact judgment:
[`source-verification.json`](../../local/verify/whole-vm-stock-replay/source-verification.json).
The links to `master` document the inspected implementation, not a frozen build;
the future experiment must pin and recheck its selected release.

No runtime claim is made. The full `tools/check.sh` suite was not run for this
research-only file: it builds/runs the Port and acquires renderer resources,
outside this task's read-only/no-game-launch scope. Documentation/links and the
scoped inventory were checked; no production file, `ref/` file or game asset was
edited. Runtime performance, selected CPU compatibility, replay/snapshot device
correctness, complete-media behavior, artifact equality and Port projection all
remain explicit experiment gates.

The whole-document Jev completion review returned `review`, with
`safe_to_apply=0.71`, composite `0.92925` and correctness confidence `0.74`;
it identified no concrete defect. The source-fact claim was accepted, while
a claim describing this document's limitations was unsupported by the supplied
upstream-source evidence. That documentary claim was not itself evidence of a
runtime result. The gate's tests field also contained incorrectly transcribed
draft link-check counts; the retained correction gives the actual checker
output. No runtime-test claim or second gate was used to erase that uncertainty.
See [`completion-review.json`](../../local/verify/whole-vm-stock-replay/completion-review.json).
