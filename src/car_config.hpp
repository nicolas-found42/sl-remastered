#pragma once

// Typed reader for the stock car configuration file (`native car <path>.cfg`).
//
// Plain words: the car builder reads a text file one line at a time. The first word of a line
// is a directive ("body", "wheel", "slot" ...) and the rest holds numbers. Stock looks the word
// up in the table at 0060e0f8 and calls one handler per directive. This file mirrors those
// handlers as plain data: it records what each handler stores, it does not build physics
// bodies or render anything. Anything stock would do with a live engine object (create a
// collision shape, load a resource) is recorded as a request in `CarConfig::resources` or as a
// field here; the car factory (00442500/00446180) is the place that acts on it.
//
// Every address in the comments is in the stock executable StreetLegal_Redline.bak.exe
// (MD5 e686099e7233bb7d7c461b1b35c5064c). See docs/car-config-evidence.md.

#include "part_config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ---------------------------------------------------------------------------------------------
// Stock text scanner (00553890, the game's own sscanf; 00551240 forwards to it).
// ---------------------------------------------------------------------------------------------

struct StockScanValue {
  char kind = 'f';  // 'f' %f, 'd' %d/%u/%x, 's' %s, 'c' %c
  float f = 0;
  int32_t i = 0;
  std::string s;
};
struct StockScanResult {
  std::vector<StockScanValue> values;  // one entry per conversion that stock stored, in order
  int returned = 0;                    // what 00553890 returns (a byte count when `%#` was used and the format finished)
  size_t consumed = 0;                 // input bytes read
};
// Mirrors 00553890: %f (no exponent, float arithmetic), %d/%u (decimal, or 0x hex), %x (needs 0x),
// %s (width allowed, stops at whitespace or at the next literal), %c, `%#` (return bytes read).
// Failure returns what was converted so far, exactly like stock.
StockScanResult stockScan(std::string_view input, std::string_view format);

// ---------------------------------------------------------------------------------------------
// Line reader (004b6520 + 004b6340).
// ---------------------------------------------------------------------------------------------

struct CarConfigLine {
  int number = 0;       // 1-based line number in the file
  std::string rawKey;   // what stock handlers see as their first argument (text up to the key's end)
  std::string key;      // first word, lower-cased (the table match ignores case)
  std::string args;     // text stock handlers scan: everything after the one character that follows the key
};
// Splits the file into directive lines. Stops at an `eof` line. Fails on a zero byte or on a line
// of 256 bytes or more (stock would copy it into a 256-byte buffer without a terminator).
bool tokenizeCarConfig(std::string_view text, std::vector<CarConfigLine>& lines, std::string& error);

// One row of the directive table at 0060e0f8.
struct CarDirectiveInfo {
  const char* name;
  uint32_t handler;  // stock handler address
};
extern const std::array<CarDirectiveInfo, 107> kCarDirectiveTable;
const CarDirectiveInfo* findCarDirective(std::string_view lowerCaseName);

// ---------------------------------------------------------------------------------------------
// Typed data.
// ---------------------------------------------------------------------------------------------

using CarVec3 = std::array<float, 3>;

// Position plus the three angles as written, plus the 3x3 rotation stock builds with 0054ecd0
// (row-major, same function part_config.cpp uses for part slots).
struct CarTransform {
  CarVec3 position{};
  CarVec3 orientation{};
  std::array<float, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
};

// Shape codes are the ones stock passes to the physics geometry maker 0049ab60.
enum class CarShapeKind : int {
  None = 0,
  Sphere = 1,        // "ball" or "sphere": dims[0] = radius
  RodX = 2,          // "rodx": dims[0], dims[1]
  RodY = 3,          // "rody": dims[0], dims[1]
  RodZ = 4,          // "rodz": dims[0], dims[1]
  Box = 5,           // "box": dims
  MeshFile = 6,      // any other word is a collision mesh path (.scx)
  MeshResource = 9,  // a number is a mesh resource id (0049a990)
};
struct CarShape {
  CarShapeKind kind = CarShapeKind::None;
  float mass = 0;  // first argument of 0049ab60; 0 means a static shape (stock sets geometry flag 4)
  CarVec3 dims{};
  std::string meshPath;       // MeshFile
  uint32_t meshResource = 0;  // MeshResource (serialized id; remap with the current pack when applying)
};

// Flag bits stock keeps in each body record at +0x58.
enum CarBodyFlag : uint32_t {
  kBodyFlagMask = 0x1ff,       // `flags` directive keeps only these nine bits
  kBodyNoCollision = 0x100,    // `nocollision`
  kBodyNoClick = 0x200,        // `noclick`
  kBodyBone = 0x400,           // `bone`
  kBodyWheelRender = 0x1000,   // `render` right after `wheel`: top byte holds the wheel index
  kBodyLinked = 0x4000,        // `linked`
  kBodySteeringRender = 0x8000,  // `render` right after `steering`: top byte holds the body index
};

