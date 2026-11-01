#include "video.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <deque>

namespace {
std::string avError(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(code, buffer, sizeof buffer);
  return buffer;
}
constexpr int kOutSampleRate = 48000;
constexpr int kOutChannels = 2;
} // namespace
struct VideoDecoder::Impl {
  AVFormatContext* format = nullptr;
  AVCodecContext* codec = nullptr;
  AVFrame* decoded = nullptr;
  AVPacket* packet = nullptr;
  SwsContext* scaler = nullptr;
  int stream = -1;
  bool eof = false;
  // Audio side: PCM beside the video is decoded and resampled to the device
  // format (48 kHz stereo float) so playback can queue it in file order.
  AVCodecContext* audioCodec = nullptr;
  AVFrame* audioFrame = nullptr;
  SwrContext* resampler = nullptr;
  int audioStream = -1;
  int audioRate = 0;
  double nextAudioSeconds = 0;
  std::deque<VideoAudio> audioQueue;
  ~Impl() {
    swr_free(&resampler);
    sws_freeContext(scaler);
    av_packet_free(&packet);
    av_frame_free(&audioFrame);
    av_frame_free(&decoded);
    avcodec_free_context(&audioCodec);
    avcodec_free_context(&codec);
    avformat_close_input(&format);
  }
  // Sends one packet (nullptr flushes) to the audio decoder and queues every
  // resampled chunk. Returns false with `error` set on a hard failure.
  bool decodeAudio(const AVPacket* pkt, std::string& error) {
    int rc = avcodec_send_packet(audioCodec, pkt);
    if (rc < 0 && rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) {
      error = "video audio packet: " + avError(rc);
      return false;
    }
    for (;;) {
      rc = avcodec_receive_frame(audioCodec, audioFrame);
      if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) return true;
      if (rc < 0) {
        error = "video audio decode: " + avError(rc);
        return false;
      }
      double seconds;
      if (audioFrame->best_effort_timestamp != AV_NOPTS_VALUE)
        seconds = audioFrame->best_effort_timestamp * av_q2d(format->streams[audioStream]->time_base);
      else
        seconds = nextAudioSeconds;
      const int nbSamples = audioFrame->nb_samples;
      const int64_t delay = swr_get_delay(resampler, audioCodec->sample_rate);
      const int outCapacity = static_cast<int>(av_rescale_rnd(delay + nbSamples, kOutSampleRate, audioCodec->sample_rate, AV_ROUND_UP));
      if (outCapacity <= 0) {
        av_frame_unref(audioFrame);
        error = "video audio: invalid resample size";
        return false;
      }
      VideoAudio chunk;
      chunk.seconds = seconds;
      chunk.pcm.resize(static_cast<size_t>(outCapacity) * kOutChannels);
      uint8_t* out[kOutChannels] = {reinterpret_cast<uint8_t*>(chunk.pcm.data()),
                                    reinterpret_cast<uint8_t*>(chunk.pcm.data()) + outCapacity * sizeof(float)};
      const int converted = swr_convert(resampler, out, outCapacity,
                                        const_cast<const uint8_t**>(audioFrame->extended_data), nbSamples);
      av_frame_unref(audioFrame);
      if (converted < 0) {
        error = "video audio resample: " + avError(converted);
        return false;
      }
      chunk.pcm.resize(static_cast<size_t>(converted) * kOutChannels);
      nextAudioSeconds = seconds + static_cast<double>(nbSamples) / audioCodec->sample_rate;
      if (!chunk.pcm.empty()) audioQueue.push_back(std::move(chunk));
    }
  }
};
VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() = default;
std::unique_ptr<VideoDecoder> VideoDecoder::open(const std::filesystem::path& file, std::string& error) {
  auto result = std::unique_ptr<VideoDecoder>(new VideoDecoder());
  auto& v = *result->impl_;
  int rc = avformat_open_input(&v.format, file.string().c_str(), nullptr, nullptr);
  if (rc < 0) {
    error = "video open: " + avError(rc);
    return {};
  }
  rc = avformat_find_stream_info(v.format, nullptr);
  if (rc < 0) {
    error = "video stream info: " + avError(rc);
    return {};
  }
  // Stock DirectShow plays files with audio streams (the startup intros
  // FUN_0058c700 plays carry PCM beside Cinepak); the read loop below decodes
  // the audio stream alongside the video instead of rejecting the file.
  const AVCodec* decoder = nullptr;
  v.stream = av_find_best_stream(v.format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
  if (v.stream < 0 || !decoder) {
    error = "video stream unavailable";
    return {};
  }
  v.codec = avcodec_alloc_context3(decoder);
  if (!v.codec) {
    error = "video codec allocation failed";
    return {};
  }
  rc = avcodec_parameters_to_context(v.codec, v.format->streams[v.stream]->codecpar);
  if (rc >= 0) rc = avcodec_open2(v.codec, decoder, nullptr);
  if (rc < 0) {
    error = "video codec: " + avError(rc);
    return {};
  }
  v.decoded = av_frame_alloc();
  v.packet = av_packet_alloc();
  if (!v.decoded || !v.packet) {
    error = "video frame allocation failed";
    return {};
  }
  const AVCodec* audioDecoder = nullptr;
  v.audioStream = av_find_best_stream(v.format, AVMEDIA_TYPE_AUDIO, -1, -1, &audioDecoder, 0);
  if (v.audioStream >= 0 && audioDecoder) {
    const AVCodecParameters* audioPar = v.format->streams[v.audioStream]->codecpar;
    v.audioCodec = avcodec_alloc_context3(audioDecoder);
    if (!v.audioCodec) {
      error = "video audio codec allocation failed";
      return {};
    }
    rc = avcodec_parameters_to_context(v.audioCodec, audioPar);
    if (rc >= 0) rc = avcodec_open2(v.audioCodec, audioDecoder, nullptr);
    if (rc < 0) {
      error = "video audio codec: " + avError(rc);
      return {};
    }
    v.audioFrame = av_frame_alloc();
    if (!v.audioFrame) {
      error = "video audio frame allocation failed";
      return {};
    }
    v.audioRate = audioPar->sample_rate;
    AVChannelLayout inLayout = audioPar->ch_layout;
    if (inLayout.nb_channels == 0) av_channel_layout_default(&inLayout, 2);
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&v.resampler, &outLayout, AV_SAMPLE_FMT_FLT, kOutSampleRate, &inLayout,
                            static_cast<AVSampleFormat>(audioPar->format), audioPar->sample_rate, 0, nullptr) < 0 ||
        !v.resampler || swr_init(v.resampler) < 0) {
      error = "video audio resampler unavailable";
      return {};
    }
  }
  error.clear();
  return result;
}
VideoRead VideoDecoder::next(VideoFrame& frame, std::string& error) {
  auto& v = *impl_;
  for (;;) {
    int rc = avcodec_receive_frame(v.codec, v.decoded);
    if (rc == 0) {
      const int w = v.decoded->width, h = v.decoded->height;
      if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || v.decoded->best_effort_timestamp == AV_NOPTS_VALUE) {
        error = "video: unsupported dimensions or timestamp";
        av_frame_unref(v.decoded);
        return VideoRead::Error;
      }
      v.scaler = sws_getCachedContext(v.scaler, w, h, static_cast<AVPixelFormat>(v.decoded->format),
                                      w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
      if (!v.scaler) {
        error = "video: pixel conversion unavailable";
        av_frame_unref(v.decoded);
        return VideoRead::Error;
      }
      VideoFrame out;
      out.width = w;
      out.height = h;
      out.seconds = v.decoded->best_effort_timestamp * av_q2d(v.format->streams[v.stream]->time_base);
      out.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
      uint8_t* dst[] = {out.rgba.data(), nullptr, nullptr, nullptr};
      int stride[] = {w * 4, 0, 0, 0};
      const int lines = sws_scale(v.scaler, v.decoded->data, v.decoded->linesize, 0, h, dst, stride);
      av_frame_unref(v.decoded);
      if (lines != h) {
        error = "video: incomplete pixel conversion";
        return VideoRead::Error;
      }
      frame = std::move(out);
      error.clear();
      return VideoRead::Frame;
    }
    if (rc == AVERROR_EOF) {
      error.clear();
      return VideoRead::End;
    }
    if (rc != AVERROR(EAGAIN)) {
      error = "video decode: " + avError(rc);
      return VideoRead::Error;
    }
    if (v.eof) {
      error = "video: decoder did not finish";
      return VideoRead::Error;
    }
    do {
      rc = av_read_frame(v.format, v.packet);
      if (rc < 0) {
        if (rc != AVERROR_EOF) {
          error = "video read: " + avError(rc);
          return VideoRead::Error;
        }
        v.eof = true;
        rc = avcodec_send_packet(v.codec, nullptr);
        if (v.audioStream >= 0 && !v.decodeAudio(nullptr, error)) return VideoRead::Error;
        break;
      }
      if (v.packet->stream_index == v.stream) {
        rc = avcodec_send_packet(v.codec, v.packet);
        av_packet_unref(v.packet);
        break;
      }
      if (v.packet->stream_index == v.audioStream) {
        const bool ok = v.decodeAudio(v.packet, error);
        av_packet_unref(v.packet);
        if (!ok) return VideoRead::Error;
        continue;
      }
      av_packet_unref(v.packet);
    } while (true);
    if (rc < 0 && rc != AVERROR_EOF) {
      error = "video packet: " + avError(rc);
      return VideoRead::Error;
    }
  }
}
bool VideoDecoder::rewind(std::string& error) {
  auto& v = *impl_;
  const int rc = av_seek_frame(v.format, v.stream, 0, AVSEEK_FLAG_BACKWARD);
  if (rc < 0) {
    error = "video seek: " + avError(rc);
    return false;
  }
  avcodec_flush_buffers(v.codec);
  if (v.audioCodec) avcodec_flush_buffers(v.audioCodec);
  if (v.resampler) swr_init(v.resampler);
  v.audioQueue.clear();
  v.nextAudioSeconds = 0;
  v.eof = false;
  error.clear();
  return true;
}
bool VideoDecoder::hasAudio() const { return impl_->audioCodec != nullptr; }
int VideoDecoder::audioSampleRate() const { return impl_->audioRate; }
void VideoDecoder::drainAudio(double seconds, size_t maxSamples, std::vector<VideoAudio>& out) {
  auto& v = *impl_;
  size_t budget = maxSamples;
  while (!v.audioQueue.empty() && v.audioQueue.front().seconds <= seconds) {
    VideoAudio chunk = std::move(v.audioQueue.front());
    v.audioQueue.pop_front();
    if (maxSamples && chunk.pcm.size() > budget) {
      v.audioQueue.push_front(std::move(chunk));
      return;
    }
    if (maxSamples) budget -= chunk.pcm.size();
    out.push_back(std::move(chunk));
  }
}
