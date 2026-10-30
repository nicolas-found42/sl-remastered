#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "rpk.hpp"
#include "scx.hpp"
#include "sdat.hpp"
#include "tufa.hpp"

#if !defined(__aarch64__) || !defined(__APPLE__)
#error "sl_port builds only for Apple Silicon macOS"
#endif

static_assert(sizeof(void*) == 8, "the original game is 32-bit, this port is 64-bit");

namespace fs = std::filesystem;

static bool hasExtension(const fs::path& p, const char* want) {
	std::string ext = p.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext == want;
}

static bool hasRpkExtension(const fs::path& p) {
	return hasExtension(p, ".rpk");
}

static int rpkCensus(const char* dir, bool hasExpect, unsigned long expect) {
	std::error_code ec;
	std::vector<fs::path> files;
	fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
	if (ec) {
		std::fprintf(stderr, "cannot walk %s: %s\n", dir, ec.message().c_str());
		return 2;
	}
	for (const fs::directory_entry& de : it) {
		if (de.is_regular_file(ec) && hasRpkExtension(de.path())) {
			files.push_back(de.path());
		}
	}
	std::sort(files.begin(), files.end());

	unsigned long opened = 0;
	unsigned long totalDeps = 0;
	unsigned long totalEntries = 0;
	std::vector<std::pair<std::string, std::string>> failures;
	for (const fs::path& p : files) {
		const std::string rel = p.lexically_relative(dir).string();
		const RpkResult r = rpkLoadFile(p.string());
		if (!r.ok) {
			std::printf("%s FAIL %s\n", rel.c_str(), r.error.c_str());
			failures.emplace_back(rel, r.error);
			continue;
		}
		++opened;
		totalDeps += r.pack.dependencies.size();
		totalEntries += r.pack.entries.size();
		std::printf("%s OK deps=%zu entries=%zu payloadsOutOfRange=%u payloadsOverlappingTable=%u\n", rel.c_str(),
			r.pack.dependencies.size(), r.pack.entries.size(), r.pack.payloadsOutOfRange, r.pack.payloadsOverlappingTable);
	}

	std::printf("files found: %zu\n", files.size());
	std::printf("files opened: %lu\n", opened);
	std::printf("files failed: %zu\n", failures.size());
	std::printf("total dependencies: %lu\n", totalDeps);
	std::printf("total entries: %lu\n", totalEntries);
	std::puts("failed files:");
	if (failures.empty()) {
		std::puts("none");
	}
	for (const auto& f : failures) {
		std::printf("%s: %s\n", f.first.c_str(), f.second.c_str());
	}

	if (files.empty() || !failures.empty()) {
		return 1;
	}
	if (hasExpect && files.size() != expect) {
		std::fprintf(stderr, "expected %lu files, found %zu\n", expect, files.size());
		return 1;
	}
	return 0;
}

