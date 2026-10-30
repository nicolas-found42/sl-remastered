#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct VideoFrame {
	int width = 0, height = 0;
	double seconds = 0;
	std::vector<uint8_t> rgba;
};
enum class VideoRead { Frame, End, Error };

// FFmpeg decoder for the first video stream. Audio and presentation are separate.
class VideoDecoder {
public:
	static std::unique_ptr<VideoDecoder> open(const std::filesystem::path& file, std::string& error);
	~VideoDecoder();
	VideoDecoder(const VideoDecoder&) = delete;
	VideoDecoder& operator=(const VideoDecoder&) = delete;
	VideoRead next(VideoFrame& frame, std::string& error);
	bool rewind(std::string& error);
private:
	VideoDecoder();
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
