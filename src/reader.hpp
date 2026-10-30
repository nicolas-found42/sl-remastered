#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

inline uint16_t readLe16(const uint8_t* p) {
	return static_cast<uint16_t>(p[0] | p[1] << 8);
}

inline uint32_t readLe32(const uint8_t* p) {
	return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
		static_cast<uint32_t>(p[3]) << 24;
}

// Result needs ok, error and errorOffset. Every read checks its length against `limit` in 64-bit math first.
template <class Result>
struct Reader {
	const uint8_t* data;
	uint64_t pos;
	uint64_t limit;
	Result& out;

	bool fail(const char* what) {
		out.ok = false;
		out.errorOffset = pos;
		out.error = std::string(what) + " at offset " + std::to_string(pos);
		return false;
	}

	bool have(uint64_t n, const char* what) {
		if (n > limit - pos) {
			return fail(what);
		}
		return true;
	}

	bool u8(uint8_t& v, const char* what) {
		if (!have(1, what)) {
			return false;
		}
		v = data[pos++];
		return true;
	}

	bool u16(uint16_t& v, const char* what) {
		if (!have(2, what)) {
			return false;
		}
		v = readLe16(data + pos);
		pos += 2;
		return true;
	}

	bool u32(uint32_t& v, const char* what) {
		if (!have(4, what)) {
			return false;
		}
		v = readLe32(data + pos);
		pos += 4;
		return true;
	}

	bool f32(float& v, const char* what) {
		uint32_t bits = 0;
		if (!u32(bits, what)) {
			return false;
		}
		std::memcpy(&v, &bits, sizeof v);
		return true;
	}

	bool str(std::string& v, uint64_t n, const char* what) {
		if (!have(n, what)) {
			return false;
		}
		const char* p = reinterpret_cast<const char*>(data + pos);
		v.assign(p, strnlen(p, static_cast<size_t>(n)));
		pos += n;
		return true;
	}

	bool skip(uint64_t n, const char* what) {
		if (!have(n, what)) {
			return false;
		}
		pos += n;
		return true;
	}

	bool append(std::vector<uint8_t>& v, uint64_t n, const char* what) {
		if (!have(n, what)) {
			return false;
		}
		v.insert(v.end(), data + pos, data + pos + n);
		pos += n;
		return true;
	}
};
