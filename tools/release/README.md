# Full Port distribution workflow

`stage.py` selects exact resource bytes and closes transitive native dependencies.
`distribute.py` continues from that retained staging report. Every output must be
fresh; failures and submission receipts remain available. Run each phase through
the shared resource/retention wrapper, with current capacity and an adequate
already-authorized artifact reservation. No phase downloads game content.

No real signing, submission, credential request or publication has been performed
by implementing these tools. Credentials and clean-system access are the user's
last step. The current product and requirement map are incomplete and publication
refuses them. Local tests use fixtures, including a real tiny unsigned DMG, and
must not be presented as release qualification.

## Candidate phases

1. `python3 -I tools/release/distribute.py prepare --application STAGED.app
   --stage-report STAGE.json --output NEW_DIRECTORY --identity CERTIFICATE_SHA1
   --team TEAM_ID` verifies every staged app file, copies the app, sets version
   1.0.0, signs nested libraries/executable before the complete bundle with
   Developer ID, hardened runtime and secure timestamps, verifies the signing
   team, preserves the source, and writes `application.zip` plus an exact manifest.
   The certificate fingerprint identifies a local identity; secrets are never
   accepted as command arguments.
2. `... submit --artifact NEW_DIRECTORY/application.zip --staple-target
   NEW_DIRECTORY/Game.app --profile EXISTING_KEYCHAIN_PROFILE --receipt NEW.json`
   flushes an exclusive `NEW.json.intent.json` before the only upload. It binds
   artifact SHA-256, original target inventory, profile name and request time.
   Service stdout/stderr and failures/timeouts remain in `NEW.json.service.json`;
   a complete returned ID is saved even when observation times out. A repeated
   submit with that receipt path refuses before another service call.

   If upload outcome is unknown, use `... resume-submission --receipt NEW.json`.
   It never uploads. It inspects history and info using the saved profile, then
   requires exactly one completed log with matching job ID, archive filename and
   uploaded SHA-256. Filename/time alone cannot authorize recovery. Pending logs,
   missing, excessive or ambiguous matches and changed original inputs refuse;
   preserve the intent and retry recovery later, never create a new receipt to
   bypass an unknown upload outcome.

   `... finish --receipt NEW.json --output NEW_FINISH.json` waits on the existing
   ID and requires `Accepted`. It copies and verifies the unchanged original
   target into a fresh retained attempt, then staples and validates that copy.
   The result's `staple_target` identifies the **finished copy** to use downstream;
   the submitted artifact/original target stays unchanged. Repeat finish with the
   same output after a wait, staple or validation timeout/failure. Every attempt
   and command diagnostic is retained; a partial copy is never reused. An existing
   successful output refuses replacement. A changed original target still refuses.
3. `... dmg --application FINISHED_COPY_FROM_REPORT --output NEW_IMAGE.dmg
   --identity CERTIFICATE_SHA1 --team TEAM_ID` requires the app's valid stapled
   ticket and Developer ID, copies it to a retained payload directory with an
   `/Applications` link, creates/verifies a compressed HFS+ UDIF and signs it.
4. Submit that exact DMG with `--staple-target NEW_IMAGE.dmg`, then finish its
   existing receipt. The app is stapled **before** the image is made; the final
   signed image receives its own notarization/staple. The DMG finish report likewise
   names a finished copy: bind clean-system acceptance/publication to that exact
   post-staple copy, not the original unstapled image. Preserve both reports.

Recovery uses the completed notarization log identity documented in Apple's
[custom workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow);
`notarytool log` is unavailable while a job is incomplete. Local `notarytool` help
was inspected without credentials or contacting the service. Tests use a stateful
service fixture for accepted-but-timeout uploads, retained handles, history/hash
qualification, wait timeouts and partially mutated staple/validation failures.
They are not real notarization or signed-ticket qualification.

The recipe and input/content/dependency identities are reproducible. Do not claim
bit-identical signed DMGs: signing timestamps, filesystem metadata and tickets
are variable. Bind clean-system acceptance to the exact **post-staple DMG SHA-256**.
A signed/notarized fixture, model judgment or successful package creation is not
acceptance of the complete game.