struct CarBodyRecord {
  enum Kind { Body = 1, Render = 3 };  // +0x3c in the stock record
  Kind kind = Body;
  std::string directive;  // body, steering, render, light, deformable
  int line = 0;
  CarTransform transform;
  CarShape shape;                  // Body only
  uint32_t renderResource = 0;     // Render only: serialized id (type 0xd load, 0xe for `light`)
  uint32_t flags = 0;              // +0x58
  bool geometryNoCollision = false;  // `nocollision` also sets geometry flag 8
  bool geometryBuoy = false;         // `buoy` sets geometry flag 0x800
  uint32_t lodNear = 0, lodFar = 0;  // `lod` nibbles (bits 16-19 and 20-23 of flags)
  std::vector<int32_t> colors;                  // `colors`
  std::vector<std::array<int32_t, 3>> links;    // `linked`
};

struct CarClick {
  CarVec3 position{};     // +0x1000
  CarVec3 orientation{};
  std::string meshPath;   // set when the word is not a number (geometry type 7 made from the file)
  std::optional<uint32_t> resource;  // set when the word is a number (type 9 load + 0049a990)
};
struct CarShadow {
  uint32_t resource = 0;
  CarTransform transform;  // +0x1074..: position stored as written
};
struct CarSlotBolt {
  CarTransform transform;
  uint32_t resource = 0;  // 0 = none
};
struct CarSlotCompatible {
  uint32_t resource = 0;
  int32_t value = 0;
};
struct CarFlap {
  // 15 floats of the `flap` line, in stock order. Vectors at [3..5] and [6..8] are normalized.
  CarVec3 position{};
  CarVec3 axisA{};
  CarVec3 axisB{};
  std::array<float, 6> values{0, 0, 0, 1.2f, 0, 0.5f};  // numbers 10..15 (defaults at +0x2c..+0x38)
};
struct CarSlotMove {
  int32_t mode = 0;
  float angle = 3.14159274f;  // 0x40490fdb
  CarVec3 axis{};
};
struct CarSlot {
  PartSlotState state;                 // position, rotation, id (same as part slots, 0046c3a0)
  CarVec3 orientation{};
  int32_t slotType = 0;                // +0x4c: `slottype` and `slotflags` share this field
  int32_t damageMode = -1;             // +0x50
  float deform = 1.0f;                 // +0xb0 `slotdeform`
  std::vector<CarSlotBolt> bolts;
  std::vector<PartAttachDirective> attachments;  // raw ids; remap with the pack at application time
  std::vector<CarSlotCompatible> compatible;
  std::optional<CarFlap> flap;
  std::array<uint32_t, 2> flapSounds{0, 0};
  bool hasFlapSounds = false;
  std::optional<CarSlotMove> move;     // `slotmove` also ORs 0x400000 into slotType
  int line = 0;
};

// The wheel record stock keeps per `type` line (record size 200 bytes, base +0x1f44, at most 16).
struct CarWheel {
  int line = 0;
  CarShape shape;                          // `type`: mass, shape word, size
  std::array<float, 2> friction{1.0f, 1.0f};  // `friction` (+0x30,+0x34); only a sphere sets 1,1 by itself
  std::array<float, 3> spring{};           // `spring` (+0x38,+0x3c,+0x40)
  // `wheel`: 10 numbers.
  CarVec3 position{};                      // numbers 1-3
  CarVec3 orientation{};                   // numbers 4-6
  float wheelA = 0, wheelB = 0;            // numbers 7, 8 (+0x18,+0x1c)
  float wheelC = 1.0f, wheelD = 1.0f;      // |number 9|, |number 10| after stock's defaults (+0x20,+0x24)
  bool hasWheel = false;
  bool wheelBones = false;                 // `wheelbones` replaced the bone sphere (radius 0.1, mass 1)
  CarVec3 bonePos{0, 0.4f, 0};             // `bonepos` (+0x58..); `wheel` resets it to this default
  float wheelDeform = 1.0f;                // `wheeldeform` (+0x64); `wheel` resets it to 1
  // `arm`: 7 numbers.
  float armValue = -1.0f;                  // number 1 -> +0x68 (`wheel` resets it to -1)
  CarVec3 armAnchor{};                     // numbers 2-4 plus the wheel position -> +0x78..
  CarVec3 armDirection{};                  // numbers 5-7 -> +0x6c..
  // `hub`: 10 numbers.
  float hubValue = -1.0f;                  // number 1 -> +0x88 (`wheel` resets it to -1)
  std::array<float, 6> hubValues{};        // numbers 2-7 -> +0x8c..+0xa0
  CarVec3 hubAnchor{};                     // numbers 8-10 plus the wheel position -> +0xa4..
  // `instant_center`: two points, each plus the wheel position.
  CarVec3 instantCenterA{0, 0, 10000.0f}, instantCenterB{10000.0f, 0, 0};  // wheel defaults 0x461c4000
};