static int scxCensus(const char* dir, const char* ext, bool perFile, bool hasExpect, unsigned long expect) {
	std::error_code ec;
	std::vector<fs::path> files;
	fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
	if (ec) {
		std::fprintf(stderr, "cannot walk %s: %s\n", dir, ec.message().c_str());
		return 2;
	}
	for (const fs::directory_entry& de : it) {
		if (de.is_regular_file(ec) && hasExtension(de.path(), ext)) {
			files.push_back(de.path());
		}
	}
	std::sort(files.begin(), files.end());

	unsigned long opened = 0;
	unsigned long materials = 0;
	unsigned long meshes = 0;
	unsigned long vertices = 0;
	unsigned long indices = 0;
	unsigned long outOfRange = 0;
	unsigned long incomplete = 0;
	unsigned long nonFinite = 0;
	unsigned long trailing = 0;
	unsigned long mismatches = 0;
	std::map<uint32_t, unsigned long> byVersion;
	std::vector<std::pair<std::string, std::string>> failures;
	for (const fs::path& p : files) {
		const std::string rel = p.lexically_relative(dir).string();
		const ScxResult r = scxLoadFile(p.string());
		if (!r.ok) {
			failures.emplace_back(rel, r.error);
			continue;
		}
		const ScxModel& m = r.model;
		unsigned long fileVertices = 0;
		unsigned long fileIndices = 0;
		for (const ScxMesh& mesh : m.meshes) {
			fileVertices += mesh.vertexCount;
			fileIndices += mesh.indices.size();
		}
		++opened;
		++byVersion[m.version];
		materials += m.materials.size();
		meshes += m.meshes.size();
		vertices += fileVertices;
		indices += fileIndices;
		outOfRange += m.indicesOutOfRange;
		incomplete += m.incompleteTriangles;
		nonFinite += m.nonFinitePositions;
		trailing += static_cast<unsigned long>(m.trailingBytes);
		mismatches += m.directoryMismatches;
		if (perFile) {
			std::printf("%s v%u materials=%zu meshes=%zu vertices=%lu indices=%lu\n", rel.c_str(), m.version,
				m.materials.size(), m.meshes.size(), fileVertices, fileIndices);
		}
	}

	std::printf("files found: %zu\n", files.size());
	std::printf("files opened: %lu\n", opened);
	std::printf("files failed: %zu\n", failures.size());
	for (const auto& v : byVersion) {
		std::printf("version %u files: %lu\n", v.first, v.second);
	}
	std::printf("total materials: %lu\n", materials);
	std::printf("total meshes: %lu\n", meshes);
	std::printf("total vertices: %lu\n", vertices);
	std::printf("total indices: %lu\n", indices);
	std::printf("indices out of range: %lu\n", outOfRange);
	std::printf("incomplete triangles: %lu\n", incomplete);
	std::printf("non-finite positions: %lu\n", nonFinite);
	std::printf("trailing bytes: %lu\n", trailing);
	std::printf("directory mismatches: %lu\n", mismatches);
	std::puts("failed files:");
	if (failures.empty()) {
		std::puts("  none");
	}
	for (const auto& f : failures) {
		std::printf("  %s: %s\n", f.first.c_str(), f.second.c_str());
	}

	// The stock SCY loader skips the vertex and triangle counts, so a file that has any carries data it never reads.
	if (std::strcmp(ext, ".scy") == 0 && (vertices != 0 || indices != 0)) {
		std::fputs(".scy files hold mesh data the stock SCY loader skips\n", stderr);
		return 1;
	}
	if (files.empty() || !failures.empty()) {
		return 1;
	}
	if (hasExpect && files.size() != expect) {
		std::fprintf(stderr, "expected %lu files, found %zu\n", expect, files.size());
		return 1;
	}
	return 0;
}

// The one class file the stock loader also rejects. See docs/decisions.tsv.
static const char* const kStaleClass = "system/Scripts/render/osd/Dialog.class";
constexpr uint32_t kStaleClassVersion = 0x108f5;

