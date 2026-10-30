#include "music.hpp"

#include "audio.hpp"

#include <AudioToolbox/ExtendedAudioFile.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <system_error>

namespace {

constexpr float kMuteThreshold = 0.01f;  // 0x005f0c20 holds 0x3c23d70a

constexpr int32_t kAttenuationTable[65] = {
	-10000, -3610, -3010, -2660, -2410, -2210, -2060, -1920, -1810, -1700, -1610, -1530, -1450, -1380,
	-1320, -1260, -1200, -1150, -1100, -1050, -1010, -970, -930, -890, -850, -820, -780, -750, -720,
	-690, -660, -630, -600, -580, -550, -520, -500, -480, -450, -430, -410, -390, -370, -350, -330,
	-310, -290, -270, -250, -230, -210, -200, -180, -160, -150, -130, -120, -100, -90, -70, -60, -40,
	-30, -10, 0};

const char* const kSetFolders[MusicPlayer::setCount] = {"Garage_Shop", "Roam_Ride", "Race_Chase", "Main_Menu"};

std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

bool playableExtension(const std::filesystem::path& file) {
	const std::string ext = lower(file.extension().string());
	return ext == ".wav" || ext == ".mp3";
}

bool findNoCase(const std::filesystem::path& dir, const std::string& name, std::filesystem::path& found, std::string& error) {
	std::error_code ec;
	const std::string want = lower(name);
	for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		if (lower(it->path().filename().string()) == want) { found = it->path(); return true; }
	if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory) {
		error = dir.string() + ": " + ec.message();
		found.clear();
		return false;
	}
	found.clear();
	return true;
}

// Mirrors the recursive scan at 0x00550c60: every file counts regardless of extension,
// subdirectories are scanned where they occur. Order is case-insensitive by name.
bool scanFolder(const std::filesystem::path& dir, std::vector<std::filesystem::path>& out, std::string& error) {
	std::error_code ec;
	std::vector<std::filesystem::directory_entry> entries;
	for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) entries.push_back(*it);
	if (ec) { error = dir.string() + ": " + ec.message(); return false; }
	std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
		return lower(a.path().filename().string()) < lower(b.path().filename().string());
	});
	for (const auto& entry : entries) {
		const bool isDir = entry.is_directory(ec);
		if (ec) { error = entry.path().string() + ": " + ec.message(); return false; }
		if (isDir) {
			if (!scanFolder(entry.path(), out, error)) return false;
		} else {
			out.push_back(entry.path());
		}
	}
	return true;
}

}  // namespace

int32_t musicAttenuationCentiDb(float volume) {
	const double scaled = static_cast<double>(volume) * 64.0;
	if (!std::isfinite(scaled) || scaled >= 65.0 || scaled <= -1.0) return 0;
	return kAttenuationTable[static_cast<int>(scaled)];
}

float musicAttenuationToGain(int32_t centiDb) {
	if (centiDb <= -10000) return 0.0f;
	if (centiDb >= 0) return 1.0f;
	return static_cast<float>(std::pow(10.0, static_cast<double>(centiDb) / 2000.0));
}

bool NullMusicBackend::play(const std::filesystem::path& file, bool loop, std::string& error) {
	if (failPlay) { error = "null backend: play refused"; return false; }
	events.push_back({Event::Play, file, loop, 0});
	playing_ = true;
	finished_ = false;
	error.clear();
	return true;
}

void NullMusicBackend::stop() {
	events.push_back({Event::Stop, {}, false, 0});
	playing_ = false;
	finished_ = false;
}

void NullMusicBackend::setAttenuationCentiDb(int32_t centiDb) { events.push_back({Event::Attenuation, {}, false, centiDb}); }

bool NullMusicBackend::takeTrackFinished() {
	const bool f = finished_ && playing_;
	finished_ = false;
	return f;
}

namespace {

class AudioToolboxBackend final : public MusicBackend {
public:
	explicit AudioToolboxBackend(AudioOutput& output) : output_(output) {}
	~AudioToolboxBackend() override { closeFile(); }
	const char* name() const override { return "AudioToolbox decode to SDL audio stream"; }

	bool play(const std::filesystem::path& path, bool loop, std::string& error) override {
		stop();
		const std::string name = path.string();
		CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(name.data()),
		                                                       static_cast<CFIndex>(name.size()), false);
		if (!url) { error = "cannot create music file URL"; return false; }
		OSStatus status = ExtAudioFileOpenURL(url, &file_);
		CFRelease(url);
		if (status != noErr) { file_ = nullptr; error = "cannot open music file: " + name; return false; }
		AudioStreamBasicDescription format{};
		format.mSampleRate = 48000;
		format.mFormatID = kAudioFormatLinearPCM;
		format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
		format.mBytesPerPacket = format.mBytesPerFrame = 2 * sizeof(float);
		format.mFramesPerPacket = 1;
		format.mChannelsPerFrame = 2;
		format.mBitsPerChannel = 32;
		status = ExtAudioFileSetProperty(file_, kExtAudioFileProperty_ClientDataFormat, sizeof(format), &format);
		if (status != noErr) { closeFile(); error = "cannot decode music file: " + name; return false; }
		loop_ = loop;
		eof_ = false;
		finished_ = false;
		if (!output_.clear(error) || !output_.resume(error)) { closeFile(); return false; }
		return true;
	}

