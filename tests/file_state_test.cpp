#define _DARWIN_C_SOURCE
#include "file_state.hpp"
#include "vm.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

int main() {
	char name[] = "/tmp/sl-file-state-XXXXXX";
	const char* temp = mkdtemp(name);
	CHECK(temp != nullptr);
	if (!temp) return 1;
	const std::filesystem::path root(temp);
	std::filesystem::create_directories(root / "save/game");
	std::filesystem::copy_file("ref/raw/save/game/options", root / "save/game/options");
	const std::vector<uint8_t> shortPayload{'S', 'D', 'A', 'T', 0, 1, 2, 0, 1, 2, 3,
	                                        0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0};
	std::ofstream out(root / "save/game/short", std::ios::binary);
	out.write(reinterpret_cast<const char*>(shortPayload.data()), static_cast<std::streamsize>(shortPayload.size()));
	out.close();
	VmObject first, second, unopened;
	std::filesystem::create_directories(root / "assets/Dir");
	std::ofstream(root / "assets/z.txt") << "z";
	std::ofstream(root / "assets/A.TXT") << "a";
	FileState files(root, "ref", root);
	int32_t searchHandle = -1;
	std::optional<std::string> found;
	std::string searchError;
	CHECK(files.findFirst("assets/*.txt", 1, searchHandle, found, searchError));
	CHECK(searchHandle == 0 && found == "A.TXT");
	CHECK(files.findNext(searchHandle, 1) == "z.txt");
	CHECK(!files.findNext(searchHandle, 1));
	files.findClose(searchHandle);
	CHECK(files.findFirst("assets/*.*", 2, searchHandle, found, searchError));
	CHECK(searchHandle == 0 && found == "Dir");
	CHECK(!files.findNext(searchHandle, 2));
	files.findClose(searchHandle);
	CHECK(files.findFirst("assets/*.missing", 0, searchHandle, found, searchError));
	CHECK(searchHandle == -1 && !found);
	CHECK(!files.findFirst("assets/../../*", 0, searchHandle, found, searchError));
	std::string error;
	int32_t value = 0;
	float f = 0;
	CHECK(files.openRead(&first, "save/game/options", error) == FileOpenResult::opened);
	CHECK(files.openRead(&second, "save/game/options", error) == FileOpenResult::opened);
	CHECK(files.handle(&first) && files.handle(&second) && files.handle(&first) != files.handle(&second));
	CHECK(files.readInt(&first, value, error));
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof bits);
	CHECK(bits == 0xfedcba98u);
	CHECK(files.readInt(&first, value, error) && value == 12);
	CHECK(files.readInt(&second, value, error));
	std::memcpy(&bits, &value, sizeof bits);
	CHECK(bits == 0xfedcba98u);
	for (int i = 0; i < 4; ++i) CHECK(files.readInt(&first, value, error));
	CHECK(files.readFloat(&first, f, error));
	std::memcpy(&bits, &f, sizeof bits);
	CHECK(bits == 0x3f806166u);
	CHECK(files.openRead(&first, "save/game/options", error) == FileOpenResult::opened);
	CHECK(files.readInt(&first, value, error));
	std::memcpy(&bits, &value, sizeof bits);
	CHECK(bits == 0xfedcba98u);
	CHECK(files.openRead(&first, "save/game/missing", error) == FileOpenResult::missing);
	CHECK(!files.handle(&first));
	CHECK(!files.readInt(&first, value, error) && error == "file is not open");
	CHECK(files.readInt(&second, value, error) && value == 12);
	CHECK(files.openRead(&first, "save/../save/game/options", error) == FileOpenResult::error);
	CHECK(files.openRead(nullptr, "save/game/options", error) == FileOpenResult::error);
	CHECK(!files.readInt(&unopened, value, error) && error == "file is not open");
	FileState invalidRoot("ref/raw", "ref");
	CHECK(invalidRoot.openRead(&first, "save/game/options", error) == FileOpenResult::error);
	CHECK(files.openRead(&first, "save/game/short", error) == FileOpenResult::opened);
	CHECK(!files.readInt(&first, value, error) && error == "truncated file payload");
	CHECK(!files.readFloat(&first, f, error) && error == "truncated file payload");
	files.close(&first); files.close(&first); files.close(&second);
	CHECK(!files.readInt(&second, value, error) && error == "file is not open");
	std::filesystem::remove_all(root);
	return failures ? 1 : 0;
}
