#pragma once

#include <cstdint>
#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <mutex>
#include <set>
#include <string>
#include <variant>
#include <vector>
#include <filesystem>

struct SDL_Window;
struct SDL_Cursor;

class PlatformInput {
public:
	// Reports stock system aliases only for devices SDL actually detects.
	static std::unique_ptr<PlatformInput> create(std::string& error);
	~PlatformInput();
	PlatformInput(const PlatformInput&) = delete;
	PlatformInput& operator=(const PlatformInput&) = delete;
	const std::vector<std::string>& names() const { return names_; }
	std::optional<float> sample(int32_t device, int32_t axis) const;
	std::optional<int32_t> type(int32_t device, int32_t axis) const;
	// Called once per input frame on the main thread. SDL events are watched,
	// not removed from the application's event queue.
	void beginFrame();
	void captureMouse(float x, float y, float wheel);
	void captureKey(uint32_t scancode, uint32_t keycode, uint16_t modifiers, bool down, bool repeat);
	std::optional<uint32_t> nextKey();
	// Scripted input for automated runs. Folded into sample() beside the real SDL state.
	void setSyntheticMouseButton(uint8_t sdlButton, bool down);
	void setSyntheticKey(uint32_t scancode, bool down);
	// Uses the actual embedded stock cursor image; only cursor index zero is
	// decoded so far. Restores the previous SDL cursor on teardown.
	bool selectStockCursor(const std::filesystem::path& exe, int32_t index,
	                       bool visible, std::string& error);
private:
	enum class Kind { Keyboard, Mouse, Joystick };
	PlatformInput() = default;
	std::vector<std::string> names_;
	std::vector<Kind> kinds_;
	struct KeyEvent { uint32_t scancode, keycode; uint16_t modifiers; bool down; };
	mutable std::mutex keyMutex_;
	std::deque<KeyEvent> keyEvents_;
	bool keyOverflow_ = false;
	mutable std::mutex mouseMutex_;
	mutable std::mutex syntheticMutex_;
	uint32_t syntheticButtons_ = 0;
	std::set<uint32_t> syntheticKeys_;
	float pendingMouse_[3]{};
	float frameMouse_[3]{};
	float frameMilliseconds_ = 0.0f;
	uint64_t lastFrame_ = 0;
	bool watching_ = false;
	SDL_Cursor* selectedCursor_ = nullptr;
	SDL_Cursor* previousCursor_ = nullptr;
	bool previousCursorVisible_ = false;
	bool changedCursor_ = false;
};

struct PlatformQuit {};
struct PlatformKey {
	uint32_t scancode;
	bool down;
	bool repeat;
};
struct PlatformPixelSize {
	int width;
	int height;
};
// Mouse coordinates are in window coordinates, not physical pixels.
struct PlatformMouseMotion {
	float x;
	float y;
	float dx;
	float dy;
};
struct PlatformMouseButton {
	uint8_t button;
	bool down;
	float x;
	float y;
};
using PlatformEvent = std::variant<PlatformQuit, PlatformKey, PlatformPixelSize, PlatformMouseMotion, PlatformMouseButton>;

class Platform {
public:
	static std::unique_ptr<Platform> create(const char* title, int width, int height, std::string& error);
	~Platform();
	Platform(const Platform&) = delete;
	Platform& operator=(const Platform&) = delete;

	std::vector<PlatformEvent> poll();
	bool pixelSize(PlatformPixelSize& size, std::string& error) const;
	SDL_Window* window() const { return window_; }

private:
	explicit Platform(SDL_Window* window) : window_(window) {}
	SDL_Window* window_;
};

using PlatformDisplayMode = std::array<int32_t, 3>;
std::vector<PlatformDisplayMode> uniqueDisplayModes(const std::vector<PlatformDisplayMode>& modes);
std::optional<std::vector<PlatformDisplayMode>> platformDisplayModes(std::string& error);

std::optional<std::array<float, 2>> normalizeCursorPosition(float x, float y, int width, int height);
