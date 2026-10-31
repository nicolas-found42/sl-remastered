#include "game_file.hpp"
#include "sdat.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace {
bool within(const std::filesystem::path& root, const std::filesystem::path& path) {
	auto a = root.begin(), b = path.begin();
	for (; a != root.end() && b != path.end(); ++a, ++b) if (*a != *b) return false;
	return a == root.end();
}
}

bool GameFile::open(const std::filesystem::path& dataRoot, const std::filesystem::path& referenceRoot,
                    const std::string& name, std::string& error) {
	data_.clear(); cursor_ = payloadEnd_ = 0; error.clear();
	if (name.empty() || name.front() == '/' || name.front() == '\\' ||
	    name.find(':') != std::string::npos || name.find('\0') != std::string::npos) {
		error = "invalid save path"; return false;
	}
	std::string normalized = name;
	std::replace(normalized.begin(), normalized.end(), '\\', '/');
	const std::filesystem::path relative(normalized);
	auto part = relative.begin();
	if (part == relative.end() || *part != "save" || ++part == relative.end()) {
		error = "path is not beneath save"; return false;
	}
	for (const auto& component : relative) if (component == "." || component == ".." || component.empty()) {
		error = "invalid save path"; return false;
	}
	std::error_code ec;
	const auto ref = std::filesystem::canonical(referenceRoot, ec);
	if (ec) { error = "invalid reference root"; return false; }
	const auto root = std::filesystem::canonical(dataRoot, ec);
	if (ec || within(ref, root)) { error = "invalid or reference data root"; return false; }
	const auto save = std::filesystem::canonical(root / "save", ec);
	if (ec || !within(root, save) || within(ref, save)) { error = "invalid save directory"; return false; }
	const auto path = std::filesystem::canonical(root / relative, ec);
	if (ec) { error = "save file not found"; return false; }
	if (!within(save, path) || within(ref, path) || !std::filesystem::is_regular_file(path, ec) || ec) {
		error = "save file escapes data root"; return false;
	}
	std::ifstream stream(path, std::ios::binary);
	if (!stream) { error = "cannot open save file"; return false; }
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), {});
	if (stream.bad()) { error = "cannot read save file"; return false; }
	const auto parsed = sdatParse(bytes.data(), bytes.size());
	if (!parsed.ok) { error = "invalid SDAT: " + parsed.error; return false; }
	data_ = std::move(bytes);
	cursor_ = 8;
	payloadEnd_ = 8 + static_cast<size_t>(parsed.file.payloadSize);
	packs_.clear();
	for (const auto& pack : parsed.file.packs) packs_.emplace_back(pack.ordinal, pack.path);
	return true;
}

std::optional<uint32_t> GameFile::readBits() {
	if (cursor_ > payloadEnd_ || payloadEnd_ - cursor_ < 4) return std::nullopt;
	const uint32_t bits = static_cast<uint32_t>(data_[cursor_]) |
	    (static_cast<uint32_t>(data_[cursor_ + 1]) << 8) |
	    (static_cast<uint32_t>(data_[cursor_ + 2]) << 16) |
	    (static_cast<uint32_t>(data_[cursor_ + 3]) << 24);
	cursor_ += 4;
	return bits;
}

std::optional<int32_t> GameFile::readInt() {
	const auto bits = readBits();
	if (!bits) return std::nullopt;
	int32_t value;
	std::memcpy(&value, &*bits, sizeof value);
	return value;
}

std::optional<float> GameFile::readFloat() {
	const auto bits = readBits();
	if (!bits) return std::nullopt;
	float value;
	static_assert(sizeof value == sizeof *bits);
	std::memcpy(&value, &*bits, sizeof value);
	return value;
}

std::optional<std::string> GameFile::readString() {
	const auto length = readInt();
	if (!length || *length < 0) return std::nullopt;
	if (*length == 0) return std::string();
	const size_t n = static_cast<size_t>(*length);
	if (cursor_ > payloadEnd_ || payloadEnd_ - cursor_ < n) return std::nullopt;
	std::string text(reinterpret_cast<const char*>(data_.data() + cursor_), n);
	cursor_ += n;
	// Stored with its terminator; the string ends at the first NUL (FUN_004174a0).
	const auto nul = text.find('\0');
	if (nul != std::string::npos) text.resize(nul);
	return text;
}
