#pragma once
#include "video.hpp"
#include "audio.hpp"
#include "natives/game_type.hpp"
#include "render2d.hpp"
#include <optional>

// Mode 1 video background with an explicitly supplied monotonic clock.
// When `audio` is non-null, a decoded audio stream is queued to it in sync
// with the video clock; null keeps playback silent.
class VideoPlayer final : public VmVideoControl {
public:
	VideoPlayer(Render2D& renderer, std::filesystem::path root, AudioOutput* audio = nullptr) : renderer_(renderer), root_(std::move(root)), audio_(audio) {}
	~VideoPlayer();
	bool open(const std::string& relative, bool loop, double now, std::string& error) override;
	void close() override;
	bool playing() const override { return decoder_ != nullptr; }
	bool draw(double now, std::string& error);
	// True after a non-looping video ended naturally with audio still queued;
	// the tail drains on the device and is cleared by the next open().
	bool drainingAudioTail() const { return audioTail_; }

private:
	// Video side only: releases texture and decoder, leaves queued audio.
	void closeVideo();
	Render2D& renderer_;
	std::filesystem::path root_;
	AudioOutput* audio_;
	std::unique_ptr<VideoDecoder> decoder_;
	VideoFrame current_, next_;
	std::optional<size_t> texture_;
	double origin_ = 0, frameDuration_ = 0;
	bool loop_ = false, haveNext_ = false, dirty_ = false;
	// Set when a non-looping video ended naturally: the queued audio tail keeps
	// playing on the device (stock's graph end-of-stream covers the audio
	// renderer) and is cleared by the next open(), not by close().
	bool audioTail_ = false;
};
