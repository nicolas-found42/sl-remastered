#include "audio.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>

int main() {
  std::string temp = (std::filesystem::temp_directory_path() / "sl-private-audio-XXXXXX").string();
  if (!mkdtemp(temp.data())) return 1;
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::filesystem::remove_all(path); }
  } cleanup{temp};
  const auto path = std::filesystem::path(temp) / "mix.raw";
  std::string error;
  if (!AudioOutput::configurePrivateCapture(path.string(), error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  auto a = AudioOutput::create(error);
  auto b = AudioOutput::create(error);
  if (!a || !b || std::string(SDL_GetCurrentAudioDriver()) != "disk") return 1;
  std::printf("private output: %s\n", a->describeDevice().c_str());
  if (AudioOutput::configurePrivateCapture((path.parent_path() / "other.raw").string(), error)) return 1;
  const std::vector<float> left(96000, 0.125f), right(96000, 0.25f);
  if (!a->queue(left.data(), left.size(), error) || !b->queue(right.data(), right.size(), error) ||
      !a->resume(error) || !b->resume(error)) return 1;
  auto awaitConsumption = [&](AudioOutput& output, size_t below) {
    const auto deadline = SDL_GetTicks() + 3000;
    do {
      size_t queued = 0;
      if (!output.queuedSamples(queued, error)) return false;
      if (queued < below) return true;
      SDL_Delay(10);
    } while (SDL_GetTicks() < deadline);
    return false;
  };
  if (!awaitConsumption(*a, 80000) || !awaitConsumption(*b, 80000) || !a->pause(error)) return 1;
  size_t paused = 0, remaining = 0;
  if (!a->queuedSamples(paused, error) || !b->queuedSamples(remaining, error) || remaining < 16000 ||
      !awaitConsumption(*b, remaining - 12000)) return 1;
  size_t stillPaused = 0;
  if (!a->queuedSamples(stillPaused, error) || stillPaused != paused) return 1;
  a.reset();
  b.reset();
  std::ifstream stream(path, std::ios::binary);
  size_t mixed = 0, independent = 0, total = 0;
  for (float sample = 0; stream.read(reinterpret_cast<char*>(&sample), sizeof(sample));) {
    if (!std::isfinite(sample)) return 1;
    if (sample == 0.375f) ++mixed;
    if (sample == 0.25f) ++independent;
    ++total;
  }
  if (!stream.eof() || mixed < 1000 || independent < 1000) return 1;
  std::printf("private final mix: %zu samples, %zu mixed, %zu independently playing\n", total, mixed, independent);
  return 0;
}
