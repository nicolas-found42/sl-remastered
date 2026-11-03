# Text width from stock font meshes

The 2005 menu's `MainMenuDialog.show` asks its OSD to create text before it
opens the background video. `Text.getWidth` tree 27 calls native
`Text.getWidthPixels` at node 6. The executable at `00487410` returns zero
for a null string and otherwise follows `004fde50` to the font material and
mesh. Renderer slot +0x214 points to `004f5720`, which sums the X coordinate
of vertex `4 * byte + 1` for each character. Escape 0x1b skips two bytes, or
ten when followed by `C`. The version-3 mesh importer `00502070` scales each
position by 0.1 before storing it; `00500180` copies imported vertices into
the renderer buffer. The identified executable and the per-address decompile
logs support this calculation.

For `frontend.rpk` type-14 `slii11` (local ID 0x138), the linked type-5 SCX
mesh has 1,024 vertices: four per possible byte. Its unscaled X coordinates
for A, B and space are 100, 90 and 50; the imported advances are 10, 9 and 5.
The same bounded resource loader is tested against startup's large font
`0x25`, medium font `0x139`, and console font `0x23`, including their actual
meshes and PNG atlases.
`font_test` measures `AB A` as 34 through the registered native, then runs
the unchanged `Text.getWidth` call slice in the VM and checks the same value.
The installed and separately extracted 2005 copies of both `Text.class` and
`slii11.scx` have identical MD5 values.

The implementation supports the bounded single-mesh, 256-glyph font material
with mesh flag 16 and material flags 4100. It rejects malformed escapes and
unsupported resources rather than reading beyond buffers. The measurement test
does not demonstrate rendered menu text or stock pixel parity.

The identified renderer `004f4de0` builds four vertices per character from
the imported font mesh, using the mesh UVs, a running X advance, alignment
0/1/2 for right/center/left, and the 0.25 offset stored at `005f0c80`.
`fontGlyphGeometry` models the bounded white-glyph path and rejects color
escapes. `font_test` decodes the actual `SLII11.png` atlas and draws two
glyphs to an SDL software surface; it verifies geometry and a nonblack glyph
pixel. This isolated draw does not connect `Text.create` or the scene traversal,
and it is not a stock screenshot comparison.

`Text.setDefaultColor` (`00487020`) and `getDefaultColor` (`00487040`)
write and read the stock global `00612c64`, whose identified image begins at
`0xffffffff`. The host keeps the equivalent value in its game state. The
registered native test checks the initial value and a changed value. The
text-creation path consumes this state.

The kind-3 material renderer now accepts the font mesh and PNG atlas through
the regular resource path. The PNG decoder preserves stored RGBA values; an
independent decoder agrees on the sampled actual atlas pixel. Stock `00543690`
uses the root bone translation multiplied by 0.1 for the screen-space text
position, and `004f4d80` maps that normalized position into the viewport.
A surface test creates a type-3 instance backed by the actual font material,
draws `AB`, then replaces it with empty text and observes the pixel disappear.
It also verifies text alignment and red vertex-color modulation. `Text.create` now makes
the bounded type-3 node and sets its bounds to mode 2, as `0048c4b0` does;
`Text.update` copies the current script string into the instance. The test
invokes both registered natives and observes their effect on the SDL surface.
Perspective text remains unsupported. This is not a stock screenshot comparison.

`Text.changeAlign` (`004871d0`) updates the alignment field, and
`Text.changeColor` (`00487150`) updates the packed vertex color. The text
renderer `004f4de0` writes this value to vertices whose Direct3D flexible
vertex format is `0x144` (transformed position, diffuse color, one texture
coordinate). The host unpacks its diffuse ARGB value for SDL. The font test
checks that changing alignment moves pixels and changing color produces red
pixels. The same stock text loop interprets escape `C` followed by eight hex
digits as a new packed color, and escape `c` restores the initial color.
The geometry builder implements these and skips other two-byte escapes; a
native-update test draws red and green glyphs from one string. Truncated
escapes and embedded zero bytes fail explicitly at the host boundary.
The test also runs the original `Text.changeText` tree through the VM before
checking those pixels. `Text.setPos` (`004870d0`) updates `bone00` using
`0048c0f0`; the bounded root-bone path reproduces that translation, and the
surface test confirms the visible glyph moves.

The camera's `0050eba0` draws the rectangle collection at `+0x4cc` before
the text collection at `+0x3ac`. `00526760` inserts text into that collection;
`00514250` groups entries by material and appends new groups, `00514390`
appends entries to a group, and `005143e0` fills a removed slot with the last
entry in its group. The bounded scene renderer follows that order for its
supported rectangle and font types. An SDL test checks a mixed scene against
explicit rectangle-then-text draws and verifies the removal swap with three
overlapping text objects. Other material passes remain unsupported.

Camera mesh triangles are clipped in homogeneous coordinates. For SDL's
affine texture interpolation, triangles are recursively subdivided until
the conservative normalized UV error bound `(maxW - minW) / minW * UV range`
is at most 1/65536 per axis. A depth and vertex limit reject cases that
cannot meet this bound. A test compares the bound with perspective-correct
interpolation over sampled barycentric positions. This approximation does not
establish stock pixel parity. The 2005 Init diagnostic presented a frame; an
SDL readback initially captured a cropped red-and-white image. The camera draw
was consuming raw SCX positions without the 0.1 import scale used by stock
`00502070`. Applying that scale before the world transform yields a second
readback showing the full splash artwork. A surface test checks the scaled
triangle extent. The continuous loop still stops on unavailable simulation
physics for queued timers.
