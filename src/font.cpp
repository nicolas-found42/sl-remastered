#include "font.hpp"
#include <cmath>

bool fontTextWidth(const ScxModel& font, std::string_view text, float& width, std::string& error) {
	if (font.meshes.size() != 1 || font.meshes[0].vertexCount != 1024 || font.meshes[0].positions.size() != 3072) {
		error = "font: unsupported glyph mesh"; return false;
	}
	float result = 0;
	for (size_t i = 0; i < text.size();) {
		const auto c = static_cast<unsigned char>(text[i]);
		if (!c) { error = "font: embedded zero unsupported"; return false; }
		if (c == 0x1b) {
			if (i + 1 >= text.size()) { error = "font: truncated escape"; return false; }
			const size_t length = text[i + 1] == 'C' ? 10 : 2;
			if (length > text.size() - i) { error = "font: truncated escape"; return false; }
			if (text.substr(i, length).find('\0') != std::string_view::npos) { error = "font: embedded zero unsupported"; return false; }
			i += length;
			continue;
		}
		const float x = font.meshes[0].positions[(static_cast<size_t>(c) * 4 + 1) * 3] * 0.1f;
		if (!std::isfinite(x) || !std::isfinite(result + x)) { error = "font: invalid glyph advance"; return false; }
		result += x;
		++i;
	}
	width = result; error.clear(); return true;
}


bool fontGlyphGeometry(const ScxModel& font, std::string_view text, float x, float y,
                       int alignment, float advanceScale, std::vector<RenderVertex>& vertices,
                       std::vector<uint32_t>& indices, std::string& error, uint32_t initialColor) {
	float width = 0;
	if (!fontTextWidth(font, text, width, error)) return false;
	if (alignment < 0 || alignment > 2 || !std::isfinite(x) || !std::isfinite(y) ||
	    !std::isfinite(advanceScale) || advanceScale <= 0 || text.size() > 16384) {
		error = "font: unsupported glyph layout"; return false;
	}
	const auto& mesh = font.meshes[0];
	if (mesh.uv1.size() != 2048 || mesh.indices.size() != 1536) { error = "font: missing glyph geometry"; return false; }
	std::vector<RenderVertex> outVertices;
	std::vector<uint32_t> outIndices;
	outVertices.reserve(text.size() * 4); outIndices.reserve(text.size() * 6);
	float pen = x - (alignment == 0 ? width * advanceScale : alignment == 1 ? width * advanceScale * 0.5f : 0.0f);
	uint32_t color = initialColor;
	for (size_t i = 0; i < text.size();) {
		const auto c = static_cast<unsigned char>(text[i++]);
		if (c == 0x1b) {
			const char command = text[i++];
			if (command == 'C') {
				color = 0;
				for (size_t j = 0; j < 8; ++j) {
					const unsigned char digit = static_cast<unsigned char>(text[i++]);
					color *= 16;
					if (digit >= '0' && digit <= '9') color += digit - '0';
					else if (digit >= 'A' && digit <= 'F') color += digit - 'A' + 10;
					else if (digit >= 'a' && digit <= 'f') color += digit - 'a' + 10;
				}
			} else if (command == 'c') color = initialColor;
			continue;
		}
		const size_t base = static_cast<size_t>(c) * 4;
		const uint32_t outputBase = static_cast<uint32_t>(outVertices.size());
		for (size_t j = 0; j < 4; ++j) {
			const size_t source = base + j;
			const float px = pen + mesh.positions[source * 3] * 0.1f - 0.25f;
			const float py = y + mesh.positions[source * 3 + 1] * 0.1f - 0.25f;
			const float u = mesh.uv1[source * 2], v = mesh.uv1[source * 2 + 1];
			if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(u) || !std::isfinite(v)) {
				error = "font: invalid glyph vertex"; return false;
			}
			outVertices.push_back({px, py, u, v, color});
		}
		for (size_t j = 0; j < 6; ++j) {
			const uint32_t vertex = mesh.indices[static_cast<size_t>(c) * 6 + j];
			if (vertex < base || vertex >= base + 4) { error = "font: unsupported glyph indices"; return false; }
			outIndices.push_back(outputBase + vertex - static_cast<uint32_t>(base));
		}
		pen += mesh.positions[(base + 1) * 3] * 0.1f * advanceScale;
	}
	vertices = std::move(outVertices); indices = std::move(outIndices); error.clear(); return true;
}
