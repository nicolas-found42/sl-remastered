#pragma once

#include "music.hpp"
#include "vm.hpp"

// Lets the Sound natives drive a MusicPlayer without the VM library depending on the audio library.
class MusicPlayerControl final : public VmMusicControl {
public:
	explicit MusicPlayerControl(MusicPlayer& player) : player_(player) {}
	void changeMusicSet(int32_t set, float volume) override { player_.changeMusicSet(set, volume); }
	void nextTrack() override { player_.nextTrack(); }
	void prevTrack() override { player_.prevTrack(); }
	int32_t setMode(int32_t mode) override { return player_.setMode(mode); }
	int32_t getMode() const override { return player_.getMode(); }
	void setMusicVolume(float volume) override { player_.setMusicVolume(volume); }
private:
	MusicPlayer& player_;
};
