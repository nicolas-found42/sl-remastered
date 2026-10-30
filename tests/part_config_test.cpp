#include "part_config.hpp"
#include "world_model.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <limits>
#include <fstream>
#include <iterator>

int main() {
  std::vector<PartConfigLine> lines;
  std::string error;
  assert(parsePartConfig("# comment\r\nRENDER 0x123 ; comment\r\nslot 1 2 3 # x\nEOF\nignored 1\n", lines, error));
  assert(lines.size() == 2 && lines[0].key == "render" && lines[0].arguments == "0x123 ");
  assert(lines[1].key == "slot" && lines[1].arguments == "1 2 3 ");
  PartSlotDirective slot{};
  assert(parsePartSlot({"slot", "1 2 3 0.5 0 0 7"}, slot));
  assert(slot.id == 7 && slot.position[1] == 2 && slot.orientation[0] == 0.5f);
  PartSlotState state{};
  assert(makePartSlotState(slot, state));
  assert(state.id == 7 && state.linkedId == -1 && state.position[1] == 2);
  assert(std::abs(state.rotation[0] - std::cos(0.5f)) < 0.000001f);
  assert(std::abs(state.rotation[2] - std::sin(0.5f)) < 0.000001f);
  assert(std::abs(state.rotation[6] + std::sin(0.5f)) < 0.000001f);
  auto invalid = slot; invalid.orientation[0] = std::numeric_limits<float>::infinity();
  assert(!makePartSlotState(invalid, state));
  assert(state.id == 7 && state.position[1] == 2);
  assert(parsePartSlot({"slot", "0 0 0 0 0 0 0x10"}, slot) && slot.id == 16);
  assert(parsePartSlot({"slot", "0 0 0 0 0 0 010"}, slot) && slot.id == 10);
  assert(!parsePartSlot({"slot", "1 2"}, slot));
  assert(!parsePartSlot({"slot", "1 2 3 0 0 0 7 extra"}, slot));
  PartAttachDirective attachment{};
  assert(parsePartAttach({"attach", "0x00000006 7"}, attachment));
  assert(attachment.resourceId == 6 && attachment.targetSlotId == 7);
  assert(!parsePartAttach({"attach", "0x100000000 7"}, attachment));
  assert(!parsePartAttach({"attach", "6 7 trailing"}, attachment));
  std::vector<PartRenderRecord> renders;
  ResourceIdMapping renderMapping{5, 10, {8, 9, 10}};
  assert(applyPartRenderLine({"render", "0x134"}, 0, renderMapping, renders, error));
  assert(renders.size() == 1 && renders[0].resourceId == 0x50134 && renders[0].transform.rotation[0] == 1);
  assert(applyPartRenderLine({"render", "0x30008 0 0 0 0.087 0 0"}, 0, renderMapping, renders, error));
  assert(renders[1].resourceId == 0xa0008);
  assert(applyPartRenderLine({"flags", "4"}, 0, renderMapping, renders, error));
  assert(renders[1].flags == 4);
  assert(applyPartRenderLine({"flags", "010"}, 0, renderMapping, renders, error));
  assert(renders[1].flags == 14);
  assert(!applyPartRenderLine({"render", "1 0 0"}, 0, renderMapping, renders, error));
  assert(renders.size() == 2);
  std::vector<PartSlotRecord> records;
  ResourceIdMapping mapping{5, 10, {8}};
  assert(applyPartSlotLine({"attach", "6 7"}, 0, mapping, records, error) && records.empty());
  assert(applyPartSlotLine({"slot", "1 2 3 0 0 0 7"}, 0, mapping, records, error));
  assert(applyPartSlotLine({"attach", "0x10006 7"}, 0, mapping, records, error));
  assert(records.size() == 1 && records[0].attachments.size() == 1);
  assert(records[0].attachments[0].resourceId == 0x80006 && records[0].attachments[0].targetSlotId == 7);
  assert(applyPartSlotLine({"slot", "invalid"}, 1, mapping, records, error));
  assert(records.size() == 1);
  assert(!applyPartSlotLine({"render", "123"}, 0, mapping, records, error));
  {
    LinkOwner part, decals, sound;
    GameWorld world;
    auto* source = world.addScript(0x50020, "java.game.parts.Part");
    assert(source);
    std::vector<PartConfigLine> prototypeLines{{"slot", "1 2 3 0 0 0 7"}, {"attach", "0x10006 7"}};
    auto wrongMapping = mapping; wrongMapping.currentPack = 4;
    assert(!initializePartPrototype(*source, prototypeLines, wrongMapping, &part, &decals, &sound, error));
    auto invalidLines = prototypeLines; invalidLines.push_back({"body", "unrepresented"});
    assert(!initializePartPrototype(*source, invalidLines, mapping, &part, &decals, &sound, error));
    assert(!source->part && !part.head && !decals.head && !sound.head);
    assert(initializePartPrototype(*source, prototypeLines, mapping, &part, &decals, &sound, error));
    assert(source->part && source->part->source.owner == source && source->part->initializationCount == 1);
    assert(source->part->slots.size() == 1 && source->part->slots[0].attachments[0].resourceId == 0x80006);
    assert(!initializePartPrototype(*source, prototypeLines, mapping, &part, &decals, &sound, error));
    source->part.reset();
    assert(!part.head && !decals.head && !sound.head && !source->head);
  }
  assert(!parsePartConfig(std::string("x\0y", 3), lines, error));
  std::ifstream in(".scratch/2005-extract/Program Files/Street Legal Racing - Redline/cars/racers/Duhen_data/scripts/L_headlights_2.cfg", std::ios::binary);
  if (in) {
    std::string actual((std::istreambuf_iterator<char>(in)), {});
    assert(parsePartConfig(actual, lines, error));
    assert(!lines.empty() && lines[0].key == "render" && lines[0].arguments.find("0x00000134") == 0);
    bool foundSlot = false, foundAttach = false;
    for (const auto& line : lines) if (line.key == "slot") {
      assert(parsePartSlot(line, slot));
      assert(slot.id == 7);
      assert(makePartSlotState(slot, state));
      assert(state.id == 7 && state.linkedId == -1);
      foundSlot = true;
    }
    for (const auto& line : lines) if (line.key == "attach") {
      assert(parsePartAttach(line, attachment));
      assert(attachment.resourceId == 6 && attachment.targetSlotId == 7);
      foundAttach = true;
    }
    assert(foundSlot && foundAttach);
    renders.clear();
    for (const auto& line : lines)
      if (line.key == "render" || line.key == "flags")
        assert(applyPartRenderLine(line, 0, renderMapping, renders, error));
    assert(renders.size() == 5 && renders[0].resourceId == 0x50134);
    assert(renders[1].flags == 4 && renders[3].flags == 16);
    records.clear();
    for (const auto& line : lines)
      if (line.key == "slot" || line.key == "attach")
        assert(applyPartSlotLine(line, 0, mapping, records, error));
    assert(records.size() == 1 && records[0].state.id == 7);
    assert(records[0].attachments.size() == 1 && records[0].attachments[0].resourceId == 0x50006);
    assert(records[0].attachments[0].targetSlotId == 7);
  }
}
