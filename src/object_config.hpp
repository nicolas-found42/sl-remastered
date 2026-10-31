#pragma once

#include "resource_id.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Object class config ("objects.rpk" style, stock table 00610ff0 dispatched by
// 004b6340/004b6520). The parser below mirrors the stock directive callbacks
// 00464500..004657a0: values, defaults, ordering, slot gating, and the 16-slot
// cap are preserved. Runtime-only effects (physics body creation in 0049ab60,
// resource link-owner attachment in 00546070, flag mutation on live bodies)
// are deliberately not performed here; the remapped resource ids and the stock
// link types to request from the host are recorded instead.

namespace object_config {

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

// Stock shape ids passed to FUN_0049ab60 (body 00464500, trigger 00464900).
enum class ShapeKind : int {
  Sphere = 1,
  CylinderX = 2,
  CylinderY = 3,
  CylinderZ = 4,
  Box = 5,
  Mesh = 6,
  // Numeric shape type token: stock loads the shape through the mesh/resource
  // registry path (0049a990) instead of building a primitive. meshId holds the
  // remapped resource id.
  ReuseRegistry = 7,
};

// One body (00464500) or trigger (00464900) shape slot. Orientation is the raw
// three euler floats; the stock 0054ecd0 rotation conversion is owned by the
// consumer. Mass 0 means static (stock sets the immovable flag when mass == 0).
// Params carry the trailing shape operands with stock defaults: sphere uses
// params[0] only, box/cylinders use params[0..1], an unnamed mesh string uses
// {1, 1, 1}. Trigger shapes fix mass to 0.001 and never use operand 7.
struct ShapeDirective {
  // Proto registry key; consumed by the physics factory. Not parsed from the
  // config text: stock derives shape identity from the shape resource. The
  // parser leaves it empty except for the unnamed-string mesh path, where it
  // carries the raw type token.
  std::string name;
  Vec3 position;
  Vec3 orientation;
  ShapeKind kind = ShapeKind::Mesh;
  // True when the shape type token was numeric (kind == ReuseRegistry).
  bool reuseExisting = false;
  uint32_t meshId = 0; // remapped through mapping (0 unless ReuseRegistry).
  float mass = 0.0f;
  std::array<float, 3> params{0.0f, 0.0f, 0.0f};
  // Slot flag mutations targeting this shape's body: bit 0x8 from the
  // nocollision directive (00464ff0) and bit 0x800 from buoy (00465030);
  // bit 0x1ff ORed from the flags directive (00465520). The declarative
  // copies remain available in ObjectSlot::modifiers.
  uint32_t flags = 0;
};

enum class SlotKind : int {
  Body = 1,    // 00464500
  Trigger = 2, // 00464900
  Render = 3,  // 00464d20, keyword "render" or "light"
  Part = 5,    // 00464eb0
};

// Slot-scoped modifiers applied by the directives that target the most recent
// slot. Stock applies them only before first init and only while a slot
// exists; the apply function enforces the same gating.
struct SlotModifiers {
  bool nocollision = false; // 00464ff0: clears collision on the slot body.
  bool buoy = false;        // 00465030: sets the buoy flag on the slot body.
  bool hasFriction = false;
  float friction = 0.0f; // 00465070: slot body friction.
  bool hasFlags = false;
  uint32_t flags = 0; // 00465520: ORed into the slot flags masked to 0x1ff.
  bool hasLod = false;
  int lodNear = 0; // 00465590: (near & 0xf) << 16, (far & 0xf) << 20.
  int lodFar = 15; // Render slots default to the full range (00464d20).
  bool hasColors = false;
  // Raw ARGB words, not resource IDs. Failed scans leave malloc'd words
  // uninitialized in stock; nullopt preserves that unknown value.
  std::vector<std::optional<uint32_t>> colors;
  // "color" (singular): fixed four raw ARGB words (observed in the trlight
  // class config). Stock callback not yet identified; stored without
  // behavior until the traffic-light state machine is implemented.
  bool hasStateColors = false;
  std::array<std::optional<uint32_t>, 4> stateColors{};
  bool hasRendertypes = false;
  std::vector<uint32_t> rendertypes; // 004657a0, remapped ids (link type 0xe).
};

// One entry of the stock slot array (16 entries, 0x74 stride). Slot order is
// preserved exactly as parsed; slot-targeting modifiers attach to the newest
// slot at the time the directive appeared.
struct ObjectSlot {
  SlotKind kind = SlotKind::Body;
  ShapeDirective shape;  // Valid for Body and Trigger.
  Vec3 position;         // Valid for Render and Part.
  Vec3 orientation;      // Valid for Render and Part (raw euler floats).
  uint32_t renderId = 0; // Render only: remapped; stock links types 0xd/0xe.
  bool lightKeyword = false; // Render only: line used the "light" keyword.
  uint32_t resourceId = 0;   // Part only: remapped; stock link type 8.
  SlotModifiers modifiers;
};

// 00465bd0 parses six comma-separated placement floats and an optional id
// from the instance parameters, separately from the class directives.
struct InstanceHeader {
  Vec3 position;
  Vec3 orientation;
  int32_t id = -1;
};

// Shipped object configs contain "bounds" followed by six floats and
// sometimes a decimal flag. Stock has no dispatch entry for it.
struct BoundsDirective {
  Vec3 min;
  Vec3 max;
  int32_t flag = 0;
};

// 00465150: default first operand is the resolved system.rpk ID 3; every
// operand is remapped before lookup. Directives are retained in source order;
// each replaces the same live record. These links are for collision effects.
struct MaterialDirective {
  uint32_t rpkId = 0;      // Untyped resource link.
  uint32_t materialId = 0; // First type-6 sound link.
  uint32_t detailId = 0;   // Type-6 collision impact sound link.
  float value = 0.0f;      // Minimum impact strength, strict comparison.
};

// 00465230: "%32s %d %d"; the last operand is remapped (stock link type 6).
struct PowerupDirective {
  std::string name;
  int a = 0;
  int b = 0; // Remapped resource id.
};

// 004650d0: "%f %f"; stock stores f1 and, when f2 is exactly 0.0f, f1 again.
struct DamageDirective {
  float capacity = -1.0f;
  float root = -1.0f;
};

// 00465340: resource id followed by floats. Stock stores f1 and converts
// f2/f3 from hours to integer seconds.
struct SfxLoopDirective {
  uint32_t resourceId = 0; // Remapped; stock link type 6.
  float value = 0.0f;
  int param2 = 0;
  int param3 = 86400;
};

// 00465410: resource id followed by floats, with defaults 1.0, 1.0, 0.0, 24.0
// for the trailing floats. Stock stores f1, converts f4/f5 from hours to
// integer seconds, and keeps f2 / (f3 - f2) as the delay pair.
struct SfxRandomDirective {
  uint32_t resourceId = 0; // Remapped; stock link type 6.
  float volume = 0.0f;
  int param2 = 0;
  int param3 = 86400;
  float pitchMin = 1.0f;
  float pitchSpan = 0.0f;
};

// 00465630 retains an explicit or implicit final endpoint. Only thresholds
// before that endpoint participate in LOD selection.
struct LodsDirective {
  std::vector<float> thresholds;
};

// Result of parsing an object class config. Mirrors the stock class fields the
// directives write: slot array (+0x128, cap 16), default slot (+0x8b0),
// damage pair (+0x8bc/+0x8c0), init value (+0x8b8), base texture (+0x124),
// powerup (+0x930), materials (+0x908), sfx_loop (+0x978), sfx_random
// (+0x984, cap 16), lods (+0x86c).
struct ObjectConfig {
  InstanceHeader instance;
  bool hasBounds = false;
  BoundsDirective bounds;
  std::vector<ObjectSlot> slots;
  int defaultSlotIndex = -1;
  bool multipleRenderSlots = false; // Stock +0x8b4: more than one Body/Trigger.
  int initValue = 1;
  DamageDirective damage;
  uint32_t baseTextureId = 0; // Remapped; stock link type 7.
  bool hasPowerup = false;
  PowerupDirective powerup;
  std::vector<MaterialDirective> materials;
  bool hasSfxLoop = false;
  SfxLoopDirective sfxLoop;
  std::vector<SfxRandomDirective> sfxRandom;
  bool hasLods = false;
  LodsDirective lods;
};

// One config line: the case-insensitive keyword and the argument text after
// it, both with stock comment chars (';', '#', '/') already cut.
struct ObjectConfigLine {
  std::string key;
  std::string arguments;
};

// Tokenizes like stock 004b6520/004b6340: one directive per line, comments cut
// at ';', '#', '/', lines capped at the stock 256-byte buffer. An "eof" line
// ends parsing (the terminator line itself is not returned).
bool parseObjectConfig(std::string_view text,
                       std::vector<ObjectConfigLine> &lines,
                       std::string &error);

// Parses the 00465bd0 placement and optional id from instance parameters.
bool parseInstanceHeader(std::string_view text, InstanceHeader &header,
                         std::string &error);

// Applies one directive to config, mirroring the stock callback semantics:
// slot directives append (16 cap) and set defaultSlotIndex on the first slot;
// slot modifiers attach to the newest slot and are skipped once
// initializationCount != 0 (stock +0x120 gate on 00464ff0/00465030/00465070/
// 00465520/00465590/004656d0/004657a0); slot adds and class directives always
// apply. Class directives keep parse order in their vectors. Unknown keywords
// are rejected with an error naming the directive (stock silently ignores
// them). mapping supplies the 00544590 remap; systemRpkId is the remapped id
// stock would resolve for "system.rpk" class 3 and is used when a material
// line omits its first id.
bool applyObjectConfigLine(const ObjectConfigLine &line,
                           uint32_t initializationCount,
                           const ResourceIdMapping &mapping,
                           uint32_t systemRpkId, ObjectConfig &config,
                           std::string &error);

} // namespace object_config
