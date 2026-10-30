#include "video.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <limits>

namespace {
std::string avError(int code) {
	char buffer[AV_ERROR_MAX_STRING_SIZE]{};
	av_strerror(code, buffer, sizeof buffer);
	return buffer;
}
}
struct VideoDecoder::Impl {
	AVFormatContext* format = nullptr;
	AVCodecContext* codec = nullptr;
	AVFrame* decoded = nullptr;
	AVPacket* packet = nullptr;
	SwsContext* scaler = nullptr;
	int stream = -1;
	bool eof = false;
	~Impl() {
		sws_freeContext(scaler);
		av_packet_free(&packet);
		av_frame_free(&decoded);
		avcodec_free_context(&codec);
		avformat_close_input(&format);
	}
};
VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() = default;
std::unique_ptr<VideoDecoder> VideoDecoder::open(const std::filesystem::path& file, std::string& error) {
	auto result = std::unique_ptr<VideoDecoder>(new VideoDecoder());
	auto& v = *result->impl_;
	int rc = avformat_open_input(&v.format, file.string().c_str(), nullptr, nullptr);
	if (rc < 0) { error = "video open: " + avError(rc); return {}; }
	rc = avformat_find_stream_info(v.format, nullptr);
	if (rc < 0) { error = "video stream info: " + avError(rc); return {}; }
	for (unsigned i = 0; i < v.format->nb_streams; ++i)
		if (v.format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			error = "video: audio stream is unsupported"; return {};
		}
	const AVCodec* decoder = nullptr;
	v.stream = av_find_best_stream(v.format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
	if (v.stream < 0 || !decoder) { error = "video stream unavailable"; return {}; }
	v.codec = avcodec_alloc_context3(decoder);
	if (!v.codec) { error = "video codec allocation failed"; return {}; }
	rc = avcodec_parameters_to_context(v.codec, v.format->streams[v.stream]->codecpar);
	if (rc >= 0) rc = avcodec_open2(v.codec, decoder, nullptr);
	if (rc < 0) { error = "video codec: " + avError(rc); return {}; }
	v.decoded = av_frame_alloc(); v.packet = av_packet_alloc();
	if (!v.decoded || !v.packet) { error = "video frame allocation failed"; return {}; }
	error.clear(); return result;
}
VideoRead VideoDecoder::next(VideoFrame& frame, std::string& error) {
	auto& v = *impl_;
	for (;;) {
		int rc = avcodec_receive_frame(v.codec, v.decoded);
		if (rc == 0) {
			const int w = v.decoded->width, h = v.decoded->height;
			if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || v.decoded->best_effort_timestamp == AV_NOPTS_VALUE) {
				error = "video: unsupported dimensions or timestamp"; av_frame_unref(v.decoded); return VideoRead::Error;
			}
			v.scaler = sws_getCachedContext(v.scaler, w, h, static_cast<AVPixelFormat>(v.decoded->format),
			                                w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
			if (!v.scaler) { error = "video: pixel conversion unavailable"; av_frame_unref(v.decoded); return VideoRead::Error; }
			VideoFrame out; out.width = w; out.height = h;
			out.seconds = v.decoded->best_effort_timestamp * av_q2d(v.format->streams[v.stream]->time_base);
			out.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
			uint8_t* dst[] = {out.rgba.data(), nullptr, nullptr, nullptr};
			int stride[] = {w * 4, 0, 0, 0};
			const int lines = sws_scale(v.scaler, v.decoded->data, v.decoded->linesize, 0, h, dst, stride);
			av_frame_unref(v.decoded);
			if (lines != h) { error = "video: incomplete pixel conversion"; return VideoRead::Error; }
			frame = std::move(out); error.clear(); return VideoRead::Frame;
		}
		if (rc == AVERROR_EOF) { error.clear(); return VideoRead::End; }
		if (rc != AVERROR(EAGAIN)) { error = "video decode: " + avError(rc); return VideoRead::Error; }
		if (v.eof) { error = "video: decoder did not finish"; return VideoRead::Error; }
		do {
			rc = av_read_frame(v.format, v.packet);
			if (rc < 0) {
				if (rc != AVERROR_EOF) { error = "video read: " + avError(rc); return VideoRead::Error; }
				v.eof = true; rc = avcodec_send_packet(v.codec, nullptr); break;
			}
			if (v.packet->stream_index == v.stream) {
				rc = avcodec_send_packet(v.codec, v.packet); av_packet_unref(v.packet); break;
			}
			av_packet_unref(v.packet);
		} while (true);
		if (rc < 0 && rc != AVERROR_EOF) { error = "video packet: " + avError(rc); return VideoRead::Error; }
	}
}
bool VideoDecoder::rewind(std::string& error) {
	auto& v = *impl_;
	const int rc = av_seek_frame(v.format, v.stream, 0, AVSEEK_FLAG_BACKWARD);
	if (rc < 0) { error = "video seek: " + avError(rc); return false; }
	avcodec_flush_buffers(v.codec); v.eof = false; error.clear(); return true;
}
