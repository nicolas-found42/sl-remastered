#pragma once
#include "video.hpp"
#include "natives/game_type.hpp"
#include "render2d.hpp"
#include <optional>

// Mode 1 video background with an explicitly supplied monotonic clock.
class VideoPlayer final : public VmVideoControl {
public:
	VideoPlayer(Render2D& renderer, std::filesystem::path root) : renderer_(renderer), root_(std::move(root)) {}
	~VideoPlayer();
	bool open(const std::string& relative, bool loop, double now, std::string& error) override;
	void close() override;
	bool playing() const override { return decoder_ != nullptr; }
	bool draw(double now, std::string& error);
private:
	Render2D& renderer_;
	std::filesystem::path root_;
	std::unique_ptr<VideoDecoder> decoder_;
	VideoFrame current_, next_;
	std::optional<size_t> texture_;
	double origin_ = 0, frameDuration_ = 0;
	bool loop_ = false, haveNext_ = false, dirty_ = false;
};
