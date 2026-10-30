# Road-record comparison

Run from the repository root:

```sh
tools/parity/road-record/run.sh
```

The script checks the reference fence before and after, copies the installed city road file to a fresh `local/verify/road-record-docker.*` directory, builds two 32-bit helpers, and runs them with the existing Docker Wine oracle. It verifies the stock executable MD5 through `run_original_oracle.sh` and checks the executable bytes for the called record readers, the segment reader, the segment-list helper, memcpy and the patched allocation entry point before relocation. Allocation at `0054f580` is replaced with the helper's `realloc`; the global reference table is replaced with an identity table. Both helpers set the x87 control word to `0x007f`.

The helpers invoke the original record functions `0057ba20`, `0057b220` and `00579960` on valid records from the real city file. Their output is directly compared with the native reader: all reference indices and flags, node trailing words, lane header bytes, segment words and flags, path and other-record fields, record counts, and byte offsets. The output files contain every compared word, not just a digest. The summary hash is a convenience, not the proof. The input header and index table are prepared by the harness; `00580910`, world initialization and routing are not executed. The comparisons cover only this file and valid records, not malformed input.

The original `00580910` copies the version word without checking it and does not take a file length or check for trailing bytes. The bounded native reader rejects trailing bytes and out-of-range indices, and rejects zero-segment records because `0057f1a0` unconditionally accesses the first segment. Those are safety limits, not verified stock error behavior. `00580cc0`, map registration, scene creation and gameplay are outside this test.

A prior comparison mistakenly counted 93 rather than 89 fixed bytes for each fourth-group record. Its original discrepancy is retained at `local/verify/road-record-oracle-docker-20261028/comparison-payload.diff`; the rerunnable comparison uses 89.
