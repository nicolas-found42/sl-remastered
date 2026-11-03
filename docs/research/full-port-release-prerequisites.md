# Full Port production entry and release prerequisites

Issue #53 remains incomplete. This records the production entry slice and early
prerequisite observations; it does not qualify a distribution or release.

## Production entry

`sl_game` builds `Street Legal Racing.app` as a native ARM64 application. Its
`src/game_main.cpp` launcher resolves `Contents/Resources` through CoreFoundation
and per-user Application Support through `SDL_GetPrefPath`. It executes the same
VM, initialization, rendering, input, normal intro and lifecycle implementation
as the diagnostic adapter, now in `src/game_application.cpp`. The diagnostic
executable remains `startup_probe`; its existing CLI is preserved.

The player launch requires `Resources/Game/system.rpk` and the complete reconciled
Game tree, plus `Resources/StockCursorResources.exe`, an explicitly supplied
cursor-resource carrier. Packaging must bind this carrier's provenance and digest.
The application never looks for an external `ref/` folder. Its saves/settings live
beneath the per-user `save/` tree and logs beneath `logs/`; launch creates only
external directories and never copies or replaces existing progress from Game.

`tests/game_application_paths_test.cpp` exercises actual filesystem boundaries,
existing progress preservation across app replacement, missing resources,
application/data overlap, relative paths and escaping content/save/log symlinks.
The test is registered with CTest and uses AddressSanitizer/UndefinedBehaviorSanitizer.
These filesystem observations do not prove a successful game save/reload or
clean installation.

## Bound evidence

`local/verify/issue53-production-entry-20261102-v2/result.json`
(SHA-256 `08ed08380f0a0d97ccffb7a12f4f7435461481eb2b10424d8c0e979718ea6301`)
retains eight successful console stages, with `parity_verified: false`:

- `0.log` binds source and built executable SHA-256 identities.
- `1.log` is the external-state filesystem test's empty successful output.
- `2.log` observes the shared diagnostic startup selecting `java.game.Init`.
- `3.log` checks that the recorder's source manifest includes and exactly hashes
  the moved shared source, production launcher and thin diagnostic adapter.
- `4.log` validates the generated application plist.
- `5.log` validates the ARM64 executable slice.
- `6.log` reports zero valid local code-signing identities.
- `7.log` reports zero registered repository Actions runners.

The first attempt remains retained at
`local/verify/issue53-production-entry-20261102/result.json`. It failed because
recorder `latest` symlink metadata was refused by the default artifact policy.
The console runner now accepts explicit `--artifact-links count`, using the
existing no-follow metadata accounting. Default refusal is preserved. The actual
refusal/acceptance regression passes; this option proves no symlink-target
isolation. Neither attempt launches a renderer, visible window or audible audio.

The bounded Jev check is retained in `jev-verification.json`. It verified all
three submitted claims, but requested review for console execution
(supports 0.77, confidence 0.66) and shared startup/source bindings
(supports 0.54, confidence 0.31). Signing/runner inventory received an automatic
verified judgment (supports 0.92). Those two narrow review requests require an
independent check of the actual result and source manifest; this is not a Jev
completion assessment.

## Remaining release prerequisites

On 2026-11-02, this host is ARM64 macOS 27.0.1, build 26A434. The Apple command-line
`notarytool` is installed. Local valid signing identities are absent. Notarization
account/profile access and a clean supported Apple Silicon test host remain
unestablished. The user's instruction is to defer credential and clean-machine
access requests to the final release qualification step; implementation proceeds.

GitHub CLI access has repository/workflow scope. Read-only listings found no
repository release, secret, variable or registered self-hosted runner. These are
bounded inventory observations, not a claim that external infrastructure cannot
exist. The current bundle still links Homebrew dylibs and has no selected bundled
content. Dependency closure, content reconciliation/provenance, reproducible
packaging, signing/notarization, clean-install gameplay qualification and the
completion gate remain required before 1.0.0 publication.

## Player logging correction

