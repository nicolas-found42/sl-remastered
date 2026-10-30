#define _DARWIN_C_SOURCE
#include "pack_registry.hpp"
#include "resource_texture.hpp"
#include "vm.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <string>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

static void put32(std::vector<uint8_t>& bytes, uint32_t n) {
	for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(n >> (8 * i)));
}

int main() {
	PackRegistry registry("ref/raw");
	std::string error;
	uint32_t config = 0, init = 0, again = 0, texture = 0, dependency = 0;
	CHECK(registry.resolveLiteral("system.rpk", 0x47, config, error));
	{
		const auto parsed = tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
		CHECK(parsed.ok && !parsed.file.classes.empty());
		if (parsed.ok && !parsed.file.classes.empty()) {
			auto cls = parsed.file.classes[0];
			// Keep the stock init field and its tree; omit the parent and other fields
			// because the bounded class initializer does not yet support them all.
			cls.classHeader[3] = UINT32_MAX;
			for (auto& list : cls.fields) {
				std::vector<TufaField> selected;
				for (const auto& field : list)
					if ((field.words[0] & 8) && field.words[1] < cls.constants.size() &&
					    cls.constants[field.words[1]].text == "init") selected.push_back(field);
				list = std::move(selected);
			}
			std::vector<const TufaClass*> classes{&cls};
			std::vector<VmStaticFields> states;
			VmResourceResolver resolver = [&](std::string_view pack, uint32_t id) -> std::optional<uint32_t> {
				uint32_t resolved = 0;
				if (!registry.resolveLiteral(std::string(pack), id, resolved, error)) return std::nullopt;
				return resolved;
			};
			const auto initialized = vmInitializeClass(cls, classes, states, &resolver);
			CHECK(initialized.ok);
			CHECK(states.size() == 1 && states[0].fields.size() == 1 && states[0].fields[0].name == "init");
			if (initialized.ok && states.size() == 1 && states[0].fields.size() == 1) {
				const auto* value = std::get_if<int32_t>(&states[0].fields[0].value);
				CHECK(value != nullptr);
				if (value) {
					uint32_t id;
					std::memcpy(&id, value, sizeof id);
					PackResource startup;
					CHECK(registry.lookup(id, startup, error) && startup.entry.resType == 8);
					if (startup.packBytes) {
						const auto& bytes = *startup.packBytes;
						const std::string script(bytes.begin() + startup.entry.payloadOffset,
						                         bytes.begin() + startup.entry.payloadOffset + startup.entry.payloadSize);
						CHECK(script == "script java.game.Init\r\n");
					}
				}
			}
			std::vector<VmStaticFields> missingStates;
			VmResourceResolver reject = [](std::string_view, uint32_t) -> std::optional<uint32_t> { return std::nullopt; };
			CHECK(!vmInitializeClass(cls, classes, missingStates, &reject).ok && missingStates.empty());
		}
	}
	CHECK(registry.resolveLiteral("sl.rpk", 0x40, init, error));
	CHECK(registry.resolveLiteral("SL.RPK", 0x40, again, error) && again == init);
	PackResource resource;
	CHECK(registry.lookup(config, resource, error) && resource.entry.resType == 8);
	CHECK(registry.lookup(init, resource, error) && resource.entry.resType == 8);
	if (resource.packBytes) {
		const auto& bytes = *resource.packBytes;
		const std::string payload(bytes.begin() + resource.entry.payloadOffset,
		                          bytes.begin() + resource.entry.payloadOffset + resource.entry.payloadSize);
		CHECK(payload == "script java.game.Init\r\n");
	}
	CHECK(registry.resolveLiteral("frontend.rpk", 0x28, texture, error));
	CHECK(registry.lookup(texture, resource, error) && resource.entry.resType == 7);
	if (resource.packBytes) {
		const auto textureFile = resourceTextureLookup(*resource.packBytes, 0x28, "ref/raw");
		CHECK(textureFile.ok && textureFile.path.filename() == "Info_holder.ptx");
	}
	CHECK(registry.resolveLiteral("sl.rpk", 0x00010047, dependency, error) && dependency == config);
	CHECK(registry.resolveLiteral("sl.rpk", 0, again, error) && again == 0);
	CHECK(registry.resolveLiteral("sl.rpk", 0xffff0040, again, error) && again == 0);
	CHECK(!registry.resolveLiteral("../sl.rpk", 0x40, again, error));
	CHECK(!registry.resolveLiteral("C:\\sl.rpk", 0x40, again, error));
	CHECK(!registry.resolveLiteral("/tmp/sl.rpk", 0x40, again, error));
	CHECK(!registry.resolveLiteral("missing.rpk", 0x40, again, error));
	CHECK(!registry.lookup(0, resource, error));
	CHECK(!registry.lookup(0xfffe0040, resource, error));
	CHECK(!registry.lookup((init & 0xffff0000) | 0xffff, resource, error));
	PackRegistry other("ref/raw");
	CHECK(other.resolveLiteral("sl.rpk", 0x40, again, error));
	CHECK(again != init);
	char directory[] = "/tmp/sl-pack-registry-XXXXXX";
	const char* temp = mkdtemp(directory);
	CHECK(temp != nullptr);
	if (temp) {
		const std::filesystem::path root(temp);
		std::vector<uint8_t> duplicate{'R', 'P', 'A', 'K'};
		put32(duplicate, 0x200); put32(duplicate, 0); put32(duplicate, 0);
		put32(duplicate, 46); put32(duplicate, 2); put32(duplicate, 0); put32(duplicate, 0);
		for (int i = 0; i < 2; ++i) {
			put32(duplicate, 0); put32(duplicate, 0x40); duplicate.push_back(8); duplicate.push_back(0);
			put32(duplicate, 0); put32(duplicate, 78); put32(duplicate, 0); duplicate.push_back(0);
		}
		std::ofstream output(root / "duplicate.rpk", std::ios::binary);
		output.write(reinterpret_cast<const char*>(duplicate.data()), static_cast<std::streamsize>(duplicate.size()));
		output.close();
		PackRegistry isolated(root);
		uint32_t duplicateId = 0;
		CHECK(isolated.resolveLiteral("duplicate.rpk", 0x40, duplicateId, error));
		CHECK(!isolated.lookup(duplicateId, resource, error));
		std::filesystem::create_symlink("/etc/passwd", root / "escape.rpk");
		CHECK(!isolated.resolveLiteral("escape.rpk", 0x40, again, error));
		std::filesystem::remove_all(root);
	}
	return failures ? 1 : 0;
}
