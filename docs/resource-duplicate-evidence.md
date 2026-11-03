# Resource duplication evidence

The stock registry maps `java.util.resource.ResourceRef.duplicate(Ljava.util.resource.ResourceRef;)V` to `004802c0`. Ghidra identified the stock executable by MD5 `e686099e7233bb7d7c461b1b35c5064c`. The handler resolves receiver and source handles and calls `FUN_0048d860` only when both exist. A null source therefore leaves the destination unchanged.

`FUN_0048d860` examines the source attachment. If it is absent, it clears the destination. If the attachment type is other than 5, 7, 13, or 14, it attaches the same underlying object to the destination. For those four types, it invokes `FUN_00545230` to make a replacement; the exact clone behavior remains unread. In particular, type 7 is needed for textures, so this bounded implementation does not make the menu work.

The host's legacy attachment fixture preserves identity for the sharing case and rejects clone-required types. Error handling and complete resource lifetimes are not matched to stock, and the native remains unread in the audit.

The registered handler also handles resource-world links. For an attached
source whose type does not require cloning, it attaches the destination's
`OwnerLink` to the same node and copies its ID. An empty source detaches a
linked destination; a null source leaves it unchanged. Clone-required types 7 and 13 return an explicit unsupported error without changing the
destination. Type 14 is supported only for the bounded three-directive material
with flags 4096 and one texture. Type 5 supports the bounded SCX sourcefile
payload and creates a separate runtime mesh. The handler rejects mixed legacy attachment and world-link state.
`tests/resource_ref_set_test.cpp` exercises these paths using mounted resources,
including a real type-14 rectangle material. The legacy attachment-only fixture
remains separate and does not establish the actual resource lifecycle.

For that type-14 case, the handler loads or finds the material binding, creates
a distinct type-14 runtime node under the source's parent, copies the bounded
material fields and independent resource links, and attaches the destination to
the clone. `0054a190` copies the flag word, bone count, LOD values, and shadow
field block after establishing mesh and texture links. For the supported payload,
the constructor defaults for those other fields are retained. Tests use the real
rectangle template, check node and material independence, and confirm that an
unsupported texture clone leaves the destination unchanged. They do not prove
stock renderer-cache refresh, deferred-load behavior, or visual parity. See
`local/census/DecompileAt-00545230.log` and
`local/census/DecompileAt-0054a190.log`.
