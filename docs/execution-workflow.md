# Port execution workflow

Done means the arm64 application loads the installed stock data, draws and operates the stock main menu, assembles a car, and drives on the test track. Repeatable comparisons with a running stock executable must then meet the comparison checks agreed for phase 6. Frame-by-frame parity is not established without frame-by-frame evidence.

The starting point for this workflow was a passing `tools/check.sh` baseline: 12 C++ tests, native inventory and audit tests, and data corpus comparisons. The interpreter still handles only a subset of tree operations. `System.buildNumber` is the first matched native. The 360 registered natives and 214 static references are a lower-bound planning inventory, not a runtime coverage guarantee. Platform, game, and original-game comparison work remain.

Use high rigor for stock behavior and data integrity: verify the executable identity, read each operation or native against stock code, and test observable behavior. Each matched native needs traceable stock evidence, a linked implementation, and an executed behavioral test. The audit checks those links; people must still review the quality of each test and stock reading. Unknown behavior stays unsupported, not matched.

1. Establish a runnable baseline and identify the next stock behavior using Ghidra and real script trees.
2. Add a behavioral check for one operation or interaction, implement it, and run `tools/check.sh`. Record the evidence and limits in the decision log.
3. Repeat for interpreter calls, values, object lifetime, and native dispatch. Extend the registry audit alongside real implementations.
4. Build platform and rendering support together with script execution until the stock menu draws and responds. Verify the application itself.
5. Add car assembly, physics, and the race loop, checking a test-track drive in the application.
6. Install Wine when needed, as the user chose, then prepare repeatable inputs and observations and execute the comparison. Keep writable game files outside `ref/`.

For each unit, keep a verified change, fix or remove a failing one, and label an inconclusive result as such. Run `tools/check.sh` before reporting work. Never modify `ref/`, commit game files, commit, or push without the user's request. Append decisions to `docs/decisions.tsv`.
