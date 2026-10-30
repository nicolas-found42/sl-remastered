#include "car_config.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>

namespace fs = std::filesystem;

static bool near(float a, float b, float tolerance = 1e-6f) { return std::fabs(a - b) <= tolerance; }

static std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), {});
}

static bool parses(const std::string& text, CarConfig& config, std::string& error) {
  return parseCarConfig(text, config, error);
}

static void scannerTests() {
  // %f is the game's own routine: digits, optional fraction, no exponent, no leading '+'.
  auto f = stockScan("0.357 rest", "%f");
  assert(f.values.size() == 1 && f.returned == 1 && near(f.values[0].f, 0.357f, 1e-6f));
  assert(near(stockScan("-62.400", "%f").values[0].f, -62.4f, 1e-4f));
  assert(near(stockScan("1e3", "%f").values[0].f, 1.0f));      // the exponent is not understood
  assert(stockScan("-.5", "%f").values.empty());              // a digit must follow the sign
  assert(stockScan("+1", "%f").values.empty());
  assert(near(stockScan("12.5xyz", "%f").values[0].f, 12.5f));
  // %d: decimal keeps leading zeros as decimal, 0x switches to hex (00553ce0), "-0x10" reads only "-0".
  assert(stockScan("0x0000012C", "%d").values[0].i == 0x12c);
  assert(stockScan("010", "%d").values[0].i == 10);
  assert(stockScan("-7", "%d").values[0].i == -7);
  auto negativeHex = stockScan("-0x10", "%d");
  assert(negativeHex.values[0].i == 0 && negativeHex.consumed == 2);
  assert(stockScan("0x", "%d").values.empty());
  assert(stockScan("0x00FF0000", "%d").values[0].i == 0x00ff0000);
  assert(stockScan("0xFFFFFFFF", "%d").values[0].i == -1);  // 32-bit wrap
  assert(stockScan("5", "%x").values.empty());               // %x needs the 0x prefix
  assert(stockScan("0x1f", "%x").values[0].i == 0x1f);
  // %s stops at white space, at the width, and at the next literal in the format.
  auto s = stockScan("sphere 0.1", "%s %f");
  assert(s.values.size() == 2 && s.values[0].s == "sphere" && near(s.values[1].f, 0.1f));
  assert(stockScan("abcdefghij", "%8s").values[0].s == "abcdefgh");
  auto empty = stockScan("", "%s");  // an empty word still counts as a conversion
  assert(empty.returned == 1 && empty.values[0].s.empty());
  // %# returns the byte count instead of the conversion count when the format ends normally.
  auto counted = stockScan("3 10 20 30", "%#%d");
  assert(counted.values[0].i == 3 && counted.returned == 1 && counted.consumed == 1);  // returned == bytes read
  assert(stockScan("1000:50 2000:60", "%#%f:%f").values.size() == 2);
  // Literal matching (ext_camera uses commas) and failure keeps what was read.
  auto commas = stockScan("120 0,1.8,0 0.0,1.0,-2.0 0,0.0,0.0 5", "%f %f,%f,%f %f,%f,%f %f,%f,%f %f");
  assert(commas.values.size() == 11 && near(commas.values[2].f, 1.8f) && near(commas.values[6].f, -2.0f) &&
         near(commas.values[10].f, 5.0f));
  auto partial = stockScan("1 2", "%f %f %f %f");
  assert(partial.values.size() == 2 && partial.returned == 2);
  assert(stockScan("x", "%c").values[0].s == "x");
}