Independent review found that the initial production extraction still called
Git and scanned the embedded developer source checkout through the diagnostic
recorder. That violated the clean-player launch requirement.
`RunOptions::captureDeveloperSource` now defaults to diagnostic capture but is
explicitly disabled by the player entry. Installed logging retains the running
executable SHA-256, skips source scanning and Git subprocesses, ignores the
launcher source-hash environment variable, and reports unavailable bundled build
identity without claiming a source/content release identity.

`tests/run_log_test.cpp` uses a configured valid source fixture and a fake Git
shim that writes a sentinel. Installed mode emits no source manifest, leaks no
configured checkout path and leaves the sentinel absent; the diagnostic control
with the same fixture captures that source and invokes the shim. The regression
failed four assertions before the implementation and passed afterward.

`local/verify/issue53-player-logging-20261102/result.json` retains the source and
binary hashes, exact changed source copies, passing recorder regression, clean
clang-tidy checks and native ARM64 check. This supersedes the initial source
identity snapshot for current implementation claims. The full integration gate
and actual installed-player acceptance remain required.

## Subsequent implementation evidence

Player logging now explicitly disables developer checkout scans and Git subprocesses.
The existing RunRecorder seam's fake-Git/source-fixture regression failed before
the correction and passes with the diagnostic control retained. Bound result:
`local/verify/issue53-player-logging-20261102/result.json`
(`fbf8164dc75b527457b0a8491ed3c9a2e8574387b7beccf5cf2a69bb5e06aff9`).

`tools/release/stage.py` now stages an explicitly selected complete inventory,
cursor resource carrier and transitive ARM64 dependencies. `bundle.py` resolves
load paths, copies/thins libraries, rewrites references, removes host search
paths and ad-hoc signs nested code before the complete application. Six real
loader controls and ten staging controls cover transitive/diamond dependencies,
runpaths, universal libraries, path normalization and preservation/refusal.

The actual developer candidate is retained under
`local/verify/parity-goal-20261102-092437/factory-refusal-exact-env/issue53-staged-app-v1/`.
Its result SHA-256 is `2b71ed37b551e438d0f765526c8e80b7e352237210e2977875e6132959f4cc8f`.
The five stages pass; selected content contains 7,859 files, and the app contains
19 relocated libraries. Input file bytes and the final local signature seal
were checked. This candidate uses the older installed tree for packaging
qualification; it is not the reconciled release content, and was not launched.
`release_accepted` remains false.

Independent standards and specification reviewers pinned baseline
`ceaa48df09ed157e6517ce131236b9101c4721a5` and inspected this bounded working-tree
slice including new files. Both reported no actionable findings. They ran no
checks themselves and made no whole-product or clean-install acceptance claim.
The separate camera investigation and subsequent changes need their own review.

## Distribution phases and completion gate

`tools/release/distribute.py` now provides explicit Developer ID preparation,
receipt-based notarization submission/wait/staple, signed DMG creation and final
GitHub publication/download verification. `tools/release/README.md` documents
inputs, source/tag publication boundary, exact audit schema and the post-download
installation observation still required. Submission and wait responses are
retained separately; a failed wait resumes its saved ID and never resubmits.
Reference/Git targets and existing output paths refuse.

Publication validates the current canonical specification/map hashes, every
mapped requirement's verified status and evidence hashes, accepted source-bound
independent reviews/Jev/full-gate records, exact-DMG clean ARM64 acceptance,
clean checkout, committed map/spec, remote source tag, Developer ID/Gatekeeper,
signature and stapled ticket. These are mechanical identity and declaration
checks, not a semantic proof oracle. The current incomplete map refuses before
any GitHub publication command. Source refs are not pushed automatically.

