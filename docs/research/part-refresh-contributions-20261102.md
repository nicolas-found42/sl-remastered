# Installed Part refresh contributions

Research only; production integration is incomplete. Newer behavior agrees with the older callback's selector semantics.

Fresh newer `004a8060` is retained in `local/verify/issue53-texture-rea-20261102/part-contribution-newer-v1/part-contribution.json`, evidence `ev_3584dd66101a9798403db858952e9a2bf896be266144b55e6b18593a421aa7ff`, subject SHA256 `f44fb88a4335c657e20701cdb84afb8d1d85f7c3aceb93d30113e6e6e44fefc2`. Holder87059 performed open/query/retain/close automatically and released successfully within its180-second admission; sequence/admission/audit remain beside the payload. Precheck PASS; postcheck PASS (relevance .92), retained separately in `part-contribution-review-v1/post.json`. No skipped payload was used.

The parent-qualified full refresh `004aebf0` calls this callback for installed native Parts after script updatevariables and before chassis configuration copies. Callback selector is the packed uint32 at Part+0xb0: low16 chooses contribution, high16 chooses wheel. It is not the prototype category.

| Low16 | Newer destination | Older0043c520 destination |
|---|---|---|
|5|wheel+15c/160|same|
|6|wheel+164/168|same|
|7|wheel+16c/170|same|
|8|Car+1e9c/1ea0|Car+1e38/1e3c|
|9|Car+1ea4/1ea8|Car+1e40/1e44|
|10|Car+1eb4/1eb8|Car+1e50/1e54|
|12|Car+1eac/1eb0|Car+1e48/1e4c|

Each pair receives Part+b8/bc verbatim. Wheel modes check high16 against prototype+1fb4, then index Car+1414 wheel records at stride2e4 (older prototype+1f40, Car+13e4, stride2b4). Modes9/10/12 additionally retain the contributing Part's OwnerLink via0040cac0. Other modes do nothing. The decompiler hides that helper's receiver, so exact destination owner-link offsets are not independently established by this callback alone.

Historical older `all.c`00474170/00474000 shows inherited selector halfwords independently overridden by slot damageMode halfwords, with ffff wildcard, then propagated through mounted descendants. Port currently lacks that packed per-instance selector; replacing it with cfg.category or wheelId would lose behavior. Port wear stores external health (default1), while older setWear469420 stores1-input internally and getWear469340 returns1-internal. Thus a contribution must convert health to damage. Older setMaxWear469500 stores reciprocal input, whereas current Port stores the external argument directly. Newer wear/setMaxWear reconciliation is still required before declaring those conversions complete.

The wheel reset now provides all three destination pairs and a canonical17-value tyre state, but this report does not claim contribution installation, selector inheritance, engine pair semantics, damage accumulation, or complete driving parity. Parent owns post-script chassis configuration integration.
