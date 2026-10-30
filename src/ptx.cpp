#include "ptx.hpp"

#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#include <jpeglib.h>

namespace {
uint32_t word(const uint8_t* p) {
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

struct JpegError {
	jpeg_error_mgr base;
	jmp_buf jump;
};

void failJpeg(j_common_ptr c) {
	auto* error = reinterpret_cast<JpegError*>(c->err);
	longjmp(error->jump, 1);
}

void jpegMessage(j_common_ptr c, int level) {
	if (level < 0) failJpeg(c);
}

bool decodeJpeg(const uint8_t* data, size_t size, bool grayscale, uint32_t& width, uint32_t& height,
	std::vector<uint8_t>& output, std::string& error) {
	if (size > std::numeric_limits<unsigned long>::max()) {
		error = "JPEG stream too large";
		return false;
	}
	jpeg_decompress_struct decoder{};
	JpegError handler{};
	decoder.err = jpeg_std_error(&handler.base);
	handler.base.error_exit = failJpeg;
	handler.base.emit_message = jpegMessage;
	unsigned char* volatile buffer = nullptr;
	volatile bool created = false;
	if (setjmp(handler.jump)) {
		if (created) jpeg_destroy_decompress(&decoder);
		std::free(buffer);
		error = "invalid JPEG stream";
		return false;
	}
	jpeg_create_decompress(&decoder);
	created = true;
	jpeg_mem_src(&decoder, data, static_cast<unsigned long>(size));
	jpeg_read_header(&decoder, TRUE);
	decoder.out_color_space = grayscale ? JCS_GRAYSCALE : JCS_RGB;
	jpeg_start_decompress(&decoder);
	const size_t channels = grayscale ? 1 : 3;
	const size_t w = decoder.output_width;
	const size_t h = decoder.output_height;
	if (!w || !h || w > 16384 || h > 16384 || w * h > 67108864 || decoder.output_components != static_cast<int>(channels)) {
		jpeg_destroy_decompress(&decoder);
		error = "unsupported JPEG dimensions or channels";
		return false;
	}
	const size_t bytes = w * h * channels;
	buffer = static_cast<unsigned char*>(std::malloc(bytes));
	if (!buffer) {
		jpeg_destroy_decompress(&decoder);
		error = "JPEG allocation failed";
		return false;
	}
	while (decoder.output_scanline < decoder.output_height) {
		JSAMPROW row = buffer + size_t(decoder.output_scanline) * w * channels;
		jpeg_read_scanlines(&decoder, &row, 1);
	}
	jpeg_finish_decompress(&decoder);
	jpeg_destroy_decompress(&decoder);
	width = static_cast<uint32_t>(w);
	height = static_cast<uint32_t>(h);
	output.assign(buffer, buffer + bytes);
	std::free(buffer);
	return true;
}
}

bool ptxParse(const uint8_t* data, size_t size, PtxImage& image, std::string& error) {
	image = {};
	if (!data || size < 16) { error = "truncated PTX header"; return false; }
	const uint32_t count = word(data + 4);
	const uint32_t width = word(data + 8);
	const uint32_t height = word(data + 12);
	if (!count || count > 32 || !width || !height) { error = "invalid PTX dimensions or level count"; return false; }
	size_t offset = 16;
	std::vector<PtxLevel> levels;
	for (uint32_t i = 0; i < count; ++i) {
		if (offset > size || size - offset < 20) { error = "truncated PTX level header"; return false; }
		const uint32_t payload = word(data + offset);
		const uint32_t color = word(data + offset + 4);
		const uint32_t alpha = word(data + offset + 8);
		offset += 20;
		if (!color || uint64_t(color) + alpha != payload || payload > size - offset) {
			error = "invalid or truncated PTX level";
			return false;
		}
		levels.push_back({offset, color, offset + color, alpha});
		offset += payload;
	}
	if (offset != size) { error = "trailing PTX data"; return false; }
	image = {width, height, std::move(levels)};
	error.clear();
	return true;
}

bool ptxDecode(const uint8_t* data, size_t size, const PtxLevel& level, PtxPixels& pixels, std::string& error) {
	pixels = {};
	if (!data || level.colorOffset > size || level.colorSize > size - level.colorOffset ||
		level.alphaOffset > size || level.alphaSize > size - level.alphaOffset) {
		error = "PTX level outside input";
		return false;
	}
	uint32_t width = 0, height = 0, alphaWidth = 0, alphaHeight = 0;
	std::vector<uint8_t> color, alpha;
	if (!decodeJpeg(data + level.colorOffset, level.colorSize, false, width, height, color, error)) return false;
	if (level.alphaSize) {
		if (!decodeJpeg(data + level.alphaOffset, level.alphaSize, true, alphaWidth, alphaHeight, alpha, error)) return false;
		if (width != alphaWidth || height != alphaHeight) { error = "PTX JPEG dimensions differ"; return false; }
	} else {
		alpha.assign(static_cast<size_t>(width) * height, 255);
	}
	std::vector<uint8_t> rgba(alpha.size() * 4);
	for (size_t i = 0; i < alpha.size(); ++i) {
		rgba[i * 4] = color[i * 3];
		rgba[i * 4 + 1] = color[i * 3 + 1];
		rgba[i * 4 + 2] = color[i * 3 + 2];
		rgba[i * 4 + 3] = alpha[i];
	}
	pixels = {width, height, std::move(rgba)};
	error.clear();
	return true;
}