struct CarWing {
  int32_t type = 0;
  CarVec3 position{};
  CarVec3 axis{};  // normalized when it has a length
  float a = 0;     // 7th float of the line, stored as -abs (+0x1c)
  float b = 0;     // 8th float, stored as abs (+0x20)
  float c = 1.0f;  // 9th float, as written (+0x24); floats 10-13 are read and dropped
  int line = 0;
};
struct CarRollbar {
  int32_t a = 0, b = 0;
  float c = 10000.0f, d = 500.0f;  // defaults 0x461c4000, 0x43fa0000
};
struct CarCamera {
  CarVec3 position{};
  CarVec3 orientation{};
  float fovRaw = 100.0f;  // stock stores fovRaw * (car field +0x127c, set by the factory from Config camera_FOV)
};
struct CarDriver {
  std::string directive;  // "driver" or "controller"
  std::array<int32_t, 8> values{};
  int converted = 0;
  // `ext_camera` fills the most recent driver record (stock uses the previous record's slot when none exists).
  bool hasExtCamera = false;
  std::array<float, 11> extCamera{};  // raw numbers; stock multiplies number 11 by car +0x1278 and number 1 by +0x127c (factory-set)
};
struct CarMaterial {
  int32_t system = 0;  // first number (defaults to a system.rpk id the factory resolves)
  uint32_t a = 0, b = 0;
  float value = 0;
};
struct CarEngineSound {  // sfx_engine_up / sfx_engine_down / sfx_exhaust
  uint32_t resource = 0;
  float rpmScale = 0;  // stock stores 1.0 / (number 2); here the raw number 2
  std::array<float, 4> values{};
};
struct CarGauge {  // cockpit_rpm / cockpit_speed
  CarVec3 position{};
  CarVec3 orientation{};
  uint32_t resource = 0;
  std::array<float, 4> values{};
};
struct CarOsdElement {
  std::string directive;
  uint32_t resource = 0;
  std::vector<float> floats;  // `%f` values in the order read
  std::optional<int32_t> color;
  std::optional<std::string> text;  // %8s, or the single %c of osd_damage_slot
  int converted = 0;
};
struct CarResourceRequest {
  std::string directive;
  uint32_t resource = 0;  // serialized id as written
  int loadType = 0;       // second argument of 00546070 (5 mesh, 6 sound, 7 texture, 8 part, 9 click, 0xd/0xe other)
  int line = 0;
  bool runtimeId = false;
  std::string literalPack;
  int fallbackType = 0;
};
struct CarDiagnostic {
  int line = 0;
  std::string key;
  std::string reason;
};

struct CarConfig {
  // Part-style fields shared with part configs (0046xxxx handlers).
  std::vector<CarBodyRecord> bodies;      // body, steering, render, light, deformable (max 32, +0xfa8)
  int steeringBody = -1;                  // +0xff4
  int rootBody = -1;                      // +0xff0: first body, or the last `bone`
  int lastKind = -1;                      // +0xffc top byte: 0 body, 0x10 wheel; -1 none
  int lastIndex = -1;                     // +0xffc low byte
  int renderCount = 0;                    // +0xff8
  std::vector<CarSlot> slots;
  std::vector<uint32_t> stockParts;       // `stockpart` ids (type 8 load)
  std::optional<CarClick> click;          // `click` (0046b950)
  std::optional<CarShadow> shadow;        // `shadow` (0046c1a0)
  std::vector<float> lodDistances;        // `lods` (+0xfb0..); count entries after stock's end-marker rule
  bool lodEndMarker = false;              // stock wrote a 1.0 after the last distance
  int32_t category = 2;                   // +0x114c
  float deform = 1.0f;                    // +0x1148
  float damage = 10.0f;                   // +0x11d8 (first number of `damage`)
  std::vector<CarResourceRequest> resources;  // mesh/texture/logo/sfx/etc loads in file order