static int tufaCensus(const char* dir, bool perFile, bool hasExpect, unsigned long expect, unsigned long expectStale) {
	std::error_code ec;
	std::vector<fs::path> files;
	fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
	if (ec) {
		std::fprintf(stderr, "cannot walk %s: %s\n", dir, ec.message().c_str());
		return 2;
	}
	for (const fs::directory_entry& de : it) {
		if (de.is_regular_file(ec) && hasExtension(de.path(), ".class")) {
			files.push_back(de.path());
		}
	}
	std::sort(files.begin(), files.end());

	unsigned long opened = 0;
	unsigned long classes = 0;
	unsigned long constants = 0;
	unsigned long fields = 0;
	unsigned long methods = 0;
	unsigned long interfaces = 0;
	unsigned long trees = 0;
	unsigned long nodes = 0;
	unsigned int highestOp = 0;
	std::map<uint32_t, unsigned long> byVersion;
	std::map<std::string, unsigned long> byOrder;
	std::vector<std::pair<std::string, std::string>> stale;
	std::vector<std::pair<std::string, std::string>> failures;
	for (const fs::path& p : files) {
		const std::string rel = p.lexically_relative(dir).string();
		const TufaResult r = tufaLoadFile(p.string());
		if (!r.ok) {
			if (rel == kStaleClass && r.unknownConstantKind && r.hasFirstVersion && r.firstVersion == kStaleClassVersion) {
				stale.emplace_back(rel, r.error);
			} else {
				failures.emplace_back(rel, r.error);
			}
			continue;
		}
		++opened;
		std::string line = rel + " OK";
		bool first = true;
		for (const TufaClass& c : r.file.classes) {
			unsigned long classFields = c.fields[0].size() + c.fields[1].size();
			unsigned long classMethods = c.methods[0].size() + c.methods[1].size();
			unsigned long classNodes = 0;
			for (const std::vector<TufaNode>& tree : c.trees) {
				classNodes += tree.size();
				for (const TufaNode& n : tree) {
					highestOp = std::max<unsigned int>(highestOp, n.op);
				}
			}
			++classes;
			constants += c.constants.size();
			fields += classFields;
			methods += classMethods;
			interfaces += c.interfaces.size();
			trees += c.trees.size();
			nodes += classNodes;
			++byVersion[c.version];
			++byOrder[c.chunkOrder];
			char buf[512];
			std::snprintf(buf, sizeof buf, "%s %s v=0x%x order=%s cons=%zu fild=%lu mthd=%lu iface=%zu trees=%zu nodes=%lu",
				first ? "" : " |", c.name.c_str(), c.version, c.chunkOrder.c_str(), c.constants.size(), classFields,
				classMethods, c.interfaces.size(), c.trees.size(), classNodes);
			line += buf;
			first = false;
		}
		if (perFile) {
			std::puts(line.c_str());
		}
	}

	std::printf("files found: %zu\n", files.size());
	std::printf("files opened: %lu\n", opened);
	std::printf("files known stale: %zu\n", stale.size());
	std::printf("files failed: %zu\n", failures.size());
	std::printf("classes: %lu\n", classes);
	std::printf("constants: %lu\n", constants);
	std::printf("field records: %lu\n", fields);
	std::printf("method records: %lu\n", methods);
	std::printf("interfaces: %lu\n", interfaces);
	std::printf("trees: %lu\n", trees);
	std::printf("tree nodes: %lu\n", nodes);
	std::printf("highest tree op: 0x%02x\n", highestOp);
	std::string versions = "versions:";
	for (const auto& v : byVersion) {
		char buf[64];
		std::snprintf(buf, sizeof buf, " 0x%x=%lu", v.first, v.second);
		versions += buf;
	}
	std::puts(versions.c_str());
	std::string orders = "chunk orders:";
	for (const auto& o : byOrder) {
		orders += " " + o.first + "=" + std::to_string(o.second);
	}
	std::puts(orders.c_str());
	std::puts("known stale files:");
	if (stale.empty()) {
		std::puts("  none");
	}
	for (const auto& f : stale) {
		std::printf("  %s: %s, old compiler version 0x%x\n", f.first.c_str(), f.second.c_str(), kStaleClassVersion);
	}
	std::puts("failed files:");
	if (failures.empty()) {
		std::puts("  none");
	}
	for (const auto& f : failures) {
		std::printf("  %s: %s\n", f.first.c_str(), f.second.c_str());
	}

	if (files.empty() || !failures.empty()) {
		return 1;
	}
	if (stale.size() != expectStale) {
		std::fprintf(stderr, "expected %lu known stale files, found %zu\n", expectStale, stale.size());
		return 1;
	}
	if (hasExpect && files.size() != expect) {
		std::fprintf(stderr, "expected %lu files, found %zu\n", expect, files.size());
		return 1;
	}
	return 0;
}

static bool startsWithSdat(const fs::path& p) {
	std::FILE* f = std::fopen(p.string().c_str(), "rb");
	if (f == nullptr) {
		return false;
	}
	char magic[4] = {};
	const size_t n = std::fread(magic, 1, sizeof magic, f);
	std::fclose(f);
	return n == sizeof magic && std::memcmp(magic, "SDAT", 4) == 0;
}

