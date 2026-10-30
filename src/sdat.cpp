#include "sdat.hpp"

#include "reader.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

// Layout checked against the stock executable (MD5 e686099e7233bb7d7c461b1b35c5064c). The file
// open routine is FUN_00485440 and the unsaved-resource list reader is FUN_00485190. The stock
// routine reads only the type byte of the header. This loader also checks the magic and version.

namespace {

constexpr uint16_t kVersion = 0x100;
constexpr uint8_t kTypeTrailerCount = 1;
constexpr uint8_t kTypeResourceList = 2;
constexpr uint32_t kKindEmbedded = 7;
constexpr uint32_t kMinTrailer = 8;

using SdatReader = Reader<SdatResult>;

bool readPacks(SdatReader& r, SdatFile& f, uint32_t count) {
	// Each pack record is at least 8 bytes.
	if (count > (r.limit - r.pos) / 8) {
		return r.fail("pack count exceeds trailer");
	}
	f.packs.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		SdatPack p;
		uint32_t length = 0;
		if (!r.u32(p.ordinal, "truncated pack ordinal") || !r.u32(length, "truncated pack length")) {
			return false;
		}
		if (length == 0) {
			return r.fail("pack string has no terminator");
		}
		if (!r.have(length, "truncated pack string")) {
			return false;
		}
		if (r.data[r.pos + length - 1] != 0) {
			return r.fail("pack string is not NUL terminated");
		}
		p.path.assign(reinterpret_cast<const char*>(r.data + r.pos), length - 1);
		r.pos += length;
		f.packs.push_back(std::move(p));
	}
	for (size_t i = 0; i < f.packs.size(); ++i) {
		if (f.packs[i].ordinal == 0) {
			++f.zeroOrdinals;
		}
		if (f.packs[i].ordinal >= 0xffff) {
			++f.largeOrdinals;
		}
		for (size_t j = 0; j < i; ++j) {
			if (f.packs[j].ordinal == f.packs[i].ordinal) {
				++f.duplicateOrdinals;
				break;
			}
		}
	}
	return true;
}

bool readUnsaved(SdatReader& r, SdatFile& f) {
	uint32_t count = 0;
	if (!r.u32(count, "truncated resource count")) {
		return false;
	}
	// Each record is at least 12 bytes.
	if (count > (r.limit - r.pos) / 12) {
		return r.fail("resource count exceeds trailer");
	}
	f.unsaved.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		SdatUnsaved u;
		if (!r.u32(u.ordinal, "truncated resource record") || !r.u32(u.kind, "truncated resource record") ||
			!r.u32(u.size, "truncated resource record")) {
			return false;
		}
		if (u.kind == kKindEmbedded) {
			u.blobOffset = r.pos;
			if (!r.skip(u.size, "truncated embedded resource")) {
				return false;
			}
		} else if (u.size != 0) {
			// The stock reader reads nothing for other kinds, so a payload here would desync it.
			r.pos -= 4;
			return r.fail("resource of a kind with no payload has a size");
		}
		f.unsaved.push_back(u);
	}
	return true;
}

}  // namespace

SdatResult sdatParse(const uint8_t* data, size_t size) {
	SdatResult out;
	SdatReader r{data, 0, size, out};
	SdatFile& f = out.file;

	if (!r.have(12, "file too short")) {
		return out;
	}
	if (std::memcmp(data, "SDAT", 4) != 0) {
		r.fail("bad magic");
		return out;
	}
	if (readLe16(data + 4) != kVersion) {
		r.pos = 4;
		r.fail("unsupported version");
		return out;
	}
	f.type = data[6];
	f.flags = data[7];

	uint64_t start = 0;
	uint32_t packCount = 0;
	if (f.type == kTypeResourceList) {
		const uint32_t length = readLe32(data + size - 4);
		if (length < kMinTrailer || length > size - 4 || size - 4 - length < 8) {
			r.pos = size - 4;
			r.fail("bad trailer length");
			return out;
		}
		start = size - 4 - length;
		r.pos = start;
		r.limit = size - 4;
		if (!readUnsaved(r, f) || !r.u32(packCount, "truncated pack count")) {
			return out;
		}
	} else if (f.type == kTypeTrailerCount) {
		if (size < 16) {
			r.fail("file too short for a type 1 trailer");
			return out;
		}
		const uint32_t length = readLe32(data + size - 8);
		packCount = readLe32(data + size - 4);
		if (length > size - 8 || size - 8 - length < 8) {
			r.pos = size - 8;
			r.fail("bad trailer length");
			return out;
		}
		start = size - 8 - length;
		r.pos = start;
		r.limit = size - 8;
	} else {
		r.pos = 6;
		r.fail("unsupported type");
		return out;
	}
	if (!readPacks(r, f, packCount)) {
		return out;
	}
	if (r.pos != r.limit) {
		r.fail("trailer not consumed exactly");
		return out;
	}
	f.payloadSize = start - 8;
	out.ok = true;
	return out;
}

SdatResult sdatLoadFile(const std::string& path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	if (!in) {
		SdatResult out;
		out.error = "cannot open file";
		return out;
	}
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (in.bad()) {
		SdatResult out;
		out.error = "read error";
		return out;
	}
	return sdatParse(bytes.data(), bytes.size());
}