  // Car-specific fields (0044xxxx handlers), living in the car record at +0x2e48 or in the car itself.
  std::vector<CarWheel> wheels;           // max 16
  std::vector<CarWing> wings;             // max 17
  std::vector<CarRollbar> rollbars;       // max 8
  std::vector<CarCamera> cameras;         // max 16
  std::vector<CarDriver> drivers;         // max 4
  int defaultDriver = 0;                  // +0x1780
  std::vector<CarMaterial> materials;     // max 16
  std::array<float, 6> enginePower{200, 10, 0.0001f, 0.03f, 1000, 50};  // +0xd0,+0xe0,+0xf0,+0xf4,+0xe4,+0xe8
  float enginePowerCopy = 300;            // +0xd4 (set equal to number 1 by the directive)
  int32_t gearCount = 4;                  // +0x1c
  std::array<float, 7> gearRatios{4.0f, 2.7f, 1.5f, 1.0f, 0, 0, -6.0f};  // +0x24..+0x3c
  std::vector<std::array<float, 2>> torqueCurve{{200,0}, {1500,0.8f}, {4000,1}, {5000,0.8f}, {7000,0}};
  std::vector<std::array<float, 2>> secondaryTorqueCurve{{200,0}, {1500,0.8f}, {4000,1}, {5000,0.8f}, {7000,0}};
  bool torqueSet = false;
  float rearEndRatio = 3.0f;              // +0x40
  std::array<float, 2> brakePower{100, 200};  // +0xd8,+0xdc
  float clutch = 500.0f;                  // +0xc8
  std::array<float, 2> maxSteer{0.7f, 2.0f};  // +0xa0c,+0xa1c
  std::array<float, 2> steeringRadius{0.2f, 0.0f};  // +0xa14,+0xa18 (after stock's x0.01)
  float rpmIdle = 800.0f;                 // raw number (stock stores x0.1047 at +0xec)
  std::array<float, 4> drag{};  // number 1 stored x1.116 at +0x908, others +0x8fc..
  CarVec3 center{};                       // +0x1eac
  float centerMass = -1.0f;               // `mass` +0x1ec4
  std::array<float, 3> skid{10.0f, 5.0f, 0.5f};  // +0x90c,+0x914,+0x918
  std::array<float, 6> seat{-0.4f, -0.3f, 0, 0, 0, 0};  // +0x91c..+0x930 (the first `camera` line also sets a default)
  std::array<std::array<float, 6>, 3> pedals{};  // +0x934, +0x94c, +0x964; `pedals` writes all three
  std::array<float, 6> shifter{0, -0.1f, -0.1f, 0, 0, 0};  // +0x9ac..
  std::optional<std::array<float, 6>> key;        // parsed and discarded by stock (00444a60)
  std::optional<std::array<float, 6>> handbrake;  // parsed and discarded by stock (00444ae0)
  std::optional<CarGauge> cockpitRpm, cockpitSpeed;
  std::array<float, 9> wheelPars{1.4f, 1.49f, 15.2f, 8000.0f, 1.0f, 0.015f, 0.4f, 0.0f, 0.0f};  // +0x2e50,58,60,7c,80,84,88,8c,90
  std::array<float, 6> steerHelp{0, 0, 1.0f, 0, 3.15f, 0.01f};  // +0x12bc..+0x12d0
  std::array<float, 3> turn{};            // +0x12ec..: the stored values (stock stores -number)
  std::array<float, 3> rollValues{0, 0.3f, 0.9f};   // `roll`  -> +0x12b4,+0x12a8,+0x12ac
  std::array<float, 3> pitchValues{0, 0.4f, 0.9f};  // `pitch` -> +0x12b0,+0x12a0,+0x12a4
  std::array<float, 5> cheatControl{-1, -1, -1, -1, 0};
  std::array<float, 2> velFactor{0, 0};   // +0x1298,+0x129c
  std::string name;                       // `name` is a no-op in stock (00444730); kept for the record
  bool notify = false;                    // +0x2e4c |= 2
  std::vector<CarEngineSound> engineUp, engineDown, exhaust;  // max 16 each
  std::optional<std::pair<uint32_t, float>> gearboxForward, gearboxReverse;
  std::optional<uint32_t> wreckSound;
  std::array<std::optional<float>, 4> hornVolume;
  std::optional<std::pair<uint32_t, uint32_t>> gearChangeSounds;
  std::vector<CarOsdElement> osd;

  std::vector<CarDiagnostic> ignored;     // lines stock skips (not in the table, no slot yet, over capacity)
};

// Applies every line in order. A directive that is in the table but whose handler is not mirrored
// here makes the parse fail with its name and address. Directives that are not in the table are
// recorded in `ignored` (stock skips them silently). `error` names the line on failure.
bool parseCarConfig(std::string_view text, CarConfig& config, std::string& error);
