#include "tufa.hpp"

#include "reader.hpp"

#include <fstream>
#include <iterator>

// Layout checked against the stock executable (MD5 e686099e7233bb7d7c461b1b35c5064c). The chunk
// dispatcher is FUN_00411b20, the constant pool reader is FUN_00407660 and its per-kind size
// methods, and the tree node width comes from the table at 0x005f0914.

namespace {

constexpr uint32_t kTagTufa = 0x41465554;
constexpr uint32_t kTagCons = 0x534e4f43;
constexpr uint32_t kTagFild = 0x444c4946;
constexpr uint32_t kTagMthd = 0x4448544d;
constexpr uint32_t kTagClss = 0x53534c43;
constexpr uint32_t kTagTree = 0x45455254;

constexpr uint8_t kHighestTableOp = 0x2e;

// The stock table at 0x005f0914 marks these ops as carrying a 4-byte operand. Ops 0x00 to 0x2e were
// read from the table bytes. Past 0x2e the table runs into unrelated data, so those ops are rejected.
constexpr bool isWideOp(uint8_t op) {
	switch (op) {
	case 0x01: case 0x02: case 0x03: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0a: case 0x0b:
	case 0x0d: case 0x0e: case 0x0f: case 0x11: case 0x12: case 0x14: case 0x15: case 0x16: case 0x17:
	case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1e: case 0x1f: case 0x25: case 0x26: case 0x27:
	case 0x2e:
		return true;
	default:
		return false;
	}
}

using TufaReader = Reader<TufaResult>;

bool readConstants(TufaReader& r, TufaClass& cls) {
	uint32_t count = 0;
	if (!r.u32(count, "truncated constant count")) {
		return false;
	}
	// Each entry is at least 8 bytes, so a count that cannot fit is corrupt.
	if (count > (r.limit - r.pos) / 8) {
		return r.fail("constant count exceeds chunk");
	}
	cls.constants.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		TufaConstant c;
		if (!r.u32(c.kind, "truncated constant kind")) {
			return false;
		}
		switch (c.kind) {
		case 0: {
			uint32_t length = 0;
			if (!r.u32(length, "truncated string length") || !r.str(c.text, length, "truncated string")) {
				return false;
			}
			uint8_t nul = 0;
			if (!r.u8(nul, "truncated string terminator")) {
				return false;
			}
			if (nul != 0) {
				return r.fail("string constant is not NUL terminated");
			}
			break;
		}
		case 1:
		case 2:
		case 4:
			if (!r.u32(c.a, "truncated constant")) {
				return false;
			}
			break;
		case 3:
		case 5:
		case 7:
			if (!r.u32(c.a, "truncated constant") || !r.u32(c.b, "truncated constant")) {
				return false;
			}
			break;
		default:
			r.pos -= 4;
			r.out.unknownConstantKind = true;
			return r.fail("unknown constant kind");
		}
		cls.constants.push_back(std::move(c));
	}
	if (r.pos != r.limit) {
		return r.fail("constant chunk not consumed exactly");
	}
	return true;
}

template <class Record, size_t Words>
bool readRecordLists(TufaReader& r, std::array<std::vector<Record>, 2>& lists, const char* what) {
	for (std::vector<Record>& list : lists) {
		uint32_t count = 0;
		if (!r.u32(count, what)) {
			return false;
		}
		if (count > (r.limit - r.pos) / (Words * 4)) {
			return r.fail("record count exceeds chunk");
		}
		list.resize(count);
		for (Record& rec : list) {
			for (uint32_t& w : rec.words) {
				if (!r.u32(w, what)) {
					return false;
				}
			}
		}
	}
	if (r.pos != r.limit) {
		return r.fail("record chunk not consumed exactly");
	}
	return true;
}

bool readClassHeader(TufaReader& r, TufaClass& cls) {
	for (uint32_t& w : cls.classHeader) {
		if (!r.u32(w, "truncated class header")) {
			return false;
		}
	}
	const uint32_t count = cls.classHeader[4];
	if (count > (r.limit - r.pos) / 4) {
		return r.fail("interface count exceeds chunk");
	}
	cls.interfaces.resize(count);
	for (uint32_t& w : cls.interfaces) {
		if (!r.u32(w, "truncated interface list")) {
			return false;
		}
	}
	if (r.pos != r.limit) {
		return r.fail("class chunk not consumed exactly");
	}
	cls.hasClassHeader = true;
	return true;
}

