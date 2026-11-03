# Notarization lifecycle recovery

The independent review found two concrete failure paths in `tools/release/distribute.py`: upload intent was written only after `notarytool submit` returned, and stapling modified the original target before validation. An accepted-but-timeout upload could lose its handle; a partial staple could make retries reject the submitted DMG's own changed hash. A fixed wait sidecar also blocked retries with the same output.

Submission now flushes an exclusive artifact/profile/target/time intent before the upload. The same receipt path refuses another submit. A separate diagnostic records stdout, stderr, return code or exception, including partial timeout output. Complete returned IDs survive failed observations. `resume-submission` never uploads: it checks unchanged original bytes, then existing history/info and a completed log's exact job ID, archive filename and SHA-256. Names/timestamps alone are insufficient. Missing, pending, excessive or ambiguous matches refuse; unknown outcomes remain unresolved rather than triggering another upload.

Finish retains each attempt independently and waits on the existing handle. It requires `Accepted`, copies and verifies the unchanged original, then staples and validates the private copy. Failed or partially modified copies are retained and never reused. Retrying the same output uses a fresh copy. The successful report's `staple_target` is the finished artifact to pass downstream; the original input stays unchanged. App inventories retain their original-path binding while copy comparison accounts for the different owned root. Outputs overlapping original application directories refuse.

Apple's [custom workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow) documents the notarytool/log flow. Installed `notarytool history` and `log` help were read without credentials or service contact. Actual signing, service acceptance, ticket format/validation and clean-system installation are still release prerequisites; fixtures are not their qualification.

`local/verify/issue53-reconciliation-20261102/notary-lifecycle-final-v1` retains final source snapshots/hashes, syntax validation, two genuine injected-fault red controls and the restored 27-test green suite. Removing pre-upload intent fails at the service boundary. Directing stapling back to the original fails immutable-input/retry assertions. The stateful service fixture exercises accepted-but-timeout upload, known-ID retention, exact-hash recovery, pending/wrong/ambiguous history refusal, wait timeout, partial staple timeout, validation failure, app-copy identity and unchanged-original refusal. Existing publication controls and a real tiny unsigned DMG test pass too. No real signing, notary submission or credentials were used.

AST extraction against `issue53-release-review-fixes-20261102/source/tools/release/distribute.py` confirms the publication/acceptance/remote-source/freezing function bodies and shared `command()` remain byte-identical. Only notarization helpers/functions, the resume CLI entry, their tests and candidate-workflow documentation changed.

`notary-lifecycle-review-v1` binds the independent Jev packet. Both narrow evidence claims are verified (0.92/0.61), with no contradictions or unsupported claims. The patch assessment still escalates: safe-to-apply0.25, composite0.64125, correctness confidence0. This is forwarded for independent review and is not completion approval. The parent owns the combined full integration gate; this worker did not run it or claim product/release completion.

## Independent interruption review

The follow-up source review found **NR01 (P1, US05/US46)** in source
`c9515de516365e870f2d8377122972f2cf6db5552d89b32e2688dc770e75994b`:
writing JSON directly at the canonical receipt path could expose a partial file
when the process died after retaining Apple's complete response. Recovery then
failed parsing that file before it could use the response or inspect the service.
An actual child-process SIGKILL control reproduced this failure after exactly one
simulated accepted upload. The previous intent/partial-staple fixes remain valid
within their recorded scope; they did not cover this interruption boundary.

Canonical notary receipts now use a flushed private pending file and atomic,
no-clobber installation. Interrupted pending files remain retained, while a
canonical path is either absent or complete. A concurrent canonical receipt is
preserved; its competing pending attempt remains inspectable. This helper is
limited to submission/recovery receipts and leaves publication bodies unchanged.

Recovery first checks a retained complete submission response against its exact
artifact/profile/command before using its known submission ID. It can therefore
resume polling while the job's completed log is still unavailable. A response
from another command is refused as a source of that handle; unresolved outcomes
still require the exact-hash history/log path and never trigger another upload.
The process-death control verifies recovery with a pending log and zero additional
service calls. All service behavior remains simulated; real Apple acceptance,
tickets and clean installation remain deferred qualification requirements.

Follow-up controls and source/AST bindings are retained under
`local/verify/issue53-reference-provenance-v2-20261102/notary-receipt-review/`.
This follow-up implements the discovered correction; the parent independently
reviews the final patch and owns the complete integration gate. It is not a
whole-product review or release approval.

The first follow-up wrapper at `notary-receipt-review` failed while copying binary
child output into a text-mode log. Its files remain retained and no execution
acceptance is claimed for it. The fresh sibling `notary-receipt-review-v2` fixes
that wrapper only, with the same 512 KiB child cap under the unchanged 8 MiB
parent reservation. Its bound result is
`e31584d9c40315eec3669697b0d68780cb8aa1b28fd473e587adc53facaae40a`:
original-source SIGKILL control fails as expected, all 30 final controls pass,
undefined-name checks pass, and source bindings remain unchanged during the run.
AST comparison confirms the original publication, acceptance and shared command
bodies are unchanged. This paragraph corrects the preceding generic artifact
pointer; the retained source snapshot predates this artifact-location annotation.
