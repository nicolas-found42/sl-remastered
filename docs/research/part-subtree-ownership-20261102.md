# Detached Part subtree ownership, 2026-11-02

Reference decision: prefer the newer Part callback's child wrapper. Registry removal and addition traverse mounted slot descendants in preorder; a detached root owning itself must not suppress removal of its registered descendants. This is bounded static evidence, not full lifecycle or Journey acceptance.

## Source and admission

Older source SHA256 `7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`; parent-qualified evidence `local/verify/issue53-career-journeys-20261102/subtree-owner-rea.json` and `subtree-removal-rea.json`, corresponding posts/lease-finish records. Historical decompile leads are local/census/all.c functions00470b60,00470ff0,00471620,004723f0,00474170.

Newer source SHA256 `f44fb88a4335c657e20701cdb84afb8d1d85f7c3aceb93d30113e6e6e44fefc2`. Evidence under the existing registered8MiB root `local/verify/issue53-texture-rea-20261102`:

- `subtree-ownership-newer-v1`: fourteen bounded read_bytes operations, automatic open/query/close; holder67162 exit0/released within180seconds. Sequence records23:28:58–23:29:57UTC approximately; exact timestamps are in sequence.json. No source execution or mutation.
- `subtree-ownership-review-v1/postchecks.json`: all fourteen REVIEW, injection0.31–0.43. Manual review identified provider boilerplate and fixed hexadecimal target payloads only; no directives followed, no SKIP consumed or rerolled.
- `subtree-ownership-decoder-v1`: all fourteen raw slices equal exact immutable PE bytes, source SHA checked before/after; bounded LLVM instruction supplements, result0.
- `subtree-install-caller-newer-v1`: three bounded caller/control-flow reads, automatic close; holder85242 exit0/released within180seconds. First query23:33:39.615; last query23:34:58.270UTC; exact open/close timestamps in sequence.json.
- `subtree-install-caller-review-v1`: all three postchecks PASS.
- `subtree-install-caller-decoder-v1`: all three raw slices equal immutable PE; bounded decoder, result0.

No budgets were resized, no broad unknown function inventory was queried, no code was edited by this investigation. Behavioral jg discovery was run with explicit src root, request limit3, and reported incomplete/request-limit; exact source reads followed.

## Newer callback fixes the older parent-wrapper oddity

The newer Part helper004c17b0 receives parent at[EBP+8], child at[EBP+0xc]. Registry insertion4c17eb..4c1808 updates the parent's list at+0xfc/+0x104. Child wrapper creation4c18de reads[EBP+0xc], then pushes[child+0xc] into OwnerLink assignment0040cac0. Evidence `part-callback-child.json`, id `ev_639dfbe0177b8dd100e0b409d06acf49ea98d311c804eb9b1e4ac5010faf2689`. It dispatches `addpart` using string0059eb00 at4c191d after insertion and wrapper creation. The immediate caller4c4a2b pushes the parsed child then parent before004c17b0.

Older00470b60 inserts the child in the parent's registry but wraps parent+0xc for its script callback; newer wraps child instead. Do not reproduce the older parent callback argument in production. Native dispatch order is established; synchronous versus deferred completion of script callbacks is not qualified by these slices.

## Owner propagation and registry order

Newer004ca390 (older00470ff0) emits `add_part %d` to current ownerLink at root+0xc0 when owner cached ID+0xc8 differs from self ID+8. This root command precedes mounted descendant traversal. Only present slots with flag2 are traversed, not arbitrary World children.

At4ca8ee the child's OwnerLink+0xc0 is rebound using the parent's owner pointer+0xcc. Child+0xd0 and+0xac are copied, then004ca390 is called recursively with recursiveFlag1. Evidence `owner-child-transfer.json`, id `ev_01506bbb5c868836c1f3058102401e747a4e90c413934882995e8c8cbd6f5694`. The original root alone emits its final update command when recursiveFlag0 and owner!=self.

