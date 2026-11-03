# D6 next production slice: sole Stock CREATE owner under existing guardian

Question: Can the production Stock path keep every resource reserved after launcher/coordinator death, clean only its immutable container ID, and refuse retirement after uncertain CREATE, using the existing console guardian plus protocol2 intents?

Answer from source: accounting can remain fail closed. Safe automatic recovery of an unknown CREATE is not established and must remain refused. The current production path is not qualified for pause. This is a source-backed design, not runtime Docker evidence or an implementation.

## Existing boundaries

- tools/parity/run_stock.py:455-487 combines daemon creation/start with docker run --rm. It writes launch.json, starts the CLI, then polls inspect by mutable name. Configuration isolation is audited only after execution has already begun.
- run_stock.py:608-614 and 800-806 clean by mutable name and accept empty name-filtered ps output. Neither is an authoritative CREATE cancellation barrier. Python finally cannot survive both launcher/worker death.
- run_stock.py:730 and 779 use the existing default v1 pool and run_observer. Nothing should enable protocol2 or migrate that pool in this slice.
- resource_pool.py:363-416 console guardian owns a host group independently, retains inherited lease descriptors, gates spawn on launcher GO/EOF, reserves the root PID with WNOWAIT, drains on EOF, and returns bounded status. Its scope explicitly excludes daemon-owned objects.
- resource_pool.py:568-614 run_observer retains FDs but launcher loss is not a Docker cleanup request. focus_observer.m:46-69 is an in-process watchdog thread, not an independent daemon-container guardian.
- resource_pool.py:114-123 and 867-906 provide only durable create_may_be_pending intent registration. No bind, terminal, reset or retirement API exists. New schema transitions require review, not ad hoc record rewriting.
- retained watchdog-runtime-v3/lifecycle.py supplies acknowledged FD identities, peer EOF/deadline monitoring, and hold_on_failure; supervisor.py supplies receipt-before-GO. Its launchd/coalition machinery is not needed merely to own a Docker object and should not be copied into a second lifecycle framework.
- stock/state_flow.py:70-98 already takes a container argument; supply immutable ID instead of name. It currently uses ambient docker from PATH, so endpoint/CLI ownership must also be bound before live integration.

## Smallest coherent production integration

Extend existing resource_pool.py console guardian with one closed Stock-only execution mode, called from run_stock.py. Keep ordinary console mode unchanged. Do not introduce a generic callback/plugin lifecycle service. The guardian must be the sole CREATE issuer, not trust an ID file produced by a guest-writable worker. Its inherited permit and intent-owner FDs are acknowledged by exact inode/device identities before CREATE. The coordinator launches the existing focus observer/worker after the guardian's GO receipt. The guardian remains outside that owned host group and watches launcher EOF, coordinator completion and the original execution deadline.

Use stopped docker create (no --rm), not docker run. Bind exact executable, daemon endpoint, image digest, generated name and normalized immutable launch configuration hash. The guardian runs one bounded create operation. Only a complete successful operation yielding exactly one full 64-hex ID can proceed. Strict inspect by that ID must match image/name/isolation. Commit the ID into the existing envelope-bound intent before any START authorization. All later exec/start/attach/inspect/remove arguments use that ID. The original timeout must include creation/handshakes; cleanup has a separately explicit bounded grace and keeps all accounting during failure.

On EOF/deadline: refuse any new CREATE/START permission, drain owned host command group using existing guardian identity rules, then perform bounded exact-ID removal. A successful exact-ID removal response is the candidate terminal evidence; an empty inspect/ps, CLI death, name absence, deadline, or lost socket is never terminal evidence. If CREATE never returned a qualified full receipt, remain create_may_be_pending, never retry CREATE, and never retire even if name lookup is empty. If both holders die, the durable barrier continues to block admission.

Known-ID state needs to persist immutable ID, original spec and exact response provenance under the existing envelope hash/identity binding. Terminal state must retain the same identities plus successful exact-ID deletion receipt, with no pending CREATE issuer. Retain a tombstone in the bounded registry rather than deleting records during admission. Atomic envelope commit last makes interrupted state transitions refuse admission. Current 128-entry and byte bounds remain; exhaustion refuses rather than expanding limits.

## Unresolved lifecycle decision before implementation

Choose the next accepted scope explicitly: (A) guardian CREATE/ID-binding/cleanup only, permanently retaining the durable intent even after success, or (B) complete success-path terminal transition that releases its logical reservations. A cannot support repeated actual captures; B changes the strict admission state machine and needs independently qualified backend terminal semantics. Neither permits unknown-CREATE recovery. I recommend B as the smallest useful vertical slice, but do not implement a retirement shortcut under an existing create-only API.

The concrete qualification gap is not a need for a new leasing framework: source currently does not pin Docker endpoint/context, prove stopped-CREATE response completeness, or establish exact-ID deletion as a terminal barrier after uncertain START/ATTACH. Before relying on B, bind the actual local Docker executable/endpoint and inspect primary API/implementation semantics, then run one console-only private-container matrix. No paused Docker/game is needed. A fake backend process can test the production orchestration/death seam first, but cannot qualify daemon semantics.

## Tests at existing seams

Existing test_run_stock.py exercises the new run_stock/guardian entry point with a bounded fake daemon process and scratch protocol2 pool; existing test_resource_pool.py covers typed bind/terminal transitions. Reuse actual processes and synchronized gates; do not only mock subprocess.run.

