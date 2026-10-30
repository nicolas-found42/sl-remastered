#include "video.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

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
	CHECK(!VideoDecoder::open("ref/raw/Data/Fmv/not-present.avi", error) && !error.empty());
}
