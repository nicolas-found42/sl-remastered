#include "video_player.hpp"
#include "run_log.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <system_error>
#include <vector>

namespace {
std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}
bool resolveVideo(const std::filesystem::path& root, std::string source, std::filesystem::path& out, std::string& error) {
  std::replace(source.begin(), source.end(), '\\', '/');
  if (source.empty() || source.front() == '/' || source.find(':') != std::string::npos) {
    error = "video: unsafe path";
    return false;
  }
  std::error_code ec;
  const auto base = std::filesystem::canonical(root, ec);
  if (ec) {
    error = "video: invalid root";
    return false;
  }
  auto current = base;
  for (const auto& part : std::filesystem::path(source)) {
    if (part == "." || part == ".." || part.empty()) {
      error = "video: unsafe path";
      return false;
    }
    std::filesystem::path found;
    for (std::filesystem::directory_iterator it(current, ec), end; !ec && it != end; it.increment(ec)) {
      if (lower(it->path().filename().string()) == lower(part.string())) {
        if (!found.empty()) {
          error = "video: ambiguous path";
          return false;
        }
        found = it->path();
      }
    }
    if (ec || found.empty()) {
      error = "video: file not found";
      return false;
    }
    current = found;
  }
  out = std::filesystem::canonical(current, ec);
  if (ec) {
    error = "video: invalid file";
    return false;
  }
  auto b = base.begin(), p = out.begin();
  for (; b != base.end() && p != out.end(); ++b, ++p)
    if (*b != *p) {
      error = "video: path escapes root";
      return false;
    }
  if (b != base.end() || !std::filesystem::is_regular_file(out, ec) || ec) {
    error = "video: unsafe file";
    return false;
  }
  return true;
}
} // namespace
VideoPlayer::~VideoPlayer() { close(); }
void VideoPlayer::closeVideo() {
  if (texture_) {
    std::string ignored;
    renderer_.releaseTexture(*texture_, ignored);
    texture_.reset();
  }
  if (decoder_) runlog::event(runlog::EventKind::Note, "video close");
  decoder_.reset();
  haveNext_ = false;
  current_ = {};
  next_ = {};
}
void VideoPlayer::close() {
  const bool hadAudio = decoder_ && decoder_->hasAudio();
  closeVideo();
  if (audioTail_) {
    // The previous video ended naturally: leave its queued audio tail playing
    // on the device (stock's graph end-of-stream covers the audio renderer).
    audioTail_ = false;
    if (hadAudio) runlog::event(runlog::EventKind::Note, "video audio tail left to drain");
    return;
  }
  if (audio_ && hadAudio) {
    std::string ignored;
    audio_->pause(ignored);
    audio_->clear(ignored);
    runlog::event(runlog::EventKind::Note, "video audio close");
  }
}
bool VideoPlayer::open(const std::string& path, bool loop, double now, std::string& error) {
  close();
  if (!std::isfinite(now)) {
    error = "video: invalid clock";
    return false;
  }
  std::filesystem::path file;
  if (!resolveVideo(root_, path, file, error)) return false;
  decoder_ = VideoDecoder::open(file, error);
  if (!decoder_) return false;
  const auto first = decoder_->next(current_, error);
  if (first == VideoRead::End) {
    error = "video: no frames";
    decoder_.reset();
    return false;
  }
  if (first != VideoRead::Frame) {
    decoder_.reset();
    return false;
  }
  const auto status = decoder_->next(next_, error);
  if (status == VideoRead::Error) {
    decoder_.reset();
    return false;
  }
  if (status != VideoRead::Frame || next_.seconds <= current_.seconds) {
    error = "video: single-frame video unsupported";
    decoder_.reset();
    return false;
  }
  frameDuration_ = next_.seconds - current_.seconds;
  haveNext_ = true;
  dirty_ = true;
  loop_ = loop;
  origin_ = now - current_.seconds;
  if (decoder_->hasAudio() && audio_) {
    if (!audio_->clear(error) || !audio_->resume(error)) {
      decoder_.reset();
      return false;
    }
    runlog::event(runlog::EventKind::Note, "video audio open src=%dHz dst=%dHz stereo",
                  decoder_->audioSampleRate(), 48000);
  }
  runlog::event(runlog::EventKind::Note, "video open %s loop=%d", path.c_str(), loop ? 1 : 0);
  error.clear();
  return true;
}
bool VideoPlayer::draw(double now, std::string& error) {
  if (!decoder_ || !std::isfinite(now) || now < origin_) {
    error = "video: unavailable or invalid clock";
    return false;
  }
  while (haveNext_ && origin_ + next_.seconds <= now) {
    frameDuration_ = next_.seconds - current_.seconds;
    current_ = std::move(next_);
    dirty_ = true;
    const auto status = decoder_->next(next_, error);
    if (status == VideoRead::Error) return false;
    haveNext_ = status == VideoRead::Frame;
    if (haveNext_ && next_.seconds <= current_.seconds) {
      error = "video: non-increasing timestamp";
      return false;
    }
  }
  // Queue decoded audio in sync with the video clock. When the device queue is
  // full, skip this round instead of draining: a full queue is backpressure,
  // not an error, and the video path must keep playing.
  if (audio_ && decoder_->hasAudio()) {
    size_t queued = 0;
    if (!audio_->queuedSamples(queued, error)) return false;
    if (queued < AudioOutput::maxQueuedSamples) {
      std::vector<VideoAudio> chunks;
      decoder_->drainAudio(now - origin_, AudioOutput::maxQueuedSamples - queued, chunks);
      for (const auto& chunk : chunks)
        if (!audio_->queue(chunk.pcm.data(), chunk.pcm.size(), error)) return false;
    }
  }
  if (!haveNext_ && now >= origin_ + current_.seconds + frameDuration_) {
    if (!loop_) {
      // Stock's DirectShow loop ends on the graph end-of-stream event, which
      // covers the audio renderer too: queue the remaining audio tail and let
      // the device drain it rather than cutting it at the last video frame.
      const bool tail = audio_ && decoder_->hasAudio();
      if (tail) {
        size_t queued = 0;
        if (!audio_->queuedSamples(queued, error)) return false;
        if (queued < AudioOutput::maxQueuedSamples) {
          std::vector<VideoAudio> chunks;
          decoder_->drainAudio(std::numeric_limits<double>::infinity(),
                               AudioOutput::maxQueuedSamples - queued, chunks);
          for (const auto& chunk : chunks)
            if (!audio_->queue(chunk.pcm.data(), chunk.pcm.size(), error)) return false;
        }
      }
      closeVideo();
      audioTail_ = tail;
      if (tail) runlog::event(runlog::EventKind::Note, "video audio tail queued");
      error.clear();
      return true;
    }
    const double duration = current_.seconds + frameDuration_;
    if (!std::isfinite(duration) || duration <= 0) {
      error = "video: invalid loop duration";
      return false;
    }
    if (audio_) {
      std::string ignored;
      audio_->clear(ignored);
    }
    const double nextOrigin = origin_ + std::max(1.0, std::floor((now - origin_) / duration)) * duration;
    if (!decoder_->rewind(error) || decoder_->next(current_, error) != VideoRead::Frame ||
        decoder_->next(next_, error) != VideoRead::Frame) return false;
    dirty_ = true;
    runlog::event(runlog::EventKind::Note, "video loop %.3fs", duration);
    origin_ = nextOrigin - current_.seconds;
    haveNext_ = true;
    return draw(now, error);
  }
  if (!texture_) {
    texture_ = renderer_.texture(static_cast<uint32_t>(current_.width), static_cast<uint32_t>(current_.height),
                                 current_.rgba.data(), current_.rgba.size(), error);
    if (!texture_) return false;
    dirty_ = false;
  } else if (dirty_) {
    if (!renderer_.updateTexture(*texture_, static_cast<uint32_t>(current_.width), static_cast<uint32_t>(current_.height),
                                 current_.rgba.data(), current_.rgba.size(), error)) return false;
    dirty_ = false;
  }
  int width, height;
  if (!renderer_.outputSize(width, height, error)) return false;
  const float scale = std::min(static_cast<float>(width) / static_cast<float>(current_.width), static_cast<float>(height) / static_cast<float>(current_.height));
  const float w = static_cast<float>(current_.width) * scale, h = static_cast<float>(current_.height) * scale;
  return renderer_.draw(*texture_, {(static_cast<float>(width) - w) * 0.5f, (static_cast<float>(height) - h) * 0.5f, w, h}, error);
}