1. Launcher SIGKILL after acknowledged escrow: guardian removes exact bound ID, no wrong-name deletion, capacity unavailable before completed terminal receipt.
2. Coordinator/focus-observer SIGKILL: same exact cleanup; guardian remains alive outside their group. Omission mutant places guardian in killed group, yielding retained unresolved intent and no cleanup receipt.
3. Guardian SIGKILL: coordinator detects EOF and exits; durable intent still prevents subsequent sentinel command even after all kernel permit locks are independently shown free.
4. CREATE accepted but response delayed/lost, then launcher/coordinator/guardian death: fake daemon creates late after earlier empty lookup. No START, no second CREATE, no terminal transition; new admission remains blocked. Empty-lookup-retirement mutant admits sentinel and is red.
5. CREATE full success then death before binding / after binding before START: first case remains unresolved; second cleans exact stopped ID. No state grants START before durable ID binding.
6. Wrong/truncated/multiple ID, changed image/config/name/daemon endpoint, malformed status, failed durability: no START and no retirement.
7. Exact-ID remove timeout/error/lost response or unrelated same-name container: retain barrier; never remove by name. Successful remove of known ID must not retire an unresolved second creation (one lease/one intent invariant).
8. Terminal transition crash before envelope commit: admission refuses; terminal success with complete bound record is the only capacity-release case. Replaced owner/record/permit and cached protocol2 clients remain fail closed.
9. Original deadline covers FD receipt/CREATE/binding; EOF before GO causes zero CREATE. Bounded output and descriptor count are enforced. Existing FIFO/healthy unreserved concurrency remain intact.

After process tests, one permitted console-only Docker matrix should qualify success, launcher/coordinator death, unknown CREATE receipt, exact-ID deletion, daemon unavailability and context mismatch. Future pause adds pause timeout/unpause failure tests only after this lifecycle slice is accepted; no actual pause is authorized here.

No implementation, migration, Docker invocation, captures or REA were performed in this investigation. Jg used the explicit tools/parity DIRECTORY root with 30 requests/22000 output bytes; it reported request-limit incomplete context. Exact source reads above resolve the named boundaries, not every possible backend behavior.


## Follow-up authorization and decisive host-drain mismatch

Parent subsequently authorized narrowly gated success-path retirement: one guardian-owned completed stopped CREATE with immutable ID durably bound before START/GO, then qualified exact-ID DELETE success plus independently drained coordinator/child group and closed writers. Worker JSON must not confer binding/terminal authority. Parent requested neutral created_bound and append-only state/receipt history. No live integration/migration/pause was authorized.

Before implementing, exact reads found a safety mismatch in the proposed console wrapper: focus_observer.m:249-250 uses POSIX_SPAWN_SETPGROUP and pgroup0, making the worker's PGID its own PID. This creates a new group, not a new session. The focus observer alone owns waitid WNOWAIT at326-331 and reaps at365. resource_pool._console_guardian only reserves its direct root PID/PGID at343-349, cleans that group at397-405 and cannot independently reserve/reap the separate worker after the observer dies. Therefore wrapping the current observer does NOT establish the terminal writers-closed receipt. No transition implementation was made on the assumption that it does.

Recommended topology decision: existing console guardian directly parents the suspended Stock worker in its own private PGID and retains WNOWAIT until drain. Existing focus observer gains a narrowly closed externally-owned-root observation mode, with pre-GO identity+FD/EOF attestation, and observes the worker without claiming its wait relationship or reaping/signaling it. Guardian holds worker suspended until observer has registered prelaunch observation and acknowledged exact birth identity. Guardian then permits execution. Observer/coordinator death or missing foreground observation triggers guardian cleanup; guardian death leaves durable barrier and causes observer refusal. The existing foreground evidence classifier currently requires child_started root.pgid == root.pid (focus_evidence.py:69), which remains true for the worker; externally-owned mode needs an explicit attested event, not fabricated child_started spawn ownership.

Alternative reviewed shape is to make Stock-only observer workers inherit the observer-root group, so an outer guardian reserves the whole group; however existing observer killpg(child), pgid==child assertions and drain logic would all require a separate cleanup policy. It must not quietly relax ordinary observer ownership validation. Choose this only if it is smaller after reviewing foreground semantics. Do not adopt a generic new leasing/watchdog framework.

Proposed transition APIs remain guardian-private: bind_stock_created(intent capability, completed owned CREATE receipt) and finish_stock_deleted(intent capability, completed owned DELETE receipt, independently observed drained-root/writers receipt). Public worker channel can request preparation-ready/stop, never submit these capabilities or raw JSON authorizations. Keep state create_may_be_pending -> created_bound -> terminal_deleted, retaining all prior receipts/history under strict bounded schema and envelope-last commit. The process-owner topology must be settled before terminal capability exists.

Official Docker Engine v1.47 OpenAPI retained in docker-api-v1.47.yaml: CREATE operation at7039; DELETE at8541 distinguishes204 successful removal from404 no such container and documents force kill. START at7943 has204/304/404. These API descriptions support the intended response distinction, but do not by themselves qualify the local endpoint, actual CLI response framing, or host writer containment. Source URL https://docs.docker.com/reference/api/engine/version/v1.47.yaml . No daemon request was run.


## Pause boundary

The parent chose the direct-parent worker and private observer-attach handshake. The user then paused work before any host-handoff or lifecycle implementation. The observer subagent made no source edits or builds. All owned processes and REA sessions stopped. This remains a design; do not activate it without explicit resume and qualification.