	void stop() override {
		closeFile();
		finished_ = false;
		std::string ignored;
		output_.clear(ignored);
		output_.pause(ignored);
	}

	void setAttenuationCentiDb(int32_t centiDb) override { gain_ = musicAttenuationToGain(centiDb); }

	bool takeTrackFinished() override {
		const bool f = finished_;
		finished_ = false;
		return f;
	}

	bool pump(std::string& error) override {
		error.clear();
		for (int chunk = 0; chunk < 8 && (file_ || eof_); ++chunk) {
			size_t queued = 0;
			if (!output_.queuedSamples(queued, error)) return false;
			if (eof_) {
				if (queued == 0) { closeFile(); finished_ = true; }
				return true;
			}
			if (queued > AudioOutput::maxQueuedSamples - 8192) return true;
			float samples[8192];
			AudioBufferList buffer{};
			buffer.mNumberBuffers = 1;
			buffer.mBuffers[0].mNumberChannels = 2;
			buffer.mBuffers[0].mDataByteSize = sizeof(samples);
			buffer.mBuffers[0].mData = samples;
			UInt32 frames = 4096;
			if (ExtAudioFileRead(file_, &frames, &buffer) != noErr) { error = "music decode failed"; closeFile(); return false; }
			if (frames == 0) {
				if (loop_) {
					if (ExtAudioFileSeek(file_, 0) != noErr) { error = "music loop seek failed"; closeFile(); return false; }
					continue;
				}
				eof_ = true;
				continue;
			}
			const size_t count = static_cast<size_t>(frames) * 2;
			for (size_t i = 0; i < count; ++i) samples[i] *= gain_;
			if (!output_.queue(samples, count, error)) return false;
		}
		return true;
	}

private:
	void closeFile() {
		if (file_) ExtAudioFileDispose(file_);
		file_ = nullptr;
		eof_ = false;
	}
	AudioOutput& output_;
	ExtAudioFileRef file_ = nullptr;
	bool loop_ = false;
	bool eof_ = false;
	bool finished_ = false;
	float gain_ = 1.0f;
};

}  // namespace

std::unique_ptr<MusicBackend> createAudioToolboxMusicBackend(AudioOutput& output) {
	return std::make_unique<AudioToolboxBackend>(output);
}

MusicPlayer::TickSource MusicPlayer::steadyClockTicks() {
	return [] {
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
		return static_cast<uint32_t>(ms.count());
	};
}

std::unique_ptr<MusicPlayer> MusicPlayer::create(const std::filesystem::path& root, MusicBackend& backend, TickSource ticks,
                                                 std::string& error) {
	std::array<std::vector<std::filesystem::path>, setCount> playlists;
	std::filesystem::path musicDir;
	if (!findNoCase(root, "Music", musicDir, error)) return nullptr;
	for (size_t i = 0; i < playlists.size() && !musicDir.empty(); ++i) {
		std::filesystem::path folder;
		if (!findNoCase(musicDir, kSetFolders[i], folder, error)) return nullptr;
		if (folder.empty()) continue;
		std::error_code ec;
		if (!std::filesystem::is_directory(folder, ec)) continue;
		if (!scanFolder(folder, playlists[i], error)) return nullptr;
	}
	error.clear();
	return createWithPlaylists(std::move(playlists), backend, std::move(ticks));
}

std::unique_ptr<MusicPlayer> MusicPlayer::createWithPlaylists(std::array<std::vector<std::filesystem::path>, setCount> playlists,
                                                              MusicBackend& backend, TickSource ticks) {
	if (!ticks) ticks = steadyClockTicks();
	auto player = std::unique_ptr<MusicPlayer>(new MusicPlayer(backend, std::move(ticks)));
	player->initialize(std::move(playlists));
	return player;
}

// Constructor 0x00550be0: volume 1.0, mode 3, first track GetTickCount() % count.
void MusicPlayer::initialize(std::array<std::vector<std::filesystem::path>, setCount> playlists) {
	for (size_t i = 0; i < lists_.size(); ++i) {
		lists_[i].files = std::move(playlists[i]);
		lists_[i].volume = 1.0f;
		lists_[i].mode = MusicMode::Random;
		lists_[i].index = lists_[i].files.empty() ? 0 : static_cast<int32_t>(ticks_() % lists_[i].files.size());
	}
}

void MusicPlayer::stopPlayback() {
	backend_.stop();
	playing_ = false;
}

