#define _DARWIN_C_SOURCE
#include "game_file.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                       \
  do {                                                    \
    if (!(cond)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
      ++failures;                                         \
    }                                                     \
  } while (0)

int main() {
  char name[] = "/tmp/sl-game-file-XXXXXX";
  const char* temp = mkdtemp(name);
  CHECK(temp != nullptr);
  if (!temp) return 1;
  const std::filesystem::path root(temp);
  std::filesystem::create_directories(root / "save/game");
  std::filesystem::copy_file("ref/raw/save/game/options", root / "save/game/options");
  GameFile file;
  std::string error;
  CHECK(file.open(root, "ref", "save/game/options", error));
  const auto magic = file.readInt();
  uint32_t magicBits = 0;
  if (magic) std::memcpy(&magicBits, &*magic, sizeof magicBits);
  CHECK(magic && magicBits == 0xfedcba98u);
  CHECK(file.readInt() == 12);
  CHECK(file.readInt() == 0);
  CHECK(file.readInt() == 800);
  CHECK(file.readInt() == 600);
  CHECK(file.readInt() == 16);
  const auto gamma = file.readFloat();
  uint32_t gammaBits = 0;
  if (gamma) std::memcpy(&gammaBits, &*gamma, sizeof gammaBits);
  CHECK(gamma && gammaBits == 0x3f806166u);
  CHECK(file.open(root, "ref", "save\\game\\options", error));
  CHECK(!file.open("ref/raw", "ref", "save/game/options", error));
  CHECK(!file.readInt());
  // Stock resolves "." / ".." in save paths (FUN_00485440 -> FUN_0054c880);
  // an in-root traversal opens normally while an escaping one is refused.
  CHECK(file.open(root, "ref", "save/../save/game/options", error));
  CHECK(file.readInt());
  CHECK(file.readInt() == 12);
  std::ofstream(root / "outside.txt", std::ios::binary) << "x";
  CHECK(!file.open(root, "ref", "save/../outside.txt", error));
  CHECK(!file.open(root, "ref", "/save/game/options", error));
  CHECK(!file.open(root, "ref", "save/game/missing", error));
  CHECK(!file.open(root, "ref", "other/options", error));
  CHECK(!file.open(root, "ref", "C:\\save\\game\\options", error));
  std::filesystem::create_symlink(std::filesystem::canonical("ref/raw/save/game/options"), root / "save/game/escape");
  CHECK(!file.open(root, "ref", "save/game/escape", error));
  const std::vector<uint8_t> shortPayload{'S', 'D', 'A', 'T', 0, 1, 2, 0, 1, 2, 3,
                                          0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0};
  std::ofstream out(root / "save/game/short", std::ios::binary);
  out.write(reinterpret_cast<const char*>(shortPayload.data()), static_cast<std::streamsize>(shortPayload.size()));
  out.close();
  CHECK(file.open(root, "ref", "save/game/short", error));
  CHECK(!file.readInt());
  CHECK(!file.readFloat());
  std::ofstream(root / "save/game/bad", std::ios::binary) << "SDAT";
  CHECK(!file.open(root, "ref", "save/game/bad", error));
  std::filesystem::remove_all(root);
  return failures ? 1 : 0;
}