static int sdatCensus(const char* dir, bool perFile, bool hasExpect, unsigned long expect) {
	std::error_code ec;
	std::vector<fs::path> files;
	fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
	if (ec) {
		std::fprintf(stderr, "cannot walk %s: %s\n", dir, ec.message().c_str());
		return 2;
	}
	for (const fs::directory_entry& de : it) {
		if (de.is_regular_file(ec) && startsWithSdat(de.path())) {
			files.push_back(de.path());
		}
	}
	std::sort(files.begin(), files.end());

	unsigned long opened = 0;
	unsigned long unsaved = 0;
	unsigned long packs = 0;
	unsigned long blobBytes = 0;
	unsigned long zeroOrdinals = 0;
	unsigned long duplicates = 0;
	unsigned long largeOrdinals = 0;
	std::vector<std::pair<std::string, std::string>> failures;
	for (const fs::path& p : files) {
		const std::string rel = p.lexically_relative(dir).string();
		const SdatResult r = sdatLoadFile(p.string());
		if (!r.ok) {
			failures.emplace_back(rel, r.error);
			continue;
		}
		const SdatFile& f = r.file;
		unsigned long fileBlob = 0;
		std::string kinds;
		for (const SdatUnsaved& u : f.unsaved) {
			if (u.kind == 7) {
				fileBlob += u.size;
			}
			kinds += (kinds.empty() ? "" : ",") + std::to_string(u.kind);
		}
		std::string names;
		for (const SdatPack& pk : f.packs) {
			names += (names.empty() ? "" : ",") + std::to_string(pk.ordinal) + "=" + pk.path;
		}
		++opened;
		unsaved += f.unsaved.size();
		packs += f.packs.size();
		blobBytes += fileBlob;
		zeroOrdinals += f.zeroOrdinals;
		duplicates += f.duplicateOrdinals;
		largeOrdinals += f.largeOrdinals;
		if (perFile) {
			std::printf("%s OK type=%u flags=%u payload=%llu unsaved=%zu blobBytes=%lu kinds=[%s] packs=%zu [%s]\n", rel.c_str(),
				f.type, f.flags, static_cast<unsigned long long>(f.payloadSize), f.unsaved.size(), fileBlob, kinds.c_str(),
				f.packs.size(), names.c_str());
		}
	}

	std::printf("files found: %zu\n", files.size());
	std::printf("files opened: %lu\n", opened);
	std::printf("files failed: %zu\n", failures.size());
	std::printf("unsaved resource records: %lu\n", unsaved);
	std::printf("embedded blob bytes: %lu\n", blobBytes);
	std::printf("pack records: %lu\n", packs);
	std::printf("pack ordinals of 0: %lu\n", zeroOrdinals);
	std::printf("duplicate pack ordinals: %lu\n", duplicates);
	std::printf("pack ordinals of 0xffff or more: %lu\n", largeOrdinals);
	std::puts("failed files:");
	if (failures.empty()) {
		std::puts("  none");
	}
	for (const auto& f : failures) {
		std::printf("  %s: %s\n", f.first.c_str(), f.second.c_str());
	}

	if (files.empty() || !failures.empty()) {
		return 1;
	}
	if (hasExpect && files.size() != expect) {
		std::fprintf(stderr, "expected %lu files, found %zu\n", expect, files.size());
		return 1;
	}
	return 0;
}

