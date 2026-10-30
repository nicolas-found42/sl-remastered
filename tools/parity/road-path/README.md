# Road path arithmetic comparison

Run `tools/parity/road-path/run.sh` from the repository root with the existing macOS Wine build and the 32-bit MinGW compiler. It copies the local city road file into an ignored verification directory, checks the stock executable fingerprint and selected code bytes, and runs the stock readers and path initializer with x87 control word `0x007f`. The comparison script checks every path's resulting other-record values, path value, and running maximum against its single-precision arithmetic model.

The runner also compares the native C++ implementation's final path values, affected other-record values and maximum directly against stock. This covers the installed city file and its initialization order. It does not exercise extreme floating-point inputs or prove traffic and driving behavior. No game data is placed in tracked files.
