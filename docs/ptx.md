# PTX texture loading

The stock executable's resource factory `FUN_0053cc70` selects the PTX loader for the `ptx` extension. Its factory `FUN_00505960` creates the object whose vtable at `005f2d48` reaches `FUN_00507110`. Ghidra identified the stock executable as MD5 `e686099e7233bb7d7c461b1b35c5064c`.

The loader reads the level count at byte 4, width at byte 8, and height at byte 12. Levels begin at byte 16. Each level has a 20-byte header followed by its payload. The first three words give payload length, color JPEG length, and alpha JPEG length. The next level starts after the header and payload. `FUN_00506ad0` decodes the color JPEG. When the alpha length is nonzero, `FUN_00506c70` decodes the second JPEG as grayscale and uses it for alpha.

`src/ptx.cpp` parses the level spans and decodes both JPEG streams into RGBA pixels. `tests/ptx_test.cpp` checks dimensions and sampled color and alpha for three installed menu textures. The expected samples were checked with Pillow as a second JPEG decoder; it may use the same JPEG library family. It also checks corrupt and truncated inputs. `tools/check.sh` runs the test.

The same check runs `ptx_corpus` and `tools/ptx_census.py` over the installed files. Their dimensions and full-pixel checksums agree for 339 levels in 127 files. A checksum can collide, and Pillow and the C++ decoder can share JPEG implementation details. This comparison does not establish stock runtime parity. The Python reader requires Pillow.

For a single-stream level, `FUN_00506c70` leaves the fourth byte at zero but passes a no-alpha flag to `FUN_004f6830`. `FUN_004f68d0` selects source format 22 for that case and 21 when alpha is present. [Microsoft's D3D format reference](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dformat) identifies these as RGB without alpha and ARGB with alpha. The port emits opaque alpha for the single-stream case. The running stock game has not yet been used to compare the result.

The port validates bounds, level counts, dimensions, and exact payload sizes more strictly than the stock code shown. Level-of-detail selection and JPEG rounding against the original are not verified. Loading these files does not yet draw the menu.
