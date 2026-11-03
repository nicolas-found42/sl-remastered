# Incremental build dependency-log diagnosis

Observed 2026-11-01 using Ninja 1.13.2, CMake 4.4.4 and the existing `build/` tree. This note records diagnosis and an isolated recovery test; recovery of the shared build tree is left to the build owner.

## Reproduction and ranked hypotheses

After a successful `startup_probe` build, `ninja -C build -d explain -n startup_probe` still reports:

```text
ninja: warning: premature end of file; recovering
ninja explain: stored deps info out of date for 'CMakeFiles/sl_data.dir/src/part_config.cpp.o' (1793501622140648625 vs 1793568612689430452)
...
[73/73] Linking CXX executable startup_probe
```

There are 68 compile actions and five link actions. Three compile outputs have missing dependency records; the others have dependency-record mtimes older than their current object files. This directly explains why a successful prior build does not yield a no-op.

Ranked hypotheses, shared before binary inspection:

1. A malformed complete `.ninja_deps` record stops loading before later successful build updates. Prediction: semantic validation fails before or at the end, and subsequent appends cannot remove that failure.
2. Historical concurrent writers introduced the malformed record. Prediction: a duplicate path/checksum sequence appears. Exact historical writer identities require separate process evidence.
3. Sources or headers keep changing. Prediction: Ninja explains newer inputs, rather than stale/missing dependency metadata. The captured output disfavors this as the immediate explanation.

## Exact failure

A read-only Python parser following the version-four dependency-log format found:

```text
bytes 7585760
sha256 97ffaf3de5960d7f49f6140fd8236419764c55446171a6667e4cd8216c7125ee
INVALID_PATH record 4589 start 7585680 end 7585760 size 76
expected 2767 actual 2769 duplicate_id 2767
path CMakeFiles/object_world_step_test.dir/tests/object_world_step_test.cpp.o
remaining 0
valid nodes 2769 deps 1819
```

All records have complete size framing. The final record is a repeated path record whose encoded node ID is 2767, although the next ID is 2769. Thus “premature end of file” describes a semantic validation failure here, rather than an incomplete trailing record. The warning text alone does not distinguish these failures.

Ninja's `DepsLog::Load` increments the recovery offset immediately after reading a complete record and **before** validating its node checksum. On checksum/duplicate failure it truncates at that already advanced offset. The invalid record therefore remains. Valid dependency updates appended by a successful build are beyond the same failure point and are discarded by the next load. See the [Ninja 1.13.2 loader](https://github.com/ninja-build/ninja/blob/v1.13.2/src/deps_log.cc#L191-L291), especially the offset increment, expected-ID/duplicate checks, and recovery truncation.

The source comment identifies concurrent Ninja processes writing the same dependency log as the cause this checksum is designed to detect. That is strong evidence for historical concurrent build-tree writers, but their identity and timing were not captured. No active Ninja/CMake build process appeared in the process snapshot during this diagnosis.

**A Ninja dry run is not read-only for corrupted metadata.** `DepsLog::Load` can truncate the dependency log even when build commands are disabled. The diagnostic dry run above may have removed later appended records; its retained corrupt boundary is the 7,585,760-byte file described above. Preserve logs before further Ninja invocations.

## Isolated causal test

An isolated temporary fixture used a real Ninja `deps = gcc` rule and a version-four log containing two path records for the same `out` node with ID zero. The rule creates `out` and its real depfile.

```text
actual ninja out: exit 0; compile executed; warning; deps bytes 56
first ninja -n out: exit 0; [1/1] compile scheduled; warning; deps bytes 40
second ninja -n out: exit 0; [1/1] compile scheduled; warning; deps bytes 40
rename corrupt deps log to backup
actual ninja out: exit 0; compile executed; no warning; fresh deps bytes 44
ninja -n out: exit 0; ninja: no work to do.; no warning
PASS
```

The fixture assertions required both consecutive corrupt dry runs to contain the warning and scheduled action, then required the recovered dry run to contain `no work to do` and no warning. This reproduces the persistent bad-record behavior with the installed binary and proves that regenerating dependency metadata resolves that isolated failure. It does not claim that the shared build tree has already been repaired.

## Safe recovery for the build owner

1. Stop/admit no other build-tree writers. Runtime/test processes may continue only if they do not write build metadata or change executables being tested. Keep the source snapshot stable.
2. Before invoking Ninja again, copy `build/.ninja_deps` and `build/.ninja_log` to a timestamped directory outside the build tree, recording SHA-256 hashes. Preserve both originals for diagnosis.
3. Conservative recovery: move only `.ninja_deps` aside, preserving object files, `.ninja_log`, CMake cache, and configured graph. Build `startup_probe` once with one writer; dependency-managed compilation will rebuild to regenerate trustworthy metadata. A byte-exact alternative is truncation **before** the verified invalid record at offset 7585680, but do not apply that offset if the file/hash changed.
4. Immediately assert the original symptom is gone. Ninja returning zero is insufficient: it returns zero for a dirty dry run too. Run the following twice with no intervening edits/builds:

```python
import subprocess
p = subprocess.run(["ninja", "-C", "build", "-n", "startup_probe"], text=True, capture_output=True)
output = p.stdout + p.stderr
assert p.returncode == 0, output
assert "ninja: no work to do." in output, output
assert "warning:" not in output, output
```

5. Run the repository full gate under the same single-writer policy. If dirty rebuilds recur, preserve the new logs before any further Ninja invocation and repeat semantic validation. Avoid `ninja -t recompact` on the corrupt source as the first recovery step: it loads the same invalid metadata and can discard useful data.

The minimal no-op assertion was tested in the isolated fixture; the existing live-tree reproduction remains red until its owner performs recovery. No source/build configuration changes are needed to explain this failure. Prevent recurrence by serializing all writers to each build directory; separate directories are required when independent builds must run concurrently.

## Verification limits

Jev screened the fetched primary source with `pass` (injection 0.02, substance 0.98, relevance 0.96). Claim verification returned five verified claims, zero contradicted/unsupported; two had low-confidence review flags for the source-level causal interpretation and historical writer attribution. The actual installed-binary fixture independently demonstrates persistence and fresh-log recovery. Historical writer attribution remains an inference, not an observed process event.

## Subsequent recovery confirmation

The build owner subsequently reported backing up the logs, truncating before the first invalid record, and completing one full rebuild. Two consecutive dry runs were warning-free no-ops. A later isolated `startup_probe.cpp` edit then scheduled exactly two actions (its compile and executable link). This confirms restored incremental behavior in the shared tree; this subsection records the owner's observed result rather than an additional concurrent build.