bool readTrees(TufaReader& r, TufaClass& cls) {
	uint32_t treeCount = 0;
	if (!r.u32(treeCount, "truncated tree count")) {
		return false;
	}
	// Every tree starts with a 4-byte node count.
	if (treeCount > (r.limit - r.pos) / 4) {
		return r.fail("tree count exceeds chunk");
	}
	cls.trees.resize(treeCount);
	for (std::vector<TufaNode>& tree : cls.trees) {
		uint32_t nodeCount = 0;
		if (!r.u32(nodeCount, "truncated node count")) {
			return false;
		}
		if (nodeCount > (r.limit - r.pos) / 3) {
			return r.fail("node count exceeds chunk");
		}
		tree.reserve(nodeCount);
		for (uint32_t i = 0; i < nodeCount; ++i) {
			TufaNode n;
			if (!r.u8(n.op, "truncated node") || !r.u16(n.source, "truncated node")) {
				return false;
			}
			if (n.op > kHighestTableOp) {
				r.pos -= 3;
				return r.fail("tree op past the executable's width table");
			}
			n.wide = isWideOp(n.op);
			if (n.wide && !r.u32(n.operand, "truncated node operand")) {
				return false;
			}
			tree.push_back(n);
		}
	}
	if (r.pos != r.limit) {
		return r.fail("tree chunk not consumed exactly");
	}
	return true;
}

// Constant kind 0 is text. Kind 4 names a kind 0 constant. Anything else resolves to nothing.
std::string constantString(const TufaClass& cls, uint32_t index) {
	if (index >= cls.constants.size()) {
		return {};
	}
	const TufaConstant& c = cls.constants[index];
	if (c.kind == 0) {
		return c.text;
	}
	if (c.kind == 4 && c.a < cls.constants.size() && cls.constants[c.a].kind == 0) {
		return cls.constants[c.a].text;
	}
	return {};
}

char tagLetter(uint32_t tag) {
	switch (tag) {
	case kTagTufa: return 'T';
	case kTagCons: return 'C';
	case kTagFild: return 'F';
	case kTagMthd: return 'M';
	case kTagClss: return 'C';
	default: return 'T';
	}
}

}  // namespace

TufaResult tufaParse(const uint8_t* data, size_t size) {
	TufaResult out;
	TufaReader r{data, 0, size, out};
	TufaClass* cls = nullptr;
	bool sawCons = false;
	while (r.pos < size) {
		uint32_t tag = 0;
		uint32_t chunkSize = 0;
		if (!r.u32(tag, "truncated chunk tag") || !r.u32(chunkSize, "truncated chunk size")) {
			return out;
		}
		if (chunkSize > r.limit - r.pos) {
			r.pos -= 4;
			r.fail("chunk exceeds file");
			return out;
		}
		const uint64_t chunkStart = r.pos;
		const uint64_t chunkEnd = chunkStart + chunkSize;
		const uint64_t fileLimit = r.limit;
		r.limit = chunkEnd;

		if (tag == kTagTufa) {
			if (chunkSize != 4) {
				r.fail("TUFA chunk size is not 4");
				return out;
			}
			out.file.classes.emplace_back();
			cls = &out.file.classes.back();
			sawCons = false;
			r.u32(cls->version, "truncated version");
			if (!out.hasFirstVersion) {
				out.hasFirstVersion = true;
				out.firstVersion = cls->version;
			}
		} else {
			if (cls == nullptr) {
				r.pos = chunkStart - 8;
				r.fail("chunk before the first TUFA chunk");
				return out;
			}
			// The stock loader needs the constant pool before any other chunk.
			if (tag != kTagCons && !sawCons) {
				r.pos = chunkStart - 8;
				r.fail("chunk before the constant pool");
				return out;
			}
			bool ok = false;
			switch (tag) {
			case kTagCons: ok = readConstants(r, *cls); break;
			case kTagFild: ok = readRecordLists<TufaField, 4>(r, cls->fields, "truncated field record"); break;
			case kTagMthd: ok = readRecordLists<TufaMethod, 5>(r, cls->methods, "truncated method record"); break;
			case kTagClss: ok = readClassHeader(r, *cls); break;
			case kTagTree: ok = readTrees(r, *cls); break;
			default:
				r.pos = chunkStart - 8;
				r.fail("unknown chunk tag");
				return out;
			}
			if (!ok) {
				return out;
			}
			sawCons = sawCons || tag == kTagCons;
			if (tag == kTagClss) {
				cls->name = constantString(*cls, cls->classHeader[2]);
			}
		}
		cls->chunkOrder.push_back(tagLetter(tag));
		r.pos = chunkEnd;
		r.limit = fileLimit;
	}
	if (out.file.classes.empty()) {
		r.fail("no TUFA chunk");
		return out;
	}
	out.ok = true;
	return out;
}

TufaResult tufaLoadFile(const std::string& path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	if (!in) {
		TufaResult out;
		out.error = "cannot open file";
		return out;
	}
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (in.bad()) {
		TufaResult out;
		out.error = "read error";
		return out;
	}
	return tufaParse(bytes.data(), bytes.size());
}
