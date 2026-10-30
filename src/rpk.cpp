#include "rpk.hpp"

#include "reader.hpp"

#include <cstring>
#include <fstream>
#include <iterator>

namespace {

constexpr uint32_t kVersion = 0x200;
constexpr uint64_t kDependencyBytes = 64;
constexpr size_t kDependencyPathBytes = 56;
constexpr uint64_t kRecordFixedBytes = 23;
constexpr uint8_t kFlagTransform = 1;

using RpkReader = Reader<RpkResult>;

bool readHeader(RpkReader& r, RpkPack& pack, uint64_t& depCount) {
	if (!r.have(4, "truncated magic")) {
		return false;
	}
	if (std::memcmp(r.data, "RPAK", 4) != 0) {
		return r.fail("bad magic");
	}
	r.pos = 4;
	if (!r.u32(pack.version, "truncated version")) {
		return false;
	}
	if (pack.version != kVersion) {
		r.pos = 4;
		return r.fail("unsupported version");
	}
	uint32_t depsA = 0;
	uint32_t depsB = 0;
	if (!r.u32(depsA, "truncated dependency count") || !r.u32(depsB, "truncated dependency count")) {
		return false;
	}
	depCount = static_cast<uint64_t>(depsA) + depsB;
	return true;
}

bool readDependencies(RpkReader& r, RpkPack& pack, uint64_t depCount) {
	if (depCount > (r.limit - r.pos) / kDependencyBytes) {
		return r.fail("dependency records exceed file");
	}
	pack.dependencies.reserve(static_cast<size_t>(depCount));
	for (uint64_t i = 0; i < depCount; ++i) {
		RpkDependency dep;
		if (!r.u32(dep.id, "truncated dependency id") ||
			!r.str(dep.path, kDependencyPathBytes, "truncated dependency path") ||
			!r.u32(dep.trailer, "truncated dependency trailer")) {
			return false;
		}
		pack.dependencies.push_back(std::move(dep));
	}
	return true;
}

bool readEntry(RpkReader& r, RpkEntry& e) {
	uint8_t aliasLen = 0;
	if (!r.u32(e.superId, "truncated record") || !r.u32(e.typeId, "truncated record") ||
		!r.u8(e.resType, "truncated record") || !r.u8(e.flags, "truncated record") ||
		!r.f32(e.compat, "truncated record") || !r.u32(e.payloadOffset, "truncated record") ||
		!r.u32(e.payloadSize, "truncated record") || !r.u8(aliasLen, "truncated record") ||
		!r.str(e.alias, aliasLen, "truncated alias")) {
		return false;
	}
	if (e.flags & kFlagTransform) {
		std::array<float, 12> t{};
		for (float& f : t) {
			if (!r.f32(f, "truncated transform")) {
				return false;
			}
		}
		e.transform = t;
	}
	return true;
}

}  // namespace

RpkResult rpkParse(const uint8_t* data, size_t size) {
	RpkResult out;
	RpkReader r{data, 0, size, out};
	RpkPack& pack = out.pack;

	uint64_t depCount = 0;
	if (!readHeader(r, pack, depCount) || !readDependencies(r, pack, depCount)) {
		return out;
	}

	uint32_t tableSize = 0;
	uint32_t recordCount = 0;
	if (!r.u32(tableSize, "truncated table header") || !r.u32(recordCount, "truncated table header") ||
		!r.u32(pack.serviceA, "truncated table header") || !r.u32(pack.serviceB, "truncated table header")) {
		return out;
	}
	if (tableSize > r.limit - r.pos) {
		r.fail("record table exceeds file");
		return out;
	}
	if (static_cast<uint64_t>(recordCount) * kRecordFixedBytes > tableSize) {
		r.fail("record count exceeds table size");
		return out;
	}

	const uint64_t tableEnd = r.pos + tableSize;
	r.limit = tableEnd;
	pack.entries.reserve(recordCount);
	for (uint32_t i = 0; i < recordCount; ++i) {
		RpkEntry e;
		if (!readEntry(r, e)) {
			return out;
		}
		const uint64_t payloadEnd = static_cast<uint64_t>(e.payloadOffset) + e.payloadSize;
		if (payloadEnd > size) {
			++pack.payloadsOutOfRange;
		}
		if (e.payloadSize > 0 && e.payloadOffset < tableEnd) {
			++pack.payloadsOverlappingTable;
		}
		pack.entries.push_back(std::move(e));
	}
	if (r.pos != tableEnd) {
		r.fail("record table not consumed exactly");
		return out;
	}
	out.ok = true;
	return out;
}

RpkResult rpkLoadFile(const std::string& path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	if (!in) {
		RpkResult out;
		out.error = "cannot open file";
		return out;
	}
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (in.bad()) {
		RpkResult out;
		out.error = "read error";
		return out;
	}
	return rpkParse(bytes.data(), bytes.size());
}
