# Deterministic replay design: what primary sources say, and what it means here

Research date: 2026-11-01, via Firecrawl, primary sources only. Entry point was the curated list
[xusheng6/awesome-ttd](https://github.com/xusheng6/awesome-ttd), followed to its primary projects
(rr, PANDA, Intel PinPlay, Microsoft TTD). Gap being researched: what a deterministic replay of the
multi-threaded Stock process must record beyond API results (plan deliverable 5). This note is design
input, not an implemented or qualified mechanism.

## Claims taken from primary sources

rr paper, *Engineering Record And Replay For Deployability* ([arXiv 1705.05937](https://arxiv.org/abs/1705.05937)),
read through a page query of the PDF:

* **Recording boundary.** §2.1: the boundary is the user-space/kernel interface; the inputs are mainly
  system-call results and the timing of asynchronous events.
* **Threads are serialized.** §2.2: racing accesses across cores are a source of nondeterminism, so rr runs
  **one thread at a time**, supervising tracees through ptrace, and preemptive context-switch timing "must be
  recorded".
* **Placing asynchronous events.** §2.4: progress is measured with a hardware performance counter, retired
  conditional branches (§2.4.1), paired with full register state to identify an execution point. The paper
  states replay would probably diverge if an event lands on an instruction revisited with no intervening
  conditional branch.
* **Nondeterministic instructions.** §2.6: `rdtsc` is trapped, emulated and recorded.
* **Shared memory.** §2.5: rr avoids shared-memory channels with the outside world (PulseAudio, X server,
  GPU drivers, vdso syscalls) by falling back to sockets, disabling direct GPU access and patching vdso.
* **Limits.** §5.1: porting to **ARM failed** because load-linked/store-conditional atomics make retired
  instruction/branch counts nondeterministic; VMware "VM exit clustering" also breaks the counter.

PANDA manual ([panda-re/panda](https://github.com/panda-re/panda/blob/master/panda/docs/manual.md)): deterministic
replay records the *non-deterministic inputs that cross the boundary around CPU and RAM*; it records changes to
CPU state and memory rather than device inputs, which is why one cannot "go live" mid-recording.

## What this means for the Parity workflow

1. **Record-only phase rings cannot become replay.** Our shared entry/return phase counter orders the calls we
   hook, but, as rr states for its own design, thread interleaving itself is nondeterminism that replay must
   control. This agrees with `docs/research/stock-replay-boundaries.md`: recorded responses alone do not
   reproduce a schedule.
2. **A counter-based approach is unavailable on this host.** The host is ARM64 running x86 code through
   Box64/WowBox64 dynarec; rr's own authors report the ARM atomic model defeats its progress counter. Do not
   plan an RCB-style mechanism here.
3. **The serialization idea is portable; the counter is not.** The transferable lesson is to run one game
   thread at a time and record switch points. At our API boundary that suggests a candidate: a single global
   run token released only at hooked calls (yield points), with the release/acquire order recorded. This is a
   proposal only. It would not cover code that runs between hooks, busy loops, or spin-waits on shared memory,
   and its effect on Stock behavior is unmeasured; any such mechanism needs its own owned qualification.
4. **Whole-VM replay stays the unrun alternative** (`docs/research/whole-vm-stock-replay.md`); PANDA's model
   (record what crosses the CPU/RAM boundary) is the primary-source description of that route.

## Not resolved by these sources

* Safe interception of DirectInput COM calls and window-message delivery. The curated list covered TTD, not
  hooking; the hooking results found by search were tutorials, not primary sources. Primary candidates still
  to read: Microsoft Detours documentation, MinHook's repository, and Wine's `dinput` source for the exact
  `S_FALSE`/`S_OK` and device-state semantics under Wine.
* Whether a run-token scheduler is compatible with Stock's worker threads (audio, playlist) without
  deadlock or timing drift.
