#pragma once
#include <cstddef>
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
enum class VideoRead { Frame,
											 End,
											 Error };
// One decoded audio chunk, resampled to interleaved stereo float at 48 kHz.
// `seconds` is the file-relative presentation time of the first sample.
struct VideoAudio {
	double seconds = 0;
	std::vector<float> pcm;
};

// FFmpeg decoder for the first video stream. Audio, when present, is decoded
// alongside and queued for presentation; playback is separate.
class VideoDecoder {
public:
	static std::unique_ptr<VideoDecoder> open(const std::filesystem::path& file, std::string& error);
	~VideoDecoder();
	VideoDecoder(const VideoDecoder&) = delete;
	VideoDecoder& operator=(const VideoDecoder&) = delete;
	VideoRead next(VideoFrame& frame, std::string& error);
	bool rewind(std::string& error);
	bool hasAudio() const;
	int audioSampleRate() const;
	// Moves queued audio chunks at or before `seconds` into `out`, capped at
	// `maxSamples` total floats (0 = no cap). Chunks beyond the cap stay queued.
	void drainAudio(double seconds, size_t maxSamples, std::vector<VideoAudio>& out);

private:
	VideoDecoder();
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