`local/verify/issue53-release-distribution-20261102-v2/result.json`
(`72fbba5a2d456582ce1c545edaa14e344954d2cb164976277598be6b4428d567`)
records 13 distribution, six dependency and ten staging controls. The final
source-bound distribution-only rerun after service-response retention changes is
`local/verify/issue53-release-distribution-20261102-v3/result.json`
(`e2c1bbd3d8feefde5f39ed3c3f621488f737a0e2c0f4dbb3c35bc534fdef49ac`).
It passes all13 controls, including a real tiny unsigned DMG mounted read-only to
verify payload and Applications-link installation layout, rejected incomplete or
corrupt acceptance records, rejected changed downloads, and retained notary
handles/rejection responses. Apple/GitHub commands requiring credentials use
explicit test seams; this is not actual Developer ID/notary/release acceptance.
No real credentials, signing, notarization submission or publication was used.
The full integration gate, independent current-diff reviews, reconciled official
content, complete game Journeys and exact clean-machine release remain required.

## November 3 prerequisite revalidation

Read-only inventory at `2026-11-03T06:19:29Z` reconfirms the following. The
November 2 observations above remain historical; the later staged developer
candidate already closes the tested dependency-copying path, without qualifying
official selected content or an installed game.

| Command | Current output |
|---|---|
| `security find-identity -v -p codesigning` | `0 valid identities found` |
| `uname -m` | `arm64` |
| `sw_vers` | macOS `27.0.1`, build `26A434` |
| `xcrun --find notarytool` | `/Library/Developer/CommandLineTools/usr/bin/notarytool` |
| `gh api repos/nicolas-found42/sl-remastered/actions/runners` (projected fields) | `{"runners":[],"total_count":0}` |
| `gh release list --repo nicolas-found42/sl-remastered --json tagName,isDraft,isPrerelease,publishedAt --limit 10` | `[]` |
| `gh secret list --repo nicolas-found42/sl-remastered --json name` | `[]` |
| `gh variable list --repo nicolas-found42/sl-remastered --json name` | `[]` |

All eight commands exited zero. These repository-scoped lists do not establish
that organization/environment infrastructure is absent. No secret values,
Keychain passwords, Steam account data or login data were inspected. No
notarization profile was supplied or service authentication attempted; profile
access remains **unestablished**, rather than proven absent. No clean-machine
access or installation observation was established by this inventory. The
existing instruction to defer credential/clean-host requests until final release
qualification remains in effect.

Fresh SHA-256 comparisons also match all seven selected reference files in
`local/verify/issue53-reference-provenance-v2-20261102/local-acquisition-audit.json`
(audit SHA-256 `f1e9f0b7f25d2eb4d076193a8a1a2a5f95ffebad0c26a84a1c19d31d7c9fd7bf`).
The declared Steam library list still hashes to
`65c558ec438983d5c88edf2b803dcae5eae51c9dd675fb1ec0d18b881024e789`, names one
library, and that library has no `appmanifest_497180.acf`. This is a preservation
and bounded-location observation, not authenticated acquisition or a disk-wide
search. The [provenance report](reference-distribution-provenance.md) already
binds the older displayed Build 606 label and newer static Build 940 label;
the remaining prerequisite is each release's authenticated distributor/carrier
or depot-manifest-to-complete-file chain. Build labels alone cannot resolve it.

The release boundary still requires a usable Developer ID Application identity
and team, an authorized existing notary Keychain profile, and supported clean
Apple Silicon access for the exact post-staple DMG's eleven acceptance checks.
`tools/release/distribute.py:429–479` requires current canonical map/spec hashes,
every requirement verified, source-bound accepted reviews/Jev/full gate, and
exact-DMG clean installation observations before publication. There is no
credential prerequisite for continuing product/parity work, semantic content
classification, release-tool controls or the final acceptance protocol's
implementation. Do those next; defer actual signing/release until the product
completion gate is satisfied.

A bounded Jev verification of the eight raw inventory outputs verified the
host/signing and repository-list claims automatically (supports `0.99`,
confidence `0.98` each). It also judged the statement that these inventory
outputs do not establish actual signing/notary/publication acceptance verified,
but requested review (supports `0.79`, confidence `0.68`). Direct inspection
confirms these commands only list local identities, tool/host metadata and
repository metadata; none executes signing, upload or publication. This narrow
review resolution does not provide any of that missing acceptance. No capture,
game, installer, REA session, signing or service write was launched. No full
integration gate was run for this documentation-only revalidation.