Apple's current distribution instructions are the authority for the tool flow:
[notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution),
[custom workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow),
and [distribution packaging](https://developer.apple.com/documentation/xcode/packaging-mac-software-for-distribution).

## Completion gate and publication

The canonical requirement map remains
`docs/research/full-port-1.0.0-requirements.md`; this workflow adds no second map.
A final audit JSON has schema `1`, version `1.0.0`, exact `source_commit`, signing
`team`, and `{path,sha256}` bindings named `specification` and `requirement_map`.
Its `requirements` object has exactly every stable map identifier, each with
`status: verified` and a nonempty array of hash-bound `evidence`. The actual
canonical map must mark **every row verified**; declaring a complete audit while
the map remains incomplete refuses. US01–US46 and D1–D8 are mandatory even if
accidentally removed from the map. Additional authority, workflow, testing and
execution requirements in the map remain mandatory.

`standards_review`, `specification_review`, `jev_completion` and
`integration_gate` bind actual retained final records with the same source commit,
`accepted: true` and `unresolved: []`. These are audited declarations backed by
real records, not a substitute for the independent reviews, Jev assessment or one
successful full integration invocation. Do not fabricate an accepted wrapper
for an unsuccessful or unexecuted check. The tool mechanically validates bytes
and declared completeness; independent final review establishes their meaning.

`clean_installation` binds a retained record with exact `dmg_sha256`,
`source_commit`, `architecture: arm64`, `clean_machine: true`, `hardware`, `macos`,
`tested_limits`, nonempty bound `evidence`, and exactly these successful `checks`:
`signature`, `notarization`, `gatekeeper`, `dependency_closure`, `first_launch`,
`input`, `audio`, `settings`, `save_reload`, `replacement_preserves_progress`,
`sustained_play`. This record must derive from real supported clean hardware and
the exact image, including normal visible/input/audio behavior.

Only after independent acceptance, publish the accepted source commit and tag
`1.0.0`. `... publish --audit-path FINAL_AUDIT.json --dmg FINAL.dmg --notes
NOTES.md --output NEW_PUBLICATION_DIRECTORY` first copies the DMG, audit and notes
into read-only files in a fresh owned directory and records their initial hashes.
Validation and uploads use those copies; later changes to original paths cannot
change the accepted artifact. The output is retained even on refusal.

Before any service write, the helper requires the complete audit, a clean accepted
source checkout, canonical map/spec bytes present in that commit, the exact
publication repository's tag resolving to that commit, and the final image's
signature, Developer ID, stapled ticket and Gatekeeper assessment. It checks
`https://github.com/nicolas-found42/sl-remastered.git` directly; a fork or mirror
named `origin` cannot authorize another repository's release.

The helper creates a **private draft**, records its numeric release ID, uploads
only missing accepted assets without clobbering, checks remote asset IDs/sizes/
SHA-256 digests, and downloads both the DMG and audit for exact comparison while
the release is still private. It repeats source/evidence/input/tag validation
and inspects the unchanged draft before setting `draft=false` on that exact ID.
It then verifies the public downloads too. No incomplete draft is a public game
release. A successful publication record still leaves
`downloaded_installation_verified: false`.

If creation, upload, download or publication observation fails, preserve the
output and run `... resume-publication --output EXISTING_PUBLICATION_DIRECTORY`.
This command inspects the saved handle, rechecks the frozen inputs and full
acceptance gates, and continues that release. If creation returned no ID, it
inspects the already-requested tag to recover the existing draft; it never
creates another release. If publication timed out after taking effect, it
observes and verifies the existing public release instead of publishing again.
Unexpected, missing-digest or changed assets refuse; the helper never replaces
or deletes them. Every download attempt uses a fresh retained directory.

Receipts and publication intents are flushed to disk before the next service
action. Source publication itself remains a separately gated step; the helper
does not push refs. The service workflow follows the official
[GitHub release API](https://docs.github.com/en/rest/releases/releases) and
[GitHub CLI release commands](https://cli.github.com/manual/gh_release_create).
Tests simulate signing/GitHub responses and exercise a real tiny DMG; they do not
qualify real signing, credentials, uploads or the complete game's installation.

Publication is not the final installation observation. Verify installation and
launch of the actual downloaded DMG on clean supported Apple Silicon, retain the
post-publication evidence, and complete the final requirement audit before
claiming the user-requested release complete.
