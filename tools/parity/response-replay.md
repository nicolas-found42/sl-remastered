# Owned recorded-response and Port clock foundation

`response_replay.py` consumes a validated format-3/4 actual Stock API recording
through the existing interval compiler. The caller supplies an explicit bijection
from owned harness tokens to capture lanes. Those tokens are not native thread
roles. Entry and return consume the original shared phase timeline; no schedule
is inferred from JSONL order, entry/return counters or observed outcome labels.
Only the creating Python thread may drive a session. Other actual harness
threads, unknown tokens, wrong phases/callers/arguments/machine state, malformed
requests, missing output buffers and exhausted/unfinished streams latch the first
failure. There is no fallback to a live clock or key poll. Input/callback/RNG
streams are not implemented and refuse.

Accepted returns publish recorded QPC/QPF eight-byte output into owned caller
storage and return recorded EAX/LastError/CW/MXCSR words. The Python caller does
not apply these words to a native Stock frame or CPU/FPU state. The retained
actual phase tape in `local/verify/tracked-stock-phase-record/` has SHA256
`482aa1b5fa1ad47d8efec06766c61f43ad5ef7fa2073e4f57fd64ef743230e11`.
Two complete owned-harness consumptions each use 71,547 replies and 143,094 phases
across four explicit tokens. Their emitted reply hash is identical. Sixteen
actual-artifact tests, including malformed mixed-key/cyclic requests and a real
foreign Python thread, pass. This is an offline response-consumption repeat;
it is not a Stock same-side repeat or scheduler replay.

`performance_projection.py` consumes that entire phase stream and selects the
observed engine QPF/QPC boundary (`FUN_005516c0`). It retains the separate observed
frequency/epoch words, models the recorded x87 precision with nearest rounding,
and refuses unsupported or changing control modes. The immutable Stock PE is
hashed before reading its actual 1000f and 0.001f scale constants. These initial
words are not an atomic restorable checkpoint, and no actual Stock returned
float has been observed at this seam. Global writes are modeled at observed API
return phases; intervening memory reads and cross-lane writes are unobserved.
The outer engine-clock consumer is also unobserved: these samples are not
recorded Stock script `System.currentTime` invocations. Retained raw instructions
show FILD/FSUB/FDIV/FMUL with no intermediate FST or FLDCW in the two functions.

The projection generated 20,358 operands for the actual Port `systemCurrentTime`
function. `tests/recorded_performance_clock_test.cpp` supplies each operand through
`VmArena.performanceMilliseconds` and compares the resulting float bits with the
arithmetic model. The sanitized native helper has 20,358 callbacks and zero
mismatches. A zero-clock fault produces 20,358 mismatches and exits 1. Nine
counterfactual arithmetic/refusal controls also pass. This establishes bounded
Port consumption of this projected operand fixture; it does not establish a
paired Stock return proof or wire the callback into a game run.

Evidence, frozen source, logs, prior failed/preparation stages and advisory
reviews remain in `local/verify/parity-completion-20261102/stock-input-integration/`.
The final replay artifact is `response-replay-final.json`; projection inputs are
`port-clock-first.bin` with `port-clock-first.json`, and native outputs are
`port-clock-native-first.log` / `port-clock-native-zero-first.log`. The final Jev
gate remains unaccepted (`safe_to_apply=0.35`, composite 0.69785); it verifies the
bounded Port-consumption and non-completion claims at confidence 0.86, while its
replay-count claim remains low-confidence (0.44). Parent independent review found
and prompted the corrected malformed-request latch. That gate predates the
later public API/site crosspair guard and its added test. Review uncertainty remains
explicit; these artifacts are not approval to enable native replay.

The self-contained generated-protocol, projection, prefix and input-inventory
guards run from `tools/check.sh`. Generated guard rows are not Stock evidence.
The actual-artifact fixture requires a retained capture and fresh output path:

```sh
replay_evidence=$(mktemp -d local/verify/response-replay-XXXXXX)
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 2 --timeout 60 -- \
  python3 -I tools/parity/test_response_replay.py \
  --capture local/verify/tracked-stock-phase-record/game/dumps/stock-control-tape.jsonl \
  --report "$replay_evidence/actual-response-consumption.json"
```

All eight workflow deliverables remain the acceptance contract. Next mechanisms
include native Stock response application with ABI controls; real thread lifetime
and synchronization identity; replay of input/RNG/callback and scheduler streams;
corresponding Port channels; actual Stock returned-value/consumed-stimulus
observations; same-side game repeats and actual paired comparisons. The separate
experimental Stock input recorder still requires its latest owned runtime
controls and first actual boot/menu/dialog route capture.
