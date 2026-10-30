#include "natives/registry.hpp"

#include <cstdio>
#include <set>
#include <string>
#include <tuple>

static void jsonString(std::string_view value) {
	std::putchar('"');
	for (unsigned char c : value) {
		if (c == '"' || c == '\\') std::printf("\\%c", c);
		else if (c < 0x20) std::printf("\\u%04x", static_cast<unsigned>(c));
		else std::putchar(c);
	}
	std::putchar('"');
}

int main() {
	std::set<std::tuple<std::string_view, std::string_view, std::string_view>> seen;
	for (const auto& entry : nativeRegistry()) {
		if (!entry.handler || !seen.emplace(entry.className, entry.method, entry.descriptor).second) return 1;
	}
	std::putchar('[');
	bool first = true;
	for (const auto& entry : nativeRegistry()) {
		if (!first) std::putchar(',');
		first = false;
		std::printf("{\"class\":"); jsonString(entry.className);
		std::printf(",\"method\":"); jsonString(entry.method);
		std::printf(",\"descriptor\":"); jsonString(entry.descriptor);
		std::printf(",\"handler\":"); jsonString(entry.handlerAddress);
		std::putchar('}');
	}
	std::puts("]");
}
