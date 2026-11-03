# Wheel setter comparison

This experiment compares two partial wheel representations. The original-layout model uses a 0x2b4-byte record and offsets from the stock executable. The native model uses an array of 17 coefficients and named fields for the other values touched by `setFriction`. Neither is a complete wheel or an implementation of the script interpreter. No pointer-width choice is made here.

Ghidra identified the open program as `StreetLegal_Redline.bak.exe`, MD5 `e686099e7233bb7d7c461b1b35c5064c`. Its windowed project does not define a function at 0x00441210; the bytes there contain the negative and 17-or-greater checks and the indexed store. The separate headless analysis in `local/census/all.c` defines and decompiles that handler. The other handlers below were decompiled in the windowed Ghidra project.

| Setter | Handler | Record write |
|---|---|---|
| `setPacejka(i, value)` | 0x00441210 | `0x1e8 + 4*i`, only for 0 through 16 |
| `setSliction` | 0x00441090 | 0x1e8, coefficient 0 |
| `setFriction` | 0x00440f90 | 0x1f0, coefficient 2, plus 0x64, 0x68, 0x6c, 0x70 |
| `setStiffness` | 0x004410d0 | 0x1f8, coefficient 4 |
| `setMaxLoad` | 0x00441190 | 0x214, coefficient 11 |
| `setFrictn_x` | 0x00441050 | 0x218, coefficient 12 |
| `setLoadSmooth` | 0x004411d0 | 0x220, coefficient 14 |

All handlers skip the write when the wheel lookup returns null. `setFriction` copies its argument to 0x64 and 0x68, and writes each multiplied by a factor to 0x6c and 0x70. The factor is `1 - scale*scale*0.6` when the float at 0x15c is less than 1; otherwise it is the float with bits `0x3dccccd0`, approximately 0.1. These constants were read at 0x005f08f0, 0x005f0f00, and 0x005f0efc respectively. A later `setPacejka(2, value)` changes only the shared coefficient, leaving those four other fields as they were.

Run the comparison with:

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -Wshorten-64-to-32 experiments/wheel_compare/compare.cpp -o /tmp/sl-wheel-compare
/tmp/sl-wheel-compare
```

It checks both bounds, every valid coefficient, both orders of writes to the shared fields, and friction at four scale values. This is a comparison of two models derived from the same reading, not a run of the original executable. It does not verify VM argument decoding, null lookup, ownership or lifetime of the wheel, arbitrary float bit patterns, NaNs, or exact x87 versus arm64 rounding. In particular the test uses ordinary finite values and cannot establish frame-by-frame parity. The byte record is a layout experiment, not evidence that the whole original record must be preserved in the port.
