# Crossing radius comparison

Run `tools/parity/road-radius/run.sh` from the repository root. It verifies the reference fence before and after, saves results in a fresh `local/verify/road-radius-docker.*` directory, and runs the original code under Docker Wine with x87 control word `0x007f`.

The helper verifies the original executable MD5 and checks the bytes of the original readers, segment-list and copy helpers, patched allocator entry, and `0057b9b0`. It replaces reallocation with the helper's allocator and builds the reference table and object storage itself, before invoking the original record readers and crossing-radius function. It directly compares the radius bits for every crossing in the real city file. It does not run `00580910`, the live ground or traffic engine, or test malformed data. The native implementation reports unsupported links or non-finite values; the stock behavior for those cases is not established here.
