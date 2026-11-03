# Reference distribution provenance

Official provenance of the two selected local collections remains unresolved. This bounded November 2 investigation distinguishes release-family metadata from a trusted distribution-to-file chain. It does not promote any file or content family to official status.

## Established observations

The retained [VERSIONINFO report](../../local/verify/issue53-reconciliation-20261102/versioninfo-v1/report.md) identifies the selected older executable as `2.2.1.0` and the newer as `2.3.1.0`. That initial metadata report alone did not establish Build 606 or Build 940; the later bounded observations below identify the selected local build labels. Their current complete SHA-256 values remain `7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde` and `f44fb88a4335c657e20701cdb84afb8d1d85f7c3aceb93d30113e6e6e44fefc2`, respectively.

Target-free REA certificate inspection observes no `IMAGE_DIRECTORY_ENTRY_SECURITY` table in either selected executable: `ev_11663154e6d65e2fbdb75c298671c40ccc7c1345917a70e4c5fa05edf3aa8fbe` and `ev_328fe738586f7cc1e5ab0597a21d1cc100b09563167a6b6d8d0facf3770d75e0`. This is absence of that PE table, not proof of absence of catalog/overlay signing. No signature, chain, timestamp or Authenticode digest validation occurred. Complete results and pre/post Jev judgments are retained in [queries.json](../../local/verify/issue53-reconciliation-20261102/provenance-v1/queries.json).

