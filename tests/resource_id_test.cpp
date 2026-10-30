#include "resource_id.hpp"
#include "rpk.hpp"

#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

int main() {
	const auto frontend = rpkLoadFile("ref/raw/frontend.rpk");
	CHECK(frontend.ok);
	if (!frontend.ok) return 1;
	const auto& deps = frontend.pack.dependencies;
	CHECK(deps.size() == 3);
	if (deps.size() != 3) return 1;
	CHECK(deps[0].id == 0x10000 && deps[0].path == "system.rpk");
	CHECK(deps[1].id == 0x20000 && deps[1].path == "misc\\garage.rpk");
	CHECK(deps[2].id == 0x30000 && deps[2].path == "cars.rpk");

	const ResourceIdMapping mapping{7, 10, {2, 4, 9}};
	CHECK(remapResourceId(0x28, mapping) == 0x00070028);
	CHECK(remapResourceId(0x00010028, mapping) == 0x00020028);
	CHECK(remapResourceId(0x00020028, mapping) == 0x00040028);
	CHECK(remapResourceId(0x00030028, mapping) == 0x00090028);
	CHECK(remapResourceId(0x00040028, mapping) == 0);
	CHECK(remapResourceId(0x00010000, mapping) == 0);
	CHECK(remapResourceId(0x00000000, mapping) == 0);
	CHECK(remapResourceId(0x00030028, ResourceIdMapping{7, 3, {2, 4, 9}}) == 0);
	CHECK(remapResourceId(0x00000028, ResourceIdMapping{7, 0, {2, 4, 9}}) == 0);
	CHECK(remapResourceId(0x00030000, ResourceIdMapping{0xffff, 0, {}}) == 0x00030000);
	CHECK(remapResourceId(0xffff1234, ResourceIdMapping{0xffff, 0, {}}) == 0xffff1234);
	CHECK(remapResourceId(0x00010028, ResourceIdMapping{7, 10, {0xffff}}) == 0xffff0028);
	return failures ? 1 : 0;
}
