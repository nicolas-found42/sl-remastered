# Release distribution review — 2026-11-02

Scope: independent source review of the new release helper, its documentation,
regression controls, and the distribution-test line in `tools/check.sh` against
`CODING_STANDARDS.md` and Full Port 1.0.0. This is a bounded release-tool slice;
whole-product standards/specification reviews and all final release acceptance
remain required. No real signing, notarization, credentials, uploads or publication
were used. The user's request defers signing access to the last stage.

## Original evidence and findings

Reviewed original source identities, retained under
`local/verify/issue53-release-distribution-20261102-v3/source/`:

| Source | SHA-256 |
|---|---|
| `tools/release/distribute.py` | `a45103595a965b27b42ac165fc6d99b049dbb9cda166c91c34284e3bc3b607c4` |
| `tools/release/README.md` | `823f5b8d554dd16a3e88bcaa052f9eaff724e19ec13804159ec2f07f80316809` |
| `tests/test_release_distribution.py` | `77392216e1d1f2320c7dbd682637d189119eecd0ae7973181ec8cdd283017e38` |

- **RD01, P1, US03/US46:** original `publish` validated the clean-install DMG
  identity at line 280, then established a new upload/download hash baseline only
  at line 313. A change between those points could be uploaded and reported as
  verified despite differing from the clean-accepted image. The retained
  `original-identity-drift-expected-red` stage executes the original source with
  a real fixture audit and a process-boundary mutation: accepted hash
  `c957b94450010eea1467c6560fa02a783e906a6967906876b0de56361b4acaab`, uploaded hash
  `b530ac3760163f1656608308cf5ddb85ea20446e58f0986bdc8ce0982cab5894`, and
  `download_verified=true`. Its expected exit is 1; this is evidence of the old
  defect, not a passing original publisher.
- **RD02, P1, US46:** original lines 305–307 checked the tag through local
  `origin`, whereas lines 315–316 published to the fixed project repository.
  A fork/mirror tag could therefore authorize a release whose actual repository
  tag identified another commit. GitHub CLI's `--verify-tag` requires existence,
  not equality to the accepted commit. The fix's control supplies an accepted
  fork tag and a different publication-repository tag and requires refusal before
  release creation.

The bounded `jg` discovery was incomplete at its request limit; both selected
files and their actual call paths were then read directly. This review does not
infer absent behavior from the search limit.

## Implemented corrections, pending independent re-review

RD01 now freezes DMG/audit/notes before validation, checks those exact frozen bytes
through the workflow, creates a private draft, validates remote asset identities
and actual draft downloads, repeats the acceptance/source/tag checks, and only
then publishes the saved numeric release ID. Public downloads are checked again.
Changed original inputs cannot alter the frozen upload, and changed frozen inputs
refuse before release creation. This narrows incidental mutation races; the tool
does not claim an atomic transaction across local storage and GitHub APIs.

RD02 now queries the canonical publication repository directly rather than
trusting `origin`. The current helper retains creation/publication intents and
draft IDs before subsequent service actions. `resume-publication` inspects an
existing handle after creation, download or publication observation failure,
without creating another release or replacing existing assets. A failed preflight
cannot be resumed into draft adoption. Receipts/intents are flushed before the
next service action.

Current implementation bindings:

| Source | SHA-256 |
|---|---|
| `tools/release/distribute.py` | `185b247e266478fb58dec12ed99e005e9b8c4354702468c183901d519194992a` |
| `tools/release/README.md` | `d05f68aeedfd2304a765228521589ef7ed914f231acb12454c3bdbabb7c152f2` |
| `tests/test_release_distribution.py` | `24fc9481ab60e31909a8d31f5e1b766401909c61142701bf03b13168c71e9c6b` |

Verification is retained in
`local/verify/issue53-release-review-fixes-20261102/`: original expected-red
reproduction, current distribution controls, undefined-name checks and unchanged
source bindings. Controls simulate the GitHub/signing process boundary, use real
fixture audit validation, and include a real tiny unsigned DMG layout check.
They do not qualify service credentials, actual signed application content,
notarization, clean supported installation, downloaded-game launch, or the full
integration gate. The whole requirement map remains the acceptance authority.

Authoritative service semantics consulted:
[GitHub CLI release creation](https://cli.github.com/manual/gh_release_create),
[release view fields](https://cli.github.com/manual/gh_release_view), and
[GitHub release API](https://docs.github.com/en/rest/releases/releases).
