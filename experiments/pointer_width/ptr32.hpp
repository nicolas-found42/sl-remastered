#pragma once
#include <cstdint>
#include <vector>

class HandleTable {
public:
	static uint32_t add(void* p) {
		if (!p) return 0;
		auto& t = table();
		for (uint32_t i = 1; i < t.size(); ++i)
			if (t[i] == p) return i;
		t.push_back(p);
		return static_cast<uint32_t>(t.size() - 1);
	}
	static void* get(uint32_t h) { return h ? table()[h] : nullptr; }

private:
	static std::vector<void*>& table() {
		static std::vector<void*> t{nullptr};
		return t;
	}
};

struct Ptr32 {
	uint32_t h = 0;
	Ptr32() = default;
	Ptr32(void* p) : h(HandleTable::add(p)) {}
	Ptr32& operator=(void* p) { h = HandleTable::add(p); return *this; }
	operator void*() const { return HandleTable::get(h); }
};
static_assert(sizeof(Ptr32) == 4, "a handle must occupy the 4 bytes the original pointer did");
