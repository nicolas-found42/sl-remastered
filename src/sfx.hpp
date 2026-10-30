#pragma once
#include "resource_sound.hpp"
#include <cstdint>
#include <memory>
#include <string>

// Separate SDL streams share the default device, so effects mix with music.
class SoundEffects {
public:
	static std::unique_ptr<SoundEffects> create(std::string& error);
	~SoundEffects();
	SoundEffects(const SoundEffects&) = delete;
	SoundEffects& operator=(const SoundEffects&) = delete;
	// Started: a new voice. Updated: a named voice that was already playing got the new volume and pitch
	// (stock request state 3, FUN_0055c120). Dropped: stock discards the request without an error when the
	// voice table (64), the per-sound instance limit (FUN_0055b900, at most 8) or the device has no room
	// (FUN_00550980 state 1 to 0, FUN_005505e0 returning -1); nplay still returns the id.
	enum class Outcome { Started, Updated, Dropped };
	// Only non-positional, non-looping effects with positive pitch are supported. Returns false only for a
	// request the port cannot represent; `outcome` is set on success.
	bool play(uint32_t resourceId, int32_t id, const SoundSample& sample,
	          float volume, float pitch, std::string& error, Outcome* outcome = nullptr);
	void stop(uint32_t resourceId, int32_t id);
	size_t voiceCount() const;
private:
	SoundEffects();
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