int main(int argc, char** argv) {
	if (argc == 2 && std::strcmp(argv[1], "--selftest") == 0) {
		std::puts("sl_port selftest ok: arm64, 64-bit pointers");
		return 0;
	}
	if (argc >= 3 && std::strcmp(argv[1], "--rpk-census") == 0) {
		bool hasExpect = false;
		unsigned long expect = 0;
		if (argc == 5 && std::strcmp(argv[3], "--expect") == 0) {
			char* end = nullptr;
			expect = std::strtoul(argv[4], &end, 10);
			if (end == argv[4] || *end != '\0') {
				std::fprintf(stderr, "bad --expect value: %s\n", argv[4]);
				return 2;
			}
			hasExpect = true;
		} else if (argc != 3) {
			std::fputs("usage: sl_port --rpk-census <dir> [--expect <N>]\n", stderr);
			return 2;
		}
		return rpkCensus(argv[2], hasExpect, expect);
	}
	if (argc >= 3 && std::strcmp(argv[1], "--scx-census") == 0) {
		bool perFile = false;
		bool hasExpect = false;
		unsigned long expect = 0;
		for (int i = 3; i < argc; ++i) {
			if (std::strcmp(argv[i], "--per-file") == 0) {
				perFile = true;
			} else if (std::strcmp(argv[i], "--expect") == 0 && i + 1 < argc) {
				char* end = nullptr;
				expect = std::strtoul(argv[i + 1], &end, 10);
				if (end == argv[i + 1] || *end != '\0') {
					std::fprintf(stderr, "bad --expect value: %s\n", argv[i + 1]);
					return 2;
				}
				hasExpect = true;
				++i;
			} else {
				std::fputs("usage: sl_port --scx-census <dir> [--per-file] [--expect <N>]\n", stderr);
				return 2;
			}
		}
		return scxCensus(argv[2], ".scx", perFile, hasExpect, expect);
	}
	if (argc >= 3 && std::strcmp(argv[1], "--scy-census") == 0) {
		bool perFile = false;
		bool hasExpect = false;
		unsigned long expect = 0;
		for (int i = 3; i < argc; ++i) {
			if (std::strcmp(argv[i], "--per-file") == 0) {
				perFile = true;
			} else if (std::strcmp(argv[i], "--expect") == 0 && i + 1 < argc) {
				char* end = nullptr;
				expect = std::strtoul(argv[i + 1], &end, 10);
				if (end == argv[i + 1] || *end != '\0') {
					std::fprintf(stderr, "bad --expect value: %s\n", argv[i + 1]);
					return 2;
				}
				hasExpect = true;
				++i;
			} else {
				std::fputs("usage: sl_port --scy-census <dir> [--per-file] [--expect <N>]\n", stderr);
				return 2;
			}
		}
		return scxCensus(argv[2], ".scy", perFile, hasExpect, expect);
	}
	if (argc >= 3 && std::strcmp(argv[1], "--class-census") == 0) {
		bool perFile = false;
		bool hasExpect = false;
		unsigned long expect = 0;
		unsigned long expectStale = 0;
		for (int i = 3; i < argc; ++i) {
			char* end = nullptr;
			if (std::strcmp(argv[i], "--per-file") == 0) {
				perFile = true;
			} else if (std::strcmp(argv[i], "--expect") == 0 && i + 1 < argc) {
				expect = std::strtoul(argv[i + 1], &end, 10);
				if (end == argv[i + 1] || *end != '\0') {
					std::fprintf(stderr, "bad --expect value: %s\n", argv[i + 1]);
					return 2;
				}
				hasExpect = true;
				++i;
			} else if (std::strcmp(argv[i], "--expect-stale") == 0 && i + 1 < argc) {
				expectStale = std::strtoul(argv[i + 1], &end, 10);
				if (end == argv[i + 1] || *end != '\0') {
					std::fprintf(stderr, "bad --expect-stale value: %s\n", argv[i + 1]);
					return 2;
				}
				++i;
			} else {
				std::fputs("usage: sl_port --class-census <dir> [--per-file] [--expect <N>] [--expect-stale <N>]\n", stderr);
				return 2;
			}
		}
		return tufaCensus(argv[2], perFile, hasExpect, expect, expectStale);
	}
	if (argc >= 3 && std::strcmp(argv[1], "--sdat-census") == 0) {
		bool perFile = false;
		bool hasExpect = false;
		unsigned long expect = 0;
		for (int i = 3; i < argc; ++i) {
			if (std::strcmp(argv[i], "--per-file") == 0) {
				perFile = true;
			} else if (std::strcmp(argv[i], "--expect") == 0 && i + 1 < argc) {
				char* end = nullptr;
				expect = std::strtoul(argv[i + 1], &end, 10);
				if (end == argv[i + 1] || *end != '\0') {
					std::fprintf(stderr, "bad --expect value: %s\n", argv[i + 1]);
					return 2;
				}
				hasExpect = true;
				++i;
			} else {
				std::fputs("usage: sl_port --sdat-census <dir> [--per-file] [--expect <N>]\n", stderr);
				return 2;
			}
		}
		return sdatCensus(argv[2], perFile, hasExpect, expect);
	}
	std::puts("sl_port has nothing to run yet, see docs/PLAN.md");
	return 0;
}