static void tokenizerTests() {
  std::vector<CarConfigLine> lines;
  std::string error;
  assert(tokenizeCarConfig("# note\r\nRENDER\t0x123 ; c\n// water\n  Light 0x9\nbody\n\neof\nignored 1\n", lines, error));
  assert(lines.size() == 3);
  assert(lines[0].key == "render" && lines[0].rawKey == "RENDER" && lines[0].number == 2);
  assert(lines[0].args == "0x123 ");
  assert(lines[1].key == "light" && lines[1].rawKey == "  Light" && lines[1].number == 4);  // handlers see the blanks
  assert(lines[2].key == "body" && lines[2].args.empty() && lines[2].number == 5);
  // A comment may start with ';', '#' or '/' anywhere in the line, and cuts the rest.
  assert(tokenizeCarConfig("slot 1 2 3//x\nattach 0x1 2 # y\n", lines, error));
  assert(lines[0].args == "1 2 3" && lines[1].args == "0x1 2 ");
  // Only one character after the key is skipped, as in stock (matters for %c).
  assert(tokenizeCarConfig("osd_damage_slot  X\n", lines, error) && lines[0].args == " X");
  assert(!tokenizeCarConfig(std::string("a\0b", 3), lines, error));
  assert(!tokenizeCarConfig(std::string(300, 'x'), lines, error));
  // Agreement with the part-config tokenizer on ordinary lines.
  std::vector<PartConfigLine> partLines;
  assert(parsePartConfig("render 0x123 ; c\nslot 1 2 3 # x\nEOF\n", partLines, error));
  assert(tokenizeCarConfig("render 0x123 ; c\nslot 1 2 3 # x\nEOF\n", lines, error));
  assert(partLines.size() == lines.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    assert(partLines[i].key == lines[i].key && partLines[i].arguments == lines[i].args);
  }
}

static void tableTests() {
  assert(kCarDirectiveTable.size() == 107);
  std::set<std::string> names;
  for (const auto& entry : kCarDirectiveTable) {
    assert(names.insert(entry.name).second);
    assert(entry.handler >= 0x00442f90 && entry.handler <= 0x0046d970);
  }
  assert(findCarDirective("body")->handler == 0x0046bb80);
  assert(findCarDirective("BODY") == findCarDirective("body"));  // the table match ignores case (005d7190)
  assert(findCarDirective("render")->handler == findCarDirective("light")->handler);
  assert(findCarDirective("slottype")->handler == findCarDirective("slotflags")->handler);
  assert(findCarDirective("compound") == nullptr && findCarDirective("chassis") == nullptr &&
         findCarDirective("endcompound") == nullptr);
  // Every directive has a handler in parseCarConfig: none may stop with the "not read yet" message.
  for (const auto& entry : kCarDirectiveTable) {
    CarConfig config;
    std::string error;
    parseCarConfig(std::string(entry.name) + "\n", config, error);
    assert(error.find("not read yet") == std::string::npos);
  }
}