// 0x00550e50. Does nothing (and leaves the index alone) when the list is empty or the player volume is below 0.01.
void MusicPlayer::startTrack(Playlist& p) {
	const int32_t count = static_cast<int32_t>(p.files.size());
	if (count < 1 || p.volume < kMuteThreshold) return;
	if (p.index < 0) p.index = count - 1;
	if (p.index >= count) p.index = 0;
	const auto& file = p.files[static_cast<size_t>(p.index)];
	lastPlayError_.clear();
	if (!playableExtension(file)) {
		lastPlayError_ = "unsupported music file type: " + file.string();
		playing_ = false;
		return;
	}
	std::string error;
	if (!backend_.play(file, p.mode == MusicMode::RepeatTrack, error)) {
		lastPlayError_ = error;
		playing_ = false;
		return;
	}
	playing_ = true;
	backend_.setAttenuationCentiDb(musicAttenuationCentiDb(p.volume));
}

// 0x00550fe0. Below 0.01 the stored volume becomes 0 and playback stops. Coming back above it restarts the track.
void MusicPlayer::applyVolume(Playlist& p, float v) {
	const float old = p.volume;
	p.volume = v;
	if (v < kMuteThreshold) {
		p.volume = 0.0f;
		stopPlayback();
		return;
	}
	if (old < kMuteThreshold) startTrack(p);
	if (playing_) backend_.setAttenuationCentiDb(musicAttenuationCentiDb(p.volume));
}

// 0x00551050, run when a track ends while the set's mode is not RepeatTrack.
void MusicPlayer::trackEnded(Playlist& p) {
	stopPlayback();
	const int32_t count = static_cast<int32_t>(p.files.size());
	switch (p.mode) {
	case MusicMode::RepeatTrack: break;
	case MusicMode::NextTrack: p.index += 1; break;
	case MusicMode::PreviousTrack: p.index -= 1; break;
	case MusicMode::Random:
		if (count > 1) {
			const int32_t before = p.index;
			int32_t pick = before;
			// Stock spins until the clock gives a different index. A frozen clock would hang, so give up after 1000 tries.
			for (int tries = 0; pick == before && tries < 1000; ++tries) {
				pick = static_cast<int32_t>(ticks_() % static_cast<uint32_t>(count));
				if (pick >= count) pick = 0;
			}
			if (pick == before) pick = (before + 1) % count;
			p.index = pick;
		}
		break;
	}
	startTrack(p);
}

void MusicPlayer::changeMusicSet(int32_t set, float musicChannelVolume) {
	Playlist* target = (set >= 0 && set < setCount) ? &lists_[static_cast<size_t>(set)] : nullptr;
	Playlist* cur = current();
	if (target == cur) return;
	if (cur) stopPlayback();
	selected_ = target ? set : -1;
	if (target) {
		startTrack(*target);
		applyVolume(*target, musicChannelVolume);
	}
}

void MusicPlayer::nextTrack() {
	Playlist* p = current();
	if (!p) return;
	stopPlayback();
	p->index += 1;
	startTrack(*p);
}

void MusicPlayer::prevTrack() {
	Playlist* p = current();
	if (!p) return;
	stopPlayback();
	p->index -= 1;
	startTrack(*p);
}

int32_t MusicPlayer::setMode(int32_t mode) {
	Playlist* p = current();
	if (!p) return mode;
	if (mode >= 0 && mode < 4) p->mode = static_cast<MusicMode>(mode);
	return static_cast<int32_t>(p->mode);
}

int32_t MusicPlayer::getMode() const {
	const Playlist* p = current();
	return p ? static_cast<int32_t>(p->mode) : -1;
}

void MusicPlayer::setMusicVolume(float volume) {
	if (Playlist* p = current()) applyVolume(*p, volume);
}

bool MusicPlayer::update(std::string& error) {
	if (!backend_.pump(error)) {
		lastPlayError_ = error;
		stopPlayback();
		return false;
	}
	if (backend_.takeTrackFinished()) {
		Playlist* p = current();
		if (p && playing_) {
			if (p->mode == MusicMode::RepeatTrack) playing_ = false;
			else trackEnded(*p);
		}
	}
	error.clear();
	return true;
}

size_t MusicPlayer::trackCount(int set) const {
	return (set >= 0 && set < setCount) ? lists_[static_cast<size_t>(set)].files.size() : 0;
}

int32_t MusicPlayer::trackIndex(int set) const {
	return (set >= 0 && set < setCount) ? lists_[static_cast<size_t>(set)].index : -1;
}

float MusicPlayer::setVolume(int set) const {
	return (set >= 0 && set < setCount) ? lists_[static_cast<size_t>(set)].volume : 0.0f;
}

const std::filesystem::path* MusicPlayer::currentTrack() const {
	const Playlist* p = current();
	if (!p || !playing_ || p->index < 0 || static_cast<size_t>(p->index) >= p->files.size()) return nullptr;
	return &p->files[static_cast<size_t>(p->index)];
}
