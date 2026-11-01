#include "platform.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <algorithm>

int main(int argc, char** argv) {
  const auto center = normalizeCursorPosition(320, 240, 640, 480);
  const auto clipped = normalizeCursorPosition(-10, 600, 640, 480);
  if (!center || *center != std::array<float, 2>{0, 0} || !clipped || *clipped != std::array<float, 2>{-1, 1} || normalizeCursorPosition(0, 0, 0, 480)) return 1;

  const auto modes = uniqueDisplayModes({{1920, 1080, 32}, {1920, 1080, 32}, {1920, 1080, 16}, {0, 1, 32}, {800, 600, 24}});
  if (modes.size() != 2 || modes[0] != PlatformDisplayMode{1920, 1080, 32} || modes[1] != PlatformDisplayMode{1920, 1080, 16}) return 1;

	const bool visible = argc == 2 && std::strcmp(argv[1], "--visible") == 0;
	if (!visible && !SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy")) return 1;
	std::string error;
	auto platform = Platform::create("SL platform smoke test", 640, 480, error);
	if (!platform) {
		std::fprintf(stderr, "window: %s\n", error.c_str());
		return 1;
	}
	PlatformPixelSize size{};
	if (!platform->pixelSize(size, error) || size.width <= 0 || size.height <= 0) return 1;
  if (!applyDisplayMode(platform->window(), {800, 600, 32}, true, error) ||
      !platform->pixelSize(size, error) || size.width != 800 || size.height != 600) return 1;
  for (const auto mode : {PlatformDisplayMode{0, 600, 32}, PlatformDisplayMode{800, 600, 16}}) {
    if (applyDisplayMode(platform->window(), mode, true, error) || error.empty() ||
        !platform->pixelSize(size, error) || size.width != 800 || size.height != 600) return 1;
  }
  if (!applyDisplayMode(platform->window(), {640, 480, 32}, true, error)) return 1;
	{
		auto input = PlatformInput::create(error);
		if (!input) { std::fprintf(stderr, "input: %s\n", error.c_str()); return 1; }
		input->captureKey(SDL_SCANCODE_A, 'a', 0, true, false);
		input->captureKey(SDL_SCANCODE_A, 'a', 0, false, false);
		input->captureKey(SDL_SCANCODE_1, '1', SDL_KMOD_SHIFT, true, false);
		if (input->nextKey() != (uint32_t{'a'} << 16 | 0x1e) || input->nextKey() != 0 || input->nextKey() != (uint32_t{'!'} << 16 | 0x02) || input->nextKey() != 0) return 1;
		input->captureKey(SDL_SCANCODE_A, 'a', SDL_KMOD_CTRL, true, false);
		if (input->nextKey()) return 1;
		SDL_Event keyEvent{};
		keyEvent.type = SDL_EVENT_KEY_DOWN; keyEvent.key.scancode = SDL_SCANCODE_B; keyEvent.key.key = 'b';
		if (!SDL_PushEvent(&keyEvent) || input->nextKey() != (uint32_t{'b'} << 16 | 0x30) || !SDL_HasEvent(SDL_EVENT_KEY_DOWN)) return 1;
		const auto found = std::find(input->names().begin(), input->names().end(), "SysMouse");
		if (found != input->names().end()) {
			const auto index = static_cast<int32_t>(found - input->names().begin());
			input->beginFrame();
			SDL_Event wheel{};
			wheel.type = SDL_EVENT_MOUSE_WHEEL;
			wheel.wheel.y = 1.0f;
			if (!SDL_PushEvent(&wheel)) return 1;
			SDL_Delay(2);
			input->beginFrame();
			const auto sampled = input->sample(index, 2);
			if (!sampled || *sampled <= 0.0f || *sampled > 1.0f || input->type(index, 2) != 1 ||
			    !SDL_HasEvent(SDL_EVENT_MOUSE_WHEEL)) return 1;
			input->beginFrame();
			if (input->sample(index, 2) != 0.0f) return 1;
			SDL_Event button{};
			button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
			button.button.button = SDL_BUTTON_LEFT;
			button.button.x = 25; button.button.y = 40;
			if (!SDL_PushEvent(&button)) return 1;
			button.type = SDL_EVENT_MOUSE_BUTTON_UP;
			button.button.x = 30; button.button.y = 45;
			if (!SDL_PushEvent(&button)) return 1;
			input->beginFrame();
			if (input->sample(index, 3) != 1.0f ||
			    input->frameButtonPosition() != std::array<float, 2>{25, 40} ||
			    input->sample(index, 3) != 1.0f) return 1;
			input->beginFrame(false);
			if (input->sample(index, 3) != 1.0f ||
			    input->frameButtonPosition() != std::array<float, 2>{25, 40}) return 1;
			input->beginFrame();
			if (input->sample(index, 3) != 0.0f ||
			    input->frameButtonPosition() != std::array<float, 2>{30, 45}) return 1;
			input->beginFrame();
			if (input->frameButtonPosition()) return 1;
		}
		const auto keyboard = std::find(input->names().begin(), input->names().end(), "SysKeyboard");
		if (keyboard != input->names().end()) {
			const auto index = static_cast<int32_t>(keyboard - input->names().begin());
			SDL_Event key{};
			key.type = SDL_EVENT_KEY_DOWN; key.key.scancode = SDL_SCANCODE_C; key.key.key = 'c';
			if (!SDL_PushEvent(&key)) return 1;
			key.type = SDL_EVENT_KEY_UP;
			if (!SDL_PushEvent(&key)) return 1;
			input->beginFrame();
			if (input->sample(index, 0x2e) != 1.0f) return 1;
			input->beginFrame();
			if (input->sample(index, 0x2e) != 0.0f) return 1;
		}
	}
	SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
	if (visible) {
		const Uint64 end = SDL_GetTicks() + 20000;
		while (SDL_GetTicks() < end) {
			for (const auto& event : platform->poll()) {
				if (std::holds_alternative<PlatformQuit>(event)) return 0;
			}
			SDL_Delay(10);
		}
		return 0;
	}
	platform->poll();
	const SDL_WindowID id = SDL_GetWindowID(platform->window());
	SDL_Event event{};
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.windowID = id;
	event.key.scancode = SDL_SCANCODE_A;
	event.key.repeat = true;
	if (!SDL_PushEvent(&event)) return 1;
	event.type = SDL_EVENT_KEY_UP;
	event.key.repeat = false;
	if (!SDL_PushEvent(&event)) return 1;
	event = {};
	event.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
	event.window.windowID = id;
	event.window.data1 = 800;
	event.window.data2 = 600;
	if (!SDL_PushEvent(&event)) return 1;
	event = {};
	event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
	event.window.windowID = id + 1;
	if (!SDL_PushEvent(&event)) return 1;
	event.window.windowID = id;
	if (!SDL_PushEvent(&event)) return 1;
	const auto events = platform->poll();
	if (events.size() != 4) return 1;
	const auto* down = std::get_if<PlatformKey>(&events[0]);
	const auto* up = std::get_if<PlatformKey>(&events[1]);
	const auto* resized = std::get_if<PlatformPixelSize>(&events[2]);
	if (!down || down->scancode != SDL_SCANCODE_A || !down->down || !down->repeat ||
		!up || up->scancode != SDL_SCANCODE_A || up->down || up->repeat ||
		!resized || resized->width != 800 || resized->height != 600 ||
		!std::holds_alternative<PlatformQuit>(events[3])) return 1;
	event = {};
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.windowID = id + 1;
	event.motion.x = 10.5f;
	event.motion.y = 20.25f;
	event.motion.xrel = -1.5f;
	event.motion.yrel = 2.25f;
	if (!SDL_PushEvent(&event)) return 1;
	event.motion.windowID = id;
	if (!SDL_PushEvent(&event)) return 1;
	event = {};
	event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	event.button.windowID = id + 1;
	event.button.button = SDL_BUTTON_RIGHT;
	event.button.x = 11.5f;
	event.button.y = 21.25f;
	if (!SDL_PushEvent(&event)) return 1;
	event.button.windowID = id;
	if (!SDL_PushEvent(&event)) return 1;
	event.type = SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.windowID = id + 1;
	if (!SDL_PushEvent(&event)) return 1;
	event.button.windowID = id;
	if (!SDL_PushEvent(&event)) return 1;
	const auto mouse = platform->poll();
	if (mouse.size() != 3) return 1;
	const auto* motion = std::get_if<PlatformMouseMotion>(&mouse[0]);
	const auto* pressed = std::get_if<PlatformMouseButton>(&mouse[1]);
	const auto* released = std::get_if<PlatformMouseButton>(&mouse[2]);
	if (!motion || motion->x != 10.5f || motion->y != 20.25f || motion->dx != -1.5f || motion->dy != 2.25f ||
		!pressed || pressed->button != SDL_BUTTON_RIGHT || !pressed->down || pressed->x != 11.5f || pressed->y != 21.25f ||
		!released || released->button != SDL_BUTTON_RIGHT || released->down || released->x != 11.5f || released->y != 21.25f) return 1;
	event = {};
	event.type = SDL_EVENT_QUIT;
	if (!SDL_PushEvent(&event)) return 1;
	const auto quit = platform->poll();
	if (quit.size() != 1 || !std::holds_alternative<PlatformQuit>(quit[0])) return 1;
	std::puts("platform event test: ok");
	return 0;
}
