#pragma once

// Music state machine read from the stock executable (MD5 e686099e7233bb7d7c461b1b35c5064c).
// See docs/music-evidence.md for every address and for what is and is not verified.

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AudioOutput;

// Stock end-of-track behaviour per set (player field +0x10). Default is Random.
enum class MusicMode : int32_t { RepeatTrack = 0, NextTrack = 1, PreviousTrack = 2, Random = 3 };

// Where decoded sound goes. The player calls these in the same order the stock player calls its
// platform layer. play() may fail; the stock player ignores the failure and plays nothing.
class MusicBackend {
public:
	virtual ~MusicBackend() = default;
	virtual const char* name() const = 0;
	virtual bool play(const std::filesystem::path& file, bool loop, std::string& error) = 0;
	virtual void stop() = 0;
	// Stock maps volume to a DirectSound attenuation in hundredths of a decibel. See musicAttenuationCentiDb.
	virtual void setAttenuationCentiDb(int32_t centiDb) = 0;
	// True once per track that ran to its end while not looping. Polled by MusicPlayer::update.
	virtual bool takeTrackFinished() = 0;
	// Called by MusicPlayer::update to let a decoding backend feed its output.
	virtual bool pump(std::string& error) { error.clear(); return true; }
};

// Stock volume table at 0x0061b700 (65 entries) applied to ftol(volume * 64). An index outside 0..64
// (volume below 0 or at/above 65/64, NaN) selects 0 dB, i.e. full volume.
int32_t musicAttenuationCentiDb(float volume);
float musicAttenuationToGain(int32_t centiDb);

// Records what the player asked for and never makes a sound. This is the labelled
// "no audio source available" backend, and the backend the tests drive.
class NullMusicBackend final : public MusicBackend {
public:
	struct Event { enum Kind { Play, Stop, Attenuation } kind; std::filesystem::path file; bool loop = false; int32_t centiDb = 0; };
	const char* name() const override { return "null (no audio source available)"; }
	bool play(const std::filesystem::path& file, bool loop, std::string& error) override;
	void stop() override;
	void setAttenuationCentiDb(int32_t centiDb) override;
	bool takeTrackFinished() override;
	void finishTrack() { finished_ = true; }
	bool playing() const { return playing_; }
	bool failPlay = false;
	std::vector<Event> events;
private:
	bool playing_ = false;
	bool finished_ = false;
};

// Decodes .mp3 and .wav with macOS AudioToolbox and queues stereo float PCM into an AudioOutput.
// The output must outlive the backend. Volume applies to samples queued after the change.
std::unique_ptr<MusicBackend> createAudioToolboxMusicBackend(AudioOutput& output);

// One playlist per music set. Set numbers follow Sound.MUSIC_SET_*: 0 Garage_Shop, 1 Roam_Ride,
// 2 Race_Chase, 3 Main_Menu. Any other number means "no set".
class MusicPlayer {
public:
	static constexpr int setCount = 4;
	using TickSource = std::function<uint32_t()>;  // stands in for GetTickCount

	// Scans <dataRoot>/Music/<set folder> for every set, matching names without regard to case.
	// A missing folder gives an empty playlist, as in stock. Fails only if a directory cannot be read.
	static std::unique_ptr<MusicPlayer> create(const std::filesystem::path& dataRoot, MusicBackend& backend,
	                                           TickSource ticks, std::string& error);
	// Same, with explicit playlists (the stock scan order is the order given).
	static std::unique_ptr<MusicPlayer> createWithPlaylists(std::array<std::vector<std::filesystem::path>, setCount> playlists,
	                                                        MusicBackend& backend, TickSource ticks);
	static TickSource steadyClockTicks();

	// 0x00487460 Sound.changeMusicSet(I)V. musicChannelVolume is the Sound.setVolume channel 1 global (0x00612c5c).
	void changeMusicSet(int32_t set, float musicChannelVolume);
	// 0x00487500 / 0x00487510 Sound.nextTrack / prevTrack. No-ops without a set.
	void nextTrack();
	void prevTrack();
	// 0x00487520 Sound.setMode(I)I. Returns the set's mode, or the argument when no set is selected.
	int32_t setMode(int32_t mode);
	// 0x00487560 Sound.getMode()I. -1 when no set is selected.
	int32_t getMode() const;
	// The music-channel part of 0x00487580 Sound.setVolume. Call it for channel 1 only when a set is selected;
	// without one it does nothing, as in stock.
	void setMusicVolume(float volume);
	// Drives end-of-track handling and backend decoding. Call regularly from the main loop.
	bool update(std::string& error);

	// Read-only state for tests and diagnostics.
	int32_t selectedSet() const { return selected_; }  // -1 when none
	size_t trackCount(int set) const;
	int32_t trackIndex(int set) const;
	float setVolume(int set) const;
	const std::filesystem::path* currentTrack() const;
	bool isPlaying() const { return playing_; }
	const std::string& lastPlayError() const { return lastPlayError_; }

private:
	struct Playlist {
		float volume = 1.0f;
		int32_t index = 0;
		MusicMode mode = MusicMode::Random;
		std::vector<std::filesystem::path> files;
	};
	MusicPlayer(MusicBackend& backend, TickSource ticks) : backend_(backend), ticks_(std::move(ticks)) {}
	void initialize(std::array<std::vector<std::filesystem::path>, setCount> playlists);
	void stopPlayback();                        // 0x0055af00
	void startTrack(Playlist& p);               // 0x00550e50
	void applyVolume(Playlist& p, float v);     // 0x00550fe0
	void trackEnded(Playlist& p);               // 0x00551050
	Playlist* current() { return selected_ < 0 ? nullptr : &lists_[static_cast<size_t>(selected_)]; }
	const Playlist* current() const { return selected_ < 0 ? nullptr : &lists_[static_cast<size_t>(selected_)]; }

	MusicBackend& backend_;
	TickSource ticks_;
	std::array<Playlist, setCount> lists_;
	int32_t selected_ = -1;
	bool playing_ = false;
	std::string lastPlayError_;
};
