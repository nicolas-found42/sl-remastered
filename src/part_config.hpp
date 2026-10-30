#pragma once

#include "resource_id.hpp"
#include <array>
#include <string>
#include <string_view>
#include <vector>

struct PartConfigLine {
  std::string key;
  // The argument text is preserved for the individual stock directive parser.
  std::string arguments;
};

// Reads the bounded text format dispatched by stock 004b6520/004b6340.
// This only tokenizes; it does not apply part directives or create a part.
bool parsePartConfig(std::string_view text, std::vector<PartConfigLine>& lines,
                     std::string& error);

struct PartSlotDirective {
  float position[3];
  float orientation[3];
  int id;
};
// Decode the seven operands of the stock slot directive (decimal or 0x ID). Applying the slot's
// orientation and linking it to a live part remains the factory's job.
bool parsePartSlot(const PartConfigLine& line, PartSlotDirective& slot);

// Initial part attachment slot constructed by 0046c3a0. Rotation is the
// upper 3x3 of stock 0054ecd0 in row-major order. It is not attached to a
// live physics body until the part factory supports that lifecycle.
struct PartSlotState {
  std::array<float, 3> position;
  std::array<float, 9> rotation;
  int id;
  int linkedId = -1;
};
bool makePartSlotState(const PartSlotDirective& directive, PartSlotState& slot);

// 0046c6a0 appends this to the most recently declared slot and remaps
// resourceId through the current pack at application time.
struct PartAttachDirective {
  unsigned int resourceId;
  int targetSlotId;
};
bool parsePartAttach(const PartConfigLine& line, PartAttachDirective& attach);

struct PartRenderRecord {
  uint32_t resourceId = 0;
  PartSlotState transform{};
  uint32_t flags = 0;
};
// Bounded render/flags subset of 0046bfa0 and 0046d0c0. A render may
// omit all six transform operands; partially specified transforms fail.
bool applyPartRenderLine(const PartConfigLine& line, uint32_t initializationCount,
                         const ResourceIdMapping& mapping,
                         std::vector<PartRenderRecord>& renders, std::string& error);

struct PartAttachmentRule {
  uint32_t resourceId;
  int targetSlotId;
};
struct PartSlotRecord {
  PartSlotState state;
  std::vector<PartAttachmentRule> attachments;
};
// Applies only stock slot/attach handlers. Other directives are not handled.
// As in 0046c3a0/0046c6a0, these handlers do nothing after first init and
// an attach before the first slot has no effect.
bool applyPartSlotLine(const PartConfigLine& line, uint32_t initializationCount,
                       const ResourceIdMapping& mapping, std::vector<PartSlotRecord>& slots,
                       std::string& error);

struct ScriptNode;
struct LinkOwner;
// The native device belongs to the loaded type-8 resource. This bounded
// initializer accepts only represented slot/attach directives and records no
// body or renderer state. It does not create a runtime part instance.
bool initializePartPrototype(ScriptNode& source, const std::vector<PartConfigLine>& lines,
                             const ResourceIdMapping& mapping, LinkOwner* partResource,
                             LinkOwner* decalsResource, LinkOwner* soundResource,
                             std::string& error);