static void handlerTests() {
  CarConfig c;
  std::string e;
  // Shapes: sphere, box, rod, mesh file, mesh resource.
  assert(parses("body 1 2 3 0 0 0 5 sphere 0.4\nbody 0 0 0 0 0 0 0 box 1 2 3\nbody 0 0 0 0 0 0 7 rodz 1.5 0.5\n"
                "body 0 0 0 0 0 0 9 cars\\x\\a.scx\nbody 0 0 0 0 0 0 9 0x00000064\n", c, e));
  assert(c.bodies.size() == 5);
  assert(c.bodies[0].shape.kind == CarShapeKind::Sphere && near(c.bodies[0].shape.dims[0], 0.4f, 1e-6f));
  assert(near(c.bodies[0].shape.mass, 5) && c.bodies[0].transform.position[2] == 3);
  assert(c.bodies[1].shape.kind == CarShapeKind::Box && c.bodies[1].shape.mass == 0 && c.bodies[1].shape.dims[2] == 3);
  assert(c.bodies[2].shape.kind == CarShapeKind::RodZ && near(c.bodies[2].shape.dims[1], 0.5f));
  assert(c.bodies[3].shape.kind == CarShapeKind::MeshFile && c.bodies[3].shape.meshPath == "cars\\x\\a.scx");
  assert(c.bodies[4].shape.kind == CarShapeKind::MeshResource && c.bodies[4].shape.meshResource == 0x64);
  assert(c.rootBody == 0);
  // Flags on the last record: noclick/bone/nocollision/buoy/flags/lod.
  assert(parses("body 0 0 0 0 0 0 1 sphere 1\nnoclick\nbone\nnocollision\nbuoy\nflags 0x1ff\nlod 3 5\n", c, e));
  assert(c.bodies[0].flags == (0x200u | 0x400u | 0x100u | 0x1ffu | (5u << 20) | (3u << 16)));
  assert(c.bodies[0].geometryNoCollision && c.bodies[0].geometryBuoy && c.rootBody == 0);
  // flags before any body does nothing (stock tests the body count) and is reported.
  assert(parses("noclick\nbody 0 0 0 0 0 0 1 sphere 1\n", c, e));
  assert(c.bodies[0].flags == 0 && c.ignored.size() == 1 && c.ignored[0].key == "noclick");
  // Render tagging after steering.
  assert(parses("body 0 0 0 0 0 0 1 sphere 1\nsteering 0 0 0 0 0 0 0.01 sphere 0.1\nrender 0x3001C\nrender 0x3001D\n", c, e));
  assert(c.steeringBody == 1 && c.bodies.size() == 4);
  assert(c.bodies[2].flags == ((1u << 24) | kBodySteeringRender) && c.bodies[2].renderResource == 0x3001c);
  assert(c.bodies[3].flags == ((1u << 24) | kBodySteeringRender));  // still "last body" is the steering body
  // Render tagging after a wheel; deformable is never tagged; light is detected by the raw word.
  assert(parses("type 1 sphere 0.3\nwheel 0 0 0 0 0 0\nrender 0x10\ndeformable 0x63\nlight 0x20 1 2 3 0 0 0\n", c, e));
  assert(c.bodies.size() == 3 && c.bodies[0].flags == ((0u << 24) | kBodyWheelRender));
  assert(c.bodies[1].flags == 0 && c.bodies[1].directive == "deformable" && c.renderCount == 3);
  assert(c.bodies[2].directive == "light" && c.bodies[2].transform.position[1] == 2);
  assert(c.resources.size() == 3);
  assert(c.resources[0].loadType == 0xe && c.resources[0].fallbackType == 0xd);
  assert(c.resources[1].loadType == 0xe && c.resources[1].fallbackType == 0xd);
  assert(c.resources[2].loadType == 0xd && c.resources[2].fallbackType == 0);
  // nocollision on a render record would crash stock: refused.
  assert(!parses("render 0x10\nnocollision\n", c, e) && e.find("nocollision") != std::string::npos);
  // Missing sphere radius: stock reads uninitialised stack, so we refuse.
  assert(!parses("body 0 0 0 0 0 0 1 sphere\n", c, e) && e.find("handler 0046bb80") != std::string::npos);
  // Slots, attachments, compatible, flap.
  assert(parses("attach 1 2\nslot 1 2 3 0 0 0 7\nattach 0x10006 7\nslottype 0x65\nslotdmgmode 4\nslotdeform 0.5\n"
                "compatible 0x10008 3\nflap 0 0.2 -0.8 1 0 0 0 0 -1 0 0.87\nflapsfx 5 6\nslotmove 2 1.0 0 3 4\n", c, e));
  assert(c.slots.size() == 1 && c.ignored.size() == 1 && c.ignored[0].key == "attach");
  const CarSlot& slot = c.slots[0];
  assert(slot.state.id == 7 && slot.attachments.size() == 1 && slot.attachments[0].resourceId == 0x10006);
  assert(slot.damageMode == 4 && near(slot.deform, 0.5f) && slot.compatible[0].value == 3);
  assert(slot.flap && near(slot.flap->axisB[2], -1.0f) && near(slot.flap->values[0], 0.0f) && near(slot.flap->values[1], 0.87f) &&
         near(slot.flap->values[3], 1.2f) && near(slot.flap->values[5], 0.5f));
  assert(slot.hasFlapSounds && slot.flapSounds[1] == 6);
  assert(slot.move && slot.move->mode == 2 && near(slot.move->axis[1], 0.6f) && near(slot.move->axis[2], 0.8f));
  assert(slot.slotType == (0x65 | 0x400000));
  assert(!parses("slottype 5\n", c, e) && e.find("null slot") != std::string::npos);
  assert(!parses("slot 1 2 3 0 0 0\n", c, e));  // id missing
  // Wheels.
  assert(parses("type 10 sphere 0.315\nspring 0.29 0.1 0.05\nwheel -0.709 -0.59 -1.48 0 0 0 1 0 1 0\nwheelbones\n"
                "bonepos 1 2 3\narm 1 1 1 1 0 0 1\nhub 1 2 3 4 5 6 7 1 1 1\ninstant_center 1 1 1 2 2 2\n", c, e));
  assert(c.wheels.size() == 1);
  const CarWheel& w = c.wheels[0];
  assert(near(w.shape.dims[0], 0.315f) && w.spring[1] == 0.1f && w.friction[0] == 1.0f && w.wheelBones);
  assert(near(w.armAnchor[0], 1.0f - 0.709f) && near(w.hubAnchor[2], 1.0f - 1.48f) && near(w.instantCenterB[1], 2.0f - 0.59f));
  assert(w.wheelC == 1.0f && w.wheelD == 0.0f && w.bonePos[0] == 1.0f);
  assert(parses("type 1 sphere 1\nspring 0 0 0\n", c, e) && c.wheels[0].spring[0] == 0.1f && c.ignored.size() == 3);
  assert(!parses("type 1 sphere 1\ninstant_center 1 2 3 4 5 6 7\n", c, e));
  assert(!parses("type 1 sphere 1\narm 1 2 3\n", c, e));
  assert(parses("friction 1 2\n", c, e) && c.wheels.empty() && c.ignored.size() == 1);
  // Engine, drivers, wings, cameras.
  assert(parses("enginepower 100 4 0.00006 0.003 700 32\nbrakepower 1000 2000\nmaxsteer 0.65 2.736\n"
                "ratios 5 4.2 2.5 1.3 1.0 0.758 -4.4\nrearendratio 4.41\nclutch 600\nrpm_idle 900\ndrag 0.5 1 2 3\n", c, e));
  assert(c.enginePower[0] == 100 && c.enginePowerCopy == 100 && c.enginePower[4] == 700 && c.enginePower[5] == 32);
  assert(c.brakePower[1] == 2000 && near(c.maxSteer[1], 2.736f) && c.gearCount == 5);
  assert(near(c.gearRatios[4], 0.758f) && near(c.gearRatios[5], -4.4f) && c.gearRatios[6] == -6.0f);
  assert(near(c.rearEndRatio, 4.41f) && c.clutch == 600 && c.rpmIdle == 900 && c.drag[3] == 3);
  assert(parses("enginepower 100\n", c, e) && c.enginePower[1] == 10 && c.enginePower[4] == 1000);  // untouched defaults
  assert(parses("controller 0 1 2 3 4\next_camera 120 0,1.8,0 0.0,1.0,-2.0 0,0.0,0.0 5\n", c, e));
  assert(c.drivers[0].converted == 5 && c.drivers[0].values[4] == 4 && c.drivers[0].values[5] == 0);
  assert(c.drivers[0].hasExtCamera && c.drivers[0].extCamera[0] == 120 && c.drivers[0].extCamera[10] == 5);
  assert(!parses("ext_camera 1\n", c, e));
  assert(parses("driver 1\n", c, e) && c.defaultDriver == -1);
  assert(parses("wing 4 0 -0.43 0.30 0 1 0 122.5 -0.25 0.666\n", c, e));
  assert(c.wings[0].type == 4 && near(c.wings[0].a, -122.5f) && near(c.wings[0].b, 0.25f) && near(c.wings[0].c, 0.666f));
  assert(parses("camera -0.391 0.320 0.256 0 0 0 90\n", c, e));
  assert(near(c.cameras[0].fovRaw, 90) && near(c.seat[1], 0.320f - 0.55f) && near(c.pedals[0][0], -0.391f - 0.1f) &&
         near(c.pedals[2][2], 0.256f - 0.6f));
  assert(parses("camera 0 0 0 0 0 0\n", c, e) && c.cameras[0].fovRaw == 100.0f);
  assert(parses("camera 1 1 1 0 0 0\nseat 1 2 3 0 0.6 0\npedals -0.391 -0.35 -0.7\n", c, e));
  assert(c.seat[1] == 2 && near(c.pedals[0][0], -0.491f) && near(c.pedals[1][0], -0.391f) && near(c.pedals[2][0], -0.291f));
  // Lists with counts.
  assert(parses("body 0 0 0 0 0 0 1 sphere 1\ncolors 3 0x00FF0000 0x00FFFFFF 7\nlinked 1 2 3\n", c, e));
  assert(c.bodies[0].colors.size() == 3 && c.bodies[0].colors[0] == 0xff0000 && c.bodies[0].colors[2] == 7);
  assert(c.bodies[0].links[0][2] == 3 && (c.bodies[0].flags & kBodyLinked));
  assert(!parses("body 0 0 0 0 0 0 1 sphere 1\ncolors 3 1 2\n", c, e));
  assert(parses("lods 3 10 20 0.5\n", c, e) && c.lodDistances.size() == 3 && c.lodEndMarker);
  assert(parses("lods 3 10 20 30\n", c, e) && c.lodDistances.size() == 2 && !c.lodEndMarker);
  assert(!parses("lods 20\n", c, e));
  assert(parses("torque 2 1000:50 2000:60\n", c, e) && c.torqueCurve.size() == 2 && c.torqueCurve[1][1] == 60);
  assert(!parses("torque 6\n", c, e));
  assert(parses("rollbar 0 1 1500 1950\nrollbar 2 3\n", c, e) && c.rollbars.size() == 2 && c.rollbars[1].c == 10000.0f);
  assert(!parses("rollbar 1\n", c, e));
  // Resource and sound directives.
  assert(parses("mesh 0x64\ntexture 0x20025\nlogo 5\nstockpart 0x1\nstockpart 0\nsfx_horn 7 0.8 2\nsfx_gearbox_fwd 9\n"
                "sfx_engine_up 0x300 8000 1 2 3\nmaterial 0x1 0x2 0x3 0.5\ncockpit_rpm 1 2 3 0 0 0 0x20037 2.2 -0.0004 -2.2 2.2\n", c, e));
  assert(c.stockParts.size() == 1 && c.hornVolume[2] && near(*c.hornVolume[2], 0.8f) && c.gearboxForward->second == 1000.0f);
  assert(c.engineUp.size() == 1 && c.engineUp[0].rpmScale == 8000 && c.engineUp[0].values[2] == 3 && c.engineUp[0].values[3] == 1.0f);
  assert(c.materials.size() == 1 && c.materials[0].value == 0.5f && c.cockpitRpm->resource == 0x20037);
  assert(near(c.cockpitRpm->values[1], -0.0004f));
  assert(!parses("sfx_horn 7 1 -1\n", c, e));
  // Directives no real 2005 car file uses; their behavior comes from the decompile only.
  assert(parses("slot 0 0 0 0 0 0 1\nbolt 1 2 3 0 0 0 0x20\nlogo 5\ncenter 1 2 3\nmass 900\ncheat_control 1 2 3 4 5\n"
                "deform 0.5\nkey 1 2 3 4 5 6\nhandbrake 6 5 4 3 2 1\nnodriver\npitch 1 2 3\nroll 4 5 6\n"
                "shadow 0x20 1 2 3 0 0 0\nshifter 1 2 3 4 5 6\nskid 1 2 3\nsw_radius 5 6\nvelfactor 1 2\nnotify\n", c, e));
  assert(c.slots[0].bolts.size() == 1 && c.slots[0].bolts[0].resource == 0x20 && c.resources[0].loadType == 0xe);
  assert(c.resources[1].loadType == 7 && c.center[2] == 3 && c.centerMass == 900 && c.cheatControl[4] == 5 && c.deform == 0.5f);
  assert(c.key && (*c.key)[5] == 6 && c.handbrake && (*c.handbrake)[0] == 6 && c.pitchValues[2] == 3 && c.rollValues[0] == 4);
  assert(c.shadow && c.shadow->resource == 0x20 && c.shadow->transform.position[1] == 2 && c.shifter[5] == 6);
  assert(c.skid[2] == 3 && near(c.steeringRadius[0], 0.05f) && near(c.steeringRadius[1], 0.06f) && c.velFactor[1] == 2 && c.notify);
  assert(parses("steeringradius 5\nturn 1 2 3\n", c, e) && near(c.steeringRadius[1], 0.0f) &&
         c.turn[0] == -1 && c.turn[2] == -3);
  assert(parses("turn 4\n", c, e) && c.turn[0] == -4 && c.turn[1] == 0);
  assert(parses("type 1 sphere 1\nwheeldeform 0.7\nfriction 0.8 0.9\n", c, e) && near(c.wheels[0].wheelDeform, 0.7f) &&
         near(c.wheels[0].friction[1], 0.9f));
  assert(parses("sfx_engine_down 5 2000 1 2 3 4\nsfx_exhaust 6 3000\nsfx_gearbox_rev 7 500\nsfx_gearchange 1 2\nsfx_wreck 9\n", c, e));
  assert(c.engineDown[0].values[3] == 4 && c.exhaust[0].rpmScale == 3000 && c.exhaust[0].values[0] == 1.0f &&
         c.gearboxReverse->second == 500 && c.gearChangeSounds->second == 2 && *c.wreckSound == 9);
  for (const char* name : {"osd_gauge", "osd_rpmpin", "osd_spdpin", "osd_nospin", "osd_damage", "osd_gear", "osd_speed",
                           "osd_rpm", "osd_nos", "osd_turbo", "osd_mileage", "osd_abs", "osd_asr", "osd_hbrake", "osd_emergency"}) {
    assert(parses(std::string(name) + " 0x20002 1 2 3\n", c, e) && c.osd.size() == 1 && c.osd[0].resource == 0x20002 &&
           c.osd[0].floats.size() == 3);
  }
  // OSD directives: stock gives some of them fewer destinations than the format has conversions.
  assert(parses("osd_gauge 0x20001 1 2 3\nosd_gear 5 1 2 3 255 ABC\nosd_abs 6 1 2 3\nosd_damage_slot  Z\n", c, e));
  assert(c.osd.size() == 4 && c.osd[1].text == "ABC" && c.osd[1].color == 255 && c.osd[3].text == std::string(" "));
  assert(!parses("osd_abs 6 1 2 3 255\n", c, e));  // the %8s after the number would write through a garbage pointer
  // Lines that are not in the table are skipped and listed, never silently.
  assert(parses("compound\nchassis\nendcompound auto\nstcokpart 0x1\n", c, e) && c.ignored.size() == 4);
  assert(c.ignored[3].key == "stcokpart");
  // Capacity limits mirror stock: the 33rd body line is dropped, not stored.
  std::string many;
  for (int i = 0; i < 33; ++i) many += "body 0 0 0 0 0 0 1 sphere 1\n";
  assert(parses(many, c, e) && c.bodies.size() == 32 && c.ignored.size() == 1);
  // The part-style slot parser (part_config.cpp) gives the same rotation as the car slot.
  PartSlotDirective directive{};
  assert(parsePartSlot({"slot", "1 2 3 0.5 0 0 7"}, directive));
  PartSlotState expected{};
  assert(makePartSlotState(directive, expected));
  assert(parses("slot 1 2 3 0.5 0 0 7\n", c, e) && c.slots[0].state.rotation == expected.rotation);
}

