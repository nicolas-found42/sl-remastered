#include "resource_material.hpp"

#include <charconv>
#include <sstream>
#include <string_view>

namespace {
bool parseId(std::string_view value, uint32_t& result) {
	if (value.size() < 3 || value.substr(0, 2) != "0x") return false;
	const char* first = value.data() + 2;
	const char* last = value.data() + value.size();
	const auto parsed = std::from_chars(first, last, result, 16);
	return parsed.ec == std::errc{} && parsed.ptr == last && result != 0;
}
bool parseFlags(std::string_view value, uint32_t& result) {
	const auto dot = value.find('.');
	const auto integer = value.substr(0, dot);
	if (integer.empty()) return false;
	if (dot != value.npos) {
		const auto fractional = value.substr(dot + 1);
		if (fractional.empty() || fractional.find_first_not_of('0') != value.npos) return false;
	}
	const auto parsed = std::from_chars(integer.data(), integer.data() + integer.size(), result, 10);
	return parsed.ec == std::errc{} && parsed.ptr == integer.data() + integer.size();
}
}

ResourceMaterialResult resourceMaterialParse(const PackResource& resource) {
	ResourceMaterialResult out;
	if (resource.entry.resType != 14 || !resource.packBytes) {
		out.error = "expected type-14 resource payload"; return out;
	}
	const auto& bytes = *resource.packBytes;
	const size_t offset = resource.entry.payloadOffset, size = resource.entry.payloadSize;
	if (offset > bytes.size() || size > bytes.size() - offset || size > 65536) {
		out.error = "invalid material payload bounds"; return out;
	}
	std::string payload(bytes.begin() + offset, bytes.begin() + offset + size);
	if (!payload.empty() && payload.back() == '\0') payload.pop_back();
	if (payload.find('\0') != std::string::npos) {
		out.error = "embedded NUL in material payload"; return out;
	}
	std::istringstream lines(payload);
	std::string line;
	bool mesh = false, flags = false;
	while (std::getline(lines, line)) {
		std::istringstream fields(line);
		std::string key, value, extra;
		if (!(fields >> key)) continue;
		if (!(fields >> value) || (fields >> extra)) { out.error = "invalid material line"; return out; }
		if (key == "mesh" && !mesh && parseId(value, out.mesh)) mesh = true;
		else if (key == "texture") {
			uint32_t texture = 0;
			if (!parseId(value, texture)) { out.error = "invalid material texture"; return out; }
			out.textures.push_back(texture);
		}
		else if (key == "flags" && !flags && parseFlags(value, out.flags)) flags = true;
		else { out.error = "unsupported or invalid material directive"; return out; }
	}
	if (!mesh || out.textures.empty()) { out.error = "incomplete material payload"; return out; }
	out.ok = true;
	return out;
}