Newer004ca980 (older00471620) emits `rem_part %d` before mounted descendant recursion. Its initial owner==self branch skips only that node's rem_part command and proceeds to4caa64; the later flag2 child loop still executes. Recursive call4cb005 receives recursiveFlag1. Evidence `remove-recursive.json`, id `ev_fbc1bc86fbd4219c5eb3cfdd5abe32169d9bd135ef28d42621ccec677e85caf4`. Final update is guarded by recursiveFlag0 and owner!=self at4cb026..4cb035.

## Install caller: unregister before root owner replacement

Exact caller4c80d6..4c812a is `install-remove-before-owner.json`, id `ev_9a25076f08f304b259f7290d1154d99f75f16c67290b326ae407d16e50f993e5`. It calls004ca980(root,0) whenever root.owner cached ID is nonzero, including owner==self. Only after that does selector0x2b write the new owner into root OwnerLink+0xc0. It computes root+0xd0 and calls004ca390(root,0).

The self-skipping continuation is `remove-self-skip-continues.json`, id `ev_113a6018b551061036fe288bbe373ee4d5f10d972dca83a9c575bbb61d12ec59`. Descendant slot guard4caf30 is `owner-child-guard.json`, id `ev_66aefcc77fea4c97ad0c34fa841772a589ba518bf40e7136da8c4f9439e419b4`.

Thus detached door→Car installation must first unregister mirror from door, despite door owning itself. Then assign door to Car and register/rebind door and mirror in preorder. Do not skip the old-owner traversal because the root is self-owned. Root-self updatevariables is also skipped; descendants' old registrations still must be removed.

## Independent production review

The parent-owned new changePartRegistry helper uses the child wrapper for either Part or Car, updates the registry before callback dispatch, and the install transfer collects only flag2 children in preorder. The original early subtree fixture checks registry transfer and repair reachability but has no script callback observer.

Concrete review finding reported to parent: the prior-owner updatevariables condition checked only previousRootOwner, incorrectly including previousRootOwner==partNode for a detached self-owned root. Qualified Stock tail skips that call. Parent owns the correction and its callback-order regression. This report does not claim a final post-correction source audit.

Physical detach caller/re-rooting remains the next bounded question. Read-only newer lead4c8931..4c8977 appears to remove old registry recursively, bind the detached root owner to self, clear+0xd0 and use the add walker to propagate that owner to its preserved children. Those caller bytes are not yet REA-qualified here. Physical subtree detach/rebuild, slot preservation, and callback completion behavior remain unresolved; no unsupported physical operation was enabled by this investigation.

## Physical detach follow-up

The raw attempt subtree-physical-detach-newer-v1 closed/released9817 within180seconds, but all three raw postchecks SKIP relevance0.21..0.23; none was consumed, decoded or rerolled. A distinct decoded attempt subtree-detach-decoded-newer-v1 closed/released64497; its complete small function501d00 PASS relevance0.82, id `ev_ae529e7ad538e8835f5325185493f797c52f657f0736f0ada42916f570751e70`. The provider reports complete contiguous body501d00..501d93,148bytes,52 instructions. It unlinks the root from the external parent's child/sibling chain, frees its own attachment, clears its parent link/child flag, and does not traverse or clear its own child head+24. This supports preserving installed physical descendants rather than recursively detaching them. Called helper side effects are not completely requalified by this small procedure view.

Six isolated caller instruction results in that decoded attempt were SKIP relevance0.03..0.22 and remain unconsumed. Therefore newer physical caller self-owner/wheel-reset placement remains a historical/LLVM lead, not accepted fresh evidence from those skipped records. The parent accepted the physical descendant-preservation proof for later implementation; no C++ was changed here. All corresponding postchecks are retained under subtree-physical-detach-review-v1 and subtree-detach-decoded-review-v1.

A separate qualified scheduling result supersedes treating owner updatevariables as a direct script call: see car-deferred-refresh-20261102.md. Car commands set a pending native dirty bit and the frame refresh consumes it; they must not execute script updatevariables synchronously after each partial install.