The [official Steam announcement feed](https://store.steampowered.com/oldnews/?appgroupname=Street+Legal+Racing%3A+Redline+v2.3.1&appids=497180&feed=steam_community_announcements) identifies a December 18, 2025 release named Build 940 for v2.3.1. It describes revised night-race betting and numerous fixes. This establishes an official release label; it supplies no executable/content hashes linking the selected local collection to that release. The local changelog has a Build 940 section, which is corroborating self-description, not authentication.

## Local carrier boundary

[Local bindings](../../local/verify/issue53-reconciliation-20261102/provenance-v1/local-bindings.json) retain fresh hashes of the two selected executables, readmes, install log, newer changelog/app ID, and the existing `.scratch/221-files/ex1` MSI, CAB, setup executable and setup configuration. The [inventory cross-check](../../local/verify/issue53-reconciliation-20261102/provenance-v1/inventory-crosscheck.json) compares the explicitly checked `ref/` files with the retained inventory; it is not a new full-tree snapshot.

`ref/raw/INSTALL.LOG` names a historical `SLRSetup.EXE` source and install date. It supplies no carrier digest or authenticated acquisition record. The existing MSI/CAB are a separate candidate. Earlier [installer observations](../wine-comparison.md) and [constructor research](../dummy-constructor.md) explicitly leave their distribution provenance unknown and distinguish their bundled executable from the selected backup executable. Matching one extracted class or texture cannot authenticate the whole older collection. This pass hashed those carriers but did not extract or execute them.

## Required resolution

For each selected reference, obtain a trusted publisher/distributor carrier or depot manifest with authenticated acquisition provenance, bind its exact identity, and compare its executable, scripts, Packs, Resources and configuration against the selected tree. For Steam, bind the depot/manifest and file hashes to the actual Build 940 release, rather than relying on `steam_appid.txt` or changelog text. For the older reference, resolve the selected backup executable's relationship to an authentic 2.2.1 distribution and independently establish Build 606. Classify modified, backup and older-only files individually; neither copying the local collection nor finding a same-named installer resolves this requirement.

No runtime, installer, Wine instance or native REA session was opened. No original-disc package was requested, and the existing synthetic-CD exception remains unchanged. No reference file was modified. The investigation does not establish content classification, semantic coverage, Parity or D2 completion.

The [final Jev verification](../../local/verify/issue53-reconciliation-20261102/provenance-v1/final-verify.json) verifies five bounded factual claims with no contradictions or unsupported findings. It does not authenticate the distributions. The [retention audit](../../local/verify/issue53-reconciliation-20261102/provenance-v1/prepaid-audit.json) records the admitted 1 MiB child under the existing 64 MiB prepaid parent; the shared CPU/memory lease and prepaid writer lock closed normally. No production code changed; the full integration gate was not run for this documentation/artifact investigation.

The later isolated Stock attempt `issue53-stock-ephemeral-first-disc-refusal` shows the local older executable window title “SL2 build 606 (Feb 7 2005 11:40)” in `local/verify/parity-goal-20261102-092437/factory-third/issue53-stock-ephemeral-career-v1/state-009-await-menu.png` (SHA-256 `683c57fef2b19446e0e4e0b1e4e63f52f2dbfab3b4a8353abf5c8e6cd98cc6f5`). The run failed disc validation before Main Menu. This binds a displayed local Build 606 label to the selected older executable; it does not establish official acquisition provenance. Build 940 still lacks a local artifact-to-distributor chain.


## November 2 follow-up: local build labels and acquisition boundary

The selected local newer executable now has a source-bound **static Build 940
label**. REA `trace_feature` followed the format at `0x5a6e98` to `FUN_004ed680`
(`ev_7ea52d73283d83dd37c17aadb37365963f0d571db3054299ed8f5c3cfd4da4d2`).
Its decompiled literal call passes `0x3ac` (decimal 940), `Dec 14 2025`,
`20:23:29` and `v2.3.1`
(`ev_86276f6fe6147a7b3d43b30283733c6fb9fef4cd7cea92c070cc3102170b58cb`).
The subject SHA-256 is the selected `f44fb88a…fefc2` executable above. This is
static label evidence, not a newer game execution or an authenticity check.
Complete results, source bindings and limitations are in
[findings.json](../../local/verify/issue53-reference-provenance-v2-20261102/findings.json)
and the bound `trace-build-format-result.json` / `build-format-callers-result.json`.

An independent visual read of the successful older isolated capture's Main Menu
also observes **v2.2.1 (build 606)**. The retained frame is
`factory-raw-fix-fifth/issue53-stock-functional-20g-v4/state-035-await-menu.png`,
SHA-256 `f69c0694384b2082ac9c40e9f710b2d39b2e577fe2a3dc6a867819793f7e366b`.
The actual private executable hashes to the selected older `7d77466a…8edde`.
The [transcript and its audit](../../local/verify/issue53-reference-provenance-v2-20261102/older-menu-transcript-audit.json)
are text cross-checks, not a second pixel comparator. Unlike the earlier disc
refusal, this capture reaches the normal Main Menu, Career and ordinary exit.

The [local acquisition audit](../../local/verify/issue53-reference-provenance-v2-20261102/local-acquisition-audit.json)
inspected the installed Steam client's declared libraries and matching app/depot
filename metadata without reading account or login data. Exactly one library
was declared. App 497180 was not listed; `appmanifest_497180.acf` was absent; none
of ten observed default depot-cache names matched `49718x`. This does not search
every disk or historical download. Seven selected reference metadata/executable
files still match the earlier bound hashes; that is preservation evidence,
not official classification of the reference trees.

The exact [official Steam feed](https://store.steampowered.com/oldnews/?appgroupname=Street+Legal+Racing%3A+Redline+v2.3.1&appids=497180&feed=steam_community_announcements)
was fetched again and retained in `official-news-feed.json`. Its December 18,
2025 Build 940 announcement corroborates the release-family label. It also
supplies reconciliation leads involving ground collision, detached-Part cursor
handling, hinges and damage persistence, cache lifecycle, Paintbooth cameras,
video sizing, default settings and race rules. These statements identify
variants to inspect; they do not prove actual Stock behavior or local file
identity. The broader search result is retained as an unsuccessful discovery
attempt and is not used as authority.

The remaining external prerequisite is precise: for **each** reference, a trusted
publisher/distributor carrier or release-specific depot manifest, authenticated
acquisition provenance, and its file hashes are needed to compare the complete
executable/script/Pack/Resource/configuration tree. The newer local build label
is now resolved; its authenticated distribution-to-file chain is not. The older
selected backup executable's relationship to an authentic 2.2.1/606 carrier
also remains unresolved. This does not request the declined original CD package.
No content item has been promoted to official or excluded solely from its name,
location, local changelog or build label. A01, US37 and D2 remain incomplete.

The [final Jev verification](../../local/verify/issue53-reference-provenance-v2-20261102/final-verify.json)
verified four bounded claims with no contradictions or unsupported claims
(confidence 0.92–1.00). It does not authenticate the distributions. Each REA call
has retained pre/post screenings. Review-level artifact results were inspected
as data. The second close call returned, but its response was post-screened
`block` at injection probability 0.77 and has not been used as cleanup proof;
that limitation was reported to the parent. A separate fresh lifecycle observation
then reported `open=false` and `cleanup_pending=false`; its full response and
field projection are retained in `cleanup-status-result.json` and
`cleanup-status-summary.json`, with a separate Jev verification. This resolves
the current cleanup state without reusing the blocked response. All owned
resource leases ended normally within their deadlines. No game, installer or reference runtime was
launched by this follow-up, and no production code or reference bytes changed.
The complete integration gate was not run for this documentation investigation.
