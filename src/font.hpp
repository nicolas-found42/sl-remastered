#pragma once
#include "scx.hpp"
#include "render2d.hpp"
#include <string>
#include <string_view>

// Stock 004f5720 width calculation after 00502070 imports SCX positions at 0.1 scale.
bool fontTextWidth(const ScxModel& font, std::string_view text, float& width, std::string& error);

// Builds the stock type-1 glyph quads in screen pixels. Alignment is 0 right,
// 1 center, 2 left. Alternate font formats are unsupported.
bool fontGlyphGeometry(const ScxModel& font, std::string_view text, float x, float y,
                       int alignment, float advanceScale, std::vector<RenderVertex>& vertices,
                       std::vector<uint32_t>& indices, std::string& error, uint32_t initialColor = 0xffffffffu);
