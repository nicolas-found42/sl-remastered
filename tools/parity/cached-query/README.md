# Cached point-to-face comparison

Run `tools/parity/cached-query/run.sh` from the repository root. It requires the
309-mesh fixture at `local/verify/core-pair-oracle/corpus-data/fixtures.bin`,
the i686 MinGW compiler, and the Wine backend configured by
`tools/wine/run_original_oracle.sh`. For fixture setup, run the existing
`tools/wine/core_pair_oracle.sh` and follow its instructions if it reports a
missing fixture. Each run creates a new directory under `local/verify/`;
pass a new directory path as the first argument to choose its name. Existing
output directories are rejected so earlier evidence is preserved.

The stock helper uses the original full walk to seed feature pointers, calls
`004af970`, shifts the first pose by 0.125, and calls the cached query again.
The comparison checks all ten returned float bit patterns for accepted
point-to-face cases. For rejected cases it checks the stock zero sentinel and
unchanged face pointer; stock leaves the output vectors unwritten on that
path. The comparison fails if either accepted or rejected coverage is empty.

The stock executable's MD5 is verified by `run_original_oracle.sh`. The helper
compares selected loaded PE code ranges with file bytes before relocation,
including the point-to-face cached routine and its projection and transform
helpers. The helper coverage for the full-walk cache seeding is not completely
audited. This is a bounded comparison, not complete cached-query or gameplay
parity; test rotations, values, and contact lifecycle coverage remain limited.