static void realFileTests() {
  const fs::path root =
      ".scratch/2005-extract/Program Files/Street Legal Racing - Redline/cars";
  if (!fs::exists(root)) {
    std::cout << "car_config_test: real cars directory missing, skipped real-file checks\n";
    return;
  }
  std::vector<fs::path> files;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    std::string name = entry.path().filename().string();
    for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (entry.is_regular_file() && name.rfind("_main", 0) == 0 && entry.path().extension() == ".cfg")
      files.push_back(entry.path());
  }
  assert(files.size() == 39);
  std::map<std::string, size_t> lineTally;
  size_t wheels = 0, slots = 0, wings = 0, cameras = 0, stockParts = 0, attachments = 0, compatible = 0,
         drivers = 0, bodyRecords = 0, missingMeshes = 0, meshBodies = 0;
  size_t parsedFiles = 0;
  for (const auto& path : files) {
    const std::string text = readFile(path);
    std::vector<CarConfigLine> lines;
    std::string error;
    assert(tokenizeCarConfig(text, lines, error));
    for (const auto& line : lines) ++lineTally[line.key];
    CarConfig config;
    if (!parseCarConfig(text, config, error)) {
      std::cout << path << ": " << error << "\n";
      assert(false);
    }
    ++parsedFiles;
    wheels += config.wheels.size();
    slots += config.slots.size();
    wings += config.wings.size();
    cameras += config.cameras.size();
    stockParts += config.stockParts.size();
    drivers += config.drivers.size();
    bodyRecords += config.bodies.size();
    for (const auto& s : config.slots) {
      attachments += s.attachments.size();
      compatible += s.compatible.size();
    }
    // Only these seven names are skipped by stock; every other line of every file reached a handler.
    for (const auto& d : config.ignored) {
      assert(d.key == "compound" || d.key == "chassis" || d.key == "endcompound" || d.key == "intdamage" ||
             d.key == "sfx_loop" || d.key == "stcokpart");
    }
    assert(!config.bodies.empty() && config.bodies[0].kind == CarBodyRecord::Body);
    assert(config.rootBody >= 0 && config.wheels.size() >= 4);
    assert(config.drivers.size() == 1 && config.drivers[0].hasExtCamera);
    // Every collision mesh named by a body exists in the extracted data (paths use backslashes from the game root).
    const fs::path gameRoot = root.parent_path();
    for (const auto& b : config.bodies) {
      if (b.kind != CarBodyRecord::Body || b.shape.kind != CarShapeKind::MeshFile) continue;
      ++meshBodies;
      std::string rel = b.shape.meshPath;
      for (char& ch : rel) if (ch == '\\') ch = '/';
      if (!fs::exists(gameRoot / rel)) {
        std::cout << path.filename() << ": missing mesh " << b.shape.meshPath << "\n";
        ++missingMeshes;
      }
    }
  }
  assert(parsedFiles == 39);
  // Counts from the typed result equal counts of the raw lines (an independent tally, tools/car_cfg_census.py agrees).
  assert(wheels == lineTally["type"] && wheels == lineTally["spring"] && wheels == lineTally["wheel"] && wheels == 158);
  assert(slots == lineTally["slot"] && slots == 1773);
  assert(wings == lineTally["wing"] && wings == 91 && cameras == lineTally["camera"] && cameras == 122);
  assert(stockParts == lineTally["stockpart"] && stockParts == 624);
  assert(attachments == lineTally["attach"] && attachments == 757 && compatible == lineTally["compatible"]);
  assert(drivers == lineTally["controller"] && drivers == 39);
  assert(bodyRecords == lineTally["body"] + lineTally["steering"] + lineTally["render"] + lineTally["deformable"] + lineTally["light"]);
  assert(missingMeshes == 0 && meshBodies > 300);
  std::cout << "car_config_test: " << parsedFiles << " of " << files.size() << " real car cfg files parse; " << wheels
            << " wheels, " << slots << " slots, " << bodyRecords << " body/render records, " << meshBodies
            << " collision meshes all present\n";

  // CoupeSport, values read from the file and from the handlers.
  const fs::path coupe = root / "racers/Baiern_data/scripts/_main_CoupeSport.cfg";
  CarConfig c;
  std::string e;
  assert(parseCarConfig(readFile(coupe), c, e));
  assert(c.bodies.size() == 15 && c.slots.size() == 72 && c.wheels.size() == 4 && c.wings.size() == 3 && c.cameras.size() == 5);
  assert(c.bodies[0].shape.kind == CarShapeKind::Sphere && near(c.bodies[0].shape.mass, 0.001f, 1e-6f));
  assert(c.bodies[0].flags == (kBodyNoClick | kBodyBone));
  assert(c.bodies[1].shape.meshPath == "cars\\racers\\Baiern_data\\meshes\\_phys_chassis_Rf.scx");
  assert(near(c.bodies[1].shape.mass, 62.4f, 1e-4f) && near(c.bodies[1].transform.position[1], 0.357f) &&
         near(c.bodies[1].transform.position[2], 0.381f));
  assert(c.steeringBody >= 0 && c.bodies[static_cast<size_t>(c.steeringBody)].directive == "steering");
  assert(c.wheels[0].position[0] == -0.709f || near(c.wheels[0].position[0], -0.709f));
  assert(near(c.wheels[0].shape.dims[0], 0.315f) && near(c.wheels[0].spring[0], 0.29f) && near(c.wheels[3].position[2], 1.256f));
  assert(c.wheels[2].wheelC == 1.0f && c.wheels[2].wheelD == 1.0f && c.wheels[0].wheelD == 0.0f);
  assert(c.wheels[0].wheelBones && c.wheels[0].shape.mass == 10.0f);
  assert(c.enginePower[0] == 100.0f && near(c.enginePower[2], 0.00006f, 1e-9f) && c.enginePower[4] == 700.0f &&
         c.enginePower[5] == 32.0f);
  assert(c.brakePower[0] == 1000.0f && c.brakePower[1] == 2000.0f && near(c.maxSteer[0], 0.65f) && near(c.maxSteer[1], 2.736f));
  assert(c.gearCount == 5 && near(c.gearRatios[0], 4.2f) && near(c.gearRatios[5], -4.4f) && near(c.rearEndRatio, 4.41f));
  assert(c.drivers[0].converted == 5 && c.drivers[0].extCamera[0] == 120.0f && near(c.drivers[0].extCamera[2], 1.8f));
  assert(c.wings[0].type == 4 && near(c.wings[0].a, -122.5032f, 1e-3f) && near(c.wings[0].b, 0.25f) && near(c.wings[0].c, 0.666f));
  assert(near(c.cameras[0].position[0], -0.391f) && c.cameras[0].fovRaw == 90.0f && c.cameras[1].fovRaw == 120.0f);
  assert(near(c.seat[0], -0.391f) && near(c.seat[1], -0.340f) && near(c.seat[4], 0.6f));
  assert(near(c.pedals[1][2], -0.700f) && near(c.pedals[0][0], -0.491f));
  assert(c.cockpitSpeed && c.cockpitSpeed->resource == 0x20037 && near(c.cockpitSpeed->values[1], -0.009f));
  assert(near(c.cockpitRpm->values[1], -0.0004f) && c.category == 2);
  assert(c.click && c.click->resource == 0x12c);
  bool sawDeformable = false;
  for (const auto& b : c.bodies)
    if (b.directive == "deformable") { sawDeformable = true; assert(b.renderResource == 0x63); }
  assert(sawDeformable);
  assert(c.slots[0].state.id == 501 && near(c.slots[0].state.position[0], -0.530f) && near(c.slots[0].state.position[2], -1.480f));
  assert(c.steerHelp[0] == 1.0f && c.steerHelp[1] == 30.0f && near(c.steerHelp[5], 1.0f));
  // Slot 101 ("FL wheel"): attach, slottype and slotdmgmode all attach to it, not to its neighbours.
  const CarSlot* wheelSlot = nullptr;
  for (const auto& s : c.slots) if (s.state.id == 101) wheelSlot = &s;
  assert(wheelSlot && wheelSlot->attachments.size() == 1 && wheelSlot->attachments[0].resourceId == 0x00040200 &&
         wheelSlot->attachments[0].targetSlotId == 1 && wheelSlot->slotType == 101 && wheelSlot->damageMode == 4);

  // The fake_racers Badge file uses colors, rollbar, flags and damage.
  CarConfig badge;
  assert(parseCarConfig(readFile(root / "fake_racers/Badge/scripts/_main_.cfg"), badge, e));
  assert(badge.rollbars.size() == 2 && badge.rollbars[1].a == 2 && badge.rollbars[1].c == 1500.0f && badge.rollbars[1].d == 1950.0f);
  assert(badge.damage == 10.0f);
  size_t withColors = 0;
  for (const auto& b : badge.bodies) {
    if (b.colors.size() == 16) {
      ++withColors;
      assert(b.colors[0] == 0x00EEB621);
    }
  }
  assert(withColors == 1);
}

int main() {
  scannerTests();
  tokenizerTests();
  tableTests();
  handlerTests();
  realFileTests();
  std::cout << "car_config_test: ok\n";
}
