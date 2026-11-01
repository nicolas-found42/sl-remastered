#include "video.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#define CHECK(x)                                                \
  do {                                                          \
    if (!(x)) {                                                 \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
      std::abort();                                             \
    }                                                           \
  } while (0)

int main() {
  std::string error;
  auto video = VideoDecoder::open("ref/raw/Data/Fmv/Prime.avi", error);
  CHECK(video && error.empty());
  VideoFrame first, next, again;
  CHECK(video->next(first, error) == VideoRead::Frame);
  CHECK(first.width == 160 && first.height == 120 && first.rgba.size() == 76800);
  CHECK(std::abs(first.seconds) < 0.00001);
  const size_t center = (60 * 160 + 80) * 4;
  CHECK(first.rgba[center] == 17 && first.rgba[center + 1] == 0 && first.rgba[center + 2] == 0 && first.rgba[center + 3] == 255);
  CHECK(video->next(next, error) == VideoRead::Frame && next.seconds > first.seconds);
  int count = 2;
  double previous = next.seconds;
  for (;;) {
    const auto status = video->next(next, error);
    CHECK(status != VideoRead::Error);
    if (status == VideoRead::End) break;
    CHECK(next.seconds > previous);
    previous = next.seconds;
    ++count;
  }
  CHECK(count == 834);
  CHECK(video->rewind(error));
  CHECK(video->next(again, error) == VideoRead::Frame && again.seconds == first.seconds && again.rgba == first.rgba);
  CHECK(!video->hasAudio()); // Prime.avi carries no audio stream
  CHECK(!VideoDecoder::open("ref/raw/Data/Fmv/not-present.avi", error) && !error.empty());
  // Stock startup (FUN_0058c700) plays Activision.avi and Invictus.avi,
  // which carry a PCM audio stream beside the Cinepak video. Stock DirectShow
  // plays them; the decoder must not reject the file for its audio stream.
  for (const auto& intro : {"ref/raw/Data/Fmv/Activision.avi", "ref/raw/Data/Fmv/Invictus.avi"}) {
    auto startup = VideoDecoder::open(intro, error);
    CHECK(startup && error.empty());
    int frames = 0;
    double previous = -1.0;
    for (;;) {
      const auto status = startup->next(next, error);
      CHECK(status != VideoRead::Error);
      if (status == VideoRead::End) break;
      CHECK(next.seconds > previous);
      previous = next.seconds;
      ++frames;
    }
    CHECK(frames == (std::string_view{intro}.find("Activision") != std::string_view::npos ? 304 : 373));
    // Stock DirectShow renders the PCM audio beside the video; the decoder
    // must resample it to the device format (48 kHz stereo float).
    CHECK(startup->hasAudio());
    CHECK(startup->audioSampleRate() == 44100);
    std::vector<VideoAudio> chunks;
    startup->drainAudio(1.0e9, 0, chunks);
    CHECK(chunks.size() > 10);
    double lastChunk = -1.0, peak = 0.0;
    size_t samples = 0;
    for (const auto& chunk : chunks) {
      CHECK(chunk.seconds > lastChunk);
      CHECK(!chunk.pcm.empty() && chunk.pcm.size() % 2 == 0);
      lastChunk = chunk.seconds;
      samples += chunk.pcm.size();
      for (const float s : chunk.pcm) peak = std::max(peak, static_cast<double>(std::abs(s)));
    }
    const double expected = std::string_view{intro}.find("Activision") != std::string_view::npos ? 10.143467 : 14.92;
    const double got = static_cast<double>(samples) / 2.0 / 48000.0;
    CHECK(got > expected - 0.5 && got < expected + 0.5);
    CHECK(peak > 0.01);
    CHECK(startup->rewind(error));
    std::vector<VideoAudio> stale;
    startup->drainAudio(1.0e9, 0, stale);
    CHECK(stale.empty());
    CHECK(startup->next(again, error) == VideoRead::Frame);
  }
}
