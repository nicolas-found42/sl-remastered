#define _DARWIN_C_SOURCE
#include "class_environment.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

int main() {
	std::string error;
	ClassEnvironment classes("ref/raw");
	const auto* config = classes.findOrLoad("java.util.Config", error);
	CHECK(config && config->name == "java.util.Config");
	const auto* car = classes.loadScriptPath("cars\\racers\\SuperDuty_data\\scripts\\Hauler_s_VT.class", error);
	CHECK(car && !car->name.empty());
	CHECK(car && classes.findOrLoad(car->name, error) == car);
	CHECK(car && classes.loadScriptPath("cars/racers/SuperDuty_data/scripts/Hauler_s_VT.class", error) == car);
	CHECK(!classes.loadScriptPath("../raw/system/Scripts/util/Config.class", error));
	const auto* video = classes.findOrLoad("java.util.VideoMode", error);
	CHECK(video && video->name == "java.util.VideoMode");
	const auto before = classes.classes().size();
	CHECK(classes.findOrLoad("java.util.Config", error) == config && classes.classes().size() == before);
	const auto* menu = classes.findOrLoad("java.game.MainMenu", error);
	CHECK(menu && menu->name == "java.game.MainMenu");
	const auto* dialog = classes.findOrLoad("java.game.MainMenuDialog", error);
	CHECK(dialog && dialog->name == "java.game.MainMenuDialog");
	CHECK(config && config->name == "java.util.Config" && classes.findOrLoad("java.util.Config", error) == config);
	const auto* gameRef = classes.findOrLoad("java.util.resource.GameRef", error);
	CHECK(gameRef && gameRef->name == "java.util.resource.GameRef");
	ClassEnvironment cold("ref/raw");
	CHECK(!cold.findOrLoad("java.game.MainMenuDialog", error));
	CHECK(cold.classes().empty());
	ClassEnvironment duplicate("ref/raw");
	CHECK(duplicate.findOrLoad("java.util.VideoMode", error));
	const auto count = duplicate.classes().size();
	CHECK(!duplicate.findOrLoad("java.util.Config", error));
	CHECK(error.find("duplicate class") != std::string::npos && duplicate.classes().size() == count);
	CHECK(!classes.findOrLoad("java.game..MainMenu", error));
	CHECK(!classes.findOrLoad("java.game/../../MainMenu", error));
	CHECK(!classes.findOrLoad("java.gamex.MainMenu", error));
	CHECK(!classes.findOrLoad("java.util.Config/../../etc", error));
	CHECK(!classes.findOrLoad("java.util.NoSuchClass", error));
	const auto knownCount = classes.classes().size();
	CHECK(!classes.findOrLoad("java.render.osd.Dialog", error) && classes.classes().size() == knownCount);
	char directory[] = "/tmp/sl-class-env-XXXXXX";
	const char* temp = mkdtemp(directory);
	CHECK(temp != nullptr);
	if (temp) {
		const std::filesystem::path root(temp);
		std::filesystem::create_directories(root / "system/Scripts/util");
		std::ofstream(root / "system/Scripts/util/Config.class", std::ios::binary) << "truncated";
		ClassEnvironment invalid(root);
		CHECK(!invalid.findOrLoad("java.util.Config", error) && invalid.classes().empty());
		std::filesystem::create_symlink("/etc/passwd", root / "system/Scripts/util/Escape.class");
		CHECK(!invalid.findOrLoad("java.util.Escape", error) && invalid.classes().empty());
		CHECK(!invalid.loadScriptPath("system/Scripts/util/Escape.class", error) && invalid.classes().empty());
		std::filesystem::remove_all(root);
	}
	return failures ? 1 : 0;
}
