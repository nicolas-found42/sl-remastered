#include "ptx.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
	if (argc != 2) { std::fputs("usage: ptx_corpus <root>\n", stderr); return 2; }
	const fs::path root(argv[1]);
	std::error_code ec;
	fs::recursive_directory_iterator it(root, ec), end;
	if (ec) { std::fprintf(stderr, "%s: %s\n", argv[1], ec.message().c_str()); return 1; }
	std::vector<fs::path> files;
	while (it != end) {
		if (it->is_regular_file(ec)) {
			std::string ext = it->path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (ext == ".ptx") files.push_back(it->path());
		}
		if (ec) { std::fprintf(stderr, "directory entry: %s\n", ec.message().c_str()); return 1; }
		it.increment(ec);
		if (ec) { std::fprintf(stderr, "directory walk: %s\n", ec.message().c_str()); return 1; }
	}
	std::sort(files.begin(), files.end());
	if (files.empty()) { std::fputs("no PTX files found\n", stderr); return 1; }
	bool failed = false;
	for (const auto& path : files) {
		std::ifstream file(path, std::ios::binary);
		if (!file) { std::fprintf(stderr, "%s: cannot open\n", path.c_str()); failed = true; continue; }
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
		if (file.bad()) { std::fprintf(stderr, "%s: cannot read\n", path.c_str()); failed = true; continue; }
		PtxImage image{};
		std::string error;
		if (!ptxParse(bytes.data(), bytes.size(), image, error)) {
			std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str()); failed = true; continue;
		}
		for (size_t i = 0; i < image.levels.size(); ++i) {
			PtxPixels pixels{};
			if (!ptxDecode(bytes.data(), bytes.size(), image.levels[i], pixels, error)) {
				std::fprintf(stderr, "%s level %zu: %s\n", path.c_str(), i, error.c_str()); failed = true; continue;
			}
			uint64_t hash = UINT64_C(14695981039346656037);
			for (uint8_t byte : pixels.rgba) hash = (hash ^ byte) * UINT64_C(1099511628211);
			std::printf("%s\t%u\t%u\t%zu\t%u\t%u\t%016llx\n", path.lexically_relative(root).generic_string().c_str(),
				image.width, image.height, i, pixels.width, pixels.height, static_cast<unsigned long long>(hash));
		}
	}
	return failed ? 1 : 0;
}
