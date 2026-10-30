# Inside-crossing comparison

Run `tools/parity/road-inside/run.sh` from the repository root. It checks the reference fence before and after, verifies the stock executable fingerprint and helper code bytes, and records a fresh result under `local/verify/road-inside-docker.*`. The 32-bit helper runs original readers, radius initialization and `00581b70` under Docker Wine with x87 control word `0x007f`; it substitutes allocation and builds the pools and references itself.

The eight cases use the stock city file, including positions at, just inside, and near and outside 70% of a crossing radius, as well as an empty height-filter result. They do not run the top-level graph loader, the live ground event path, or prove all input cases. The native implementation explicitly reports unsupported for non-finite coordinates, unsupported linked records and non-finite computed radius; stock behavior for those cases is not established.
