#include "platform.hpp"
#include "cursor_resource.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace {
bool SDLCALL watchInput(void* userdata, SDL_Event* event) {
  auto* input = static_cast<PlatformInput*>(userdata);
  if (event->type == SDL_EVENT_MOUSE_MOTION)
    input->captureMouse(event->motion.xrel, event->motion.yrel, 0.0f);
  else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP)
    input->captureMouseButton(event->button.button, event->type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                              event->button.x, event->button.y);
  else if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)
    input->captureKey(static_cast<uint32_t>(event->key.scancode), event->key.key,
                      static_cast<uint16_t>(event->key.mod), event->type == SDL_EVENT_KEY_DOWN, event->key.repeat);
  else if (event->type == SDL_EVENT_MOUSE_WHEEL)
    input->captureMouse(0.0f, 0.0f, event->wheel.y * 120.0f * (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f));
  return true;
}
} // namespace

std::unique_ptr<PlatformInput> PlatformInput::create(std::string& error) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
    error = SDL_GetError();
    return nullptr;
  }
  auto input = std::unique_ptr<PlatformInput>(new PlatformInput());
  if (SDL_HasKeyboard()) {
    input->names_.emplace_back("SysKeyboard");
    input->kinds_.push_back(Kind::Keyboard);
  }
  if (SDL_HasMouse()) {
    input->names_.emplace_back("SysMouse");
    input->kinds_.push_back(Kind::Mouse);
  }
  int count = 0;
  SDL_JoystickID* ids = SDL_GetJoysticks(&count);
  if (!ids) {
    error = SDL_GetError();
    return nullptr;
  }
  for (int i = 0; i < count; ++i) {
    const char* name = SDL_GetJoystickNameForID(ids[i]);
    if (!name) {
      error = SDL_GetError();
      SDL_free(ids);
      return nullptr;
    }
    input->names_.emplace_back(name);
    input->kinds_.push_back(Kind::Joystick);
  }
  SDL_free(ids);
  SDL_PumpEvents();
  input->frameButtons_ = SDL_GetMouseState(nullptr, nullptr);
  int keyCount = 0;
  const bool* keys = SDL_GetKeyboardState(&keyCount);
  if (keys)
    for (int i = 0; i < keyCount; ++i)
      if (keys[i]) input->frameKeys_.insert(static_cast<uint32_t>(i));
  if (!SDL_AddEventWatch(watchInput, input.get())) {
    error = SDL_GetError();
    return nullptr;
  }
  input->watching_ = true;
  input->lastFrame_ = SDL_GetTicksNS();
  error.clear();
  return input;
}

PlatformInput::~PlatformInput() {
  if (watching_) SDL_RemoveEventWatch(watchInput, this);
  if (changedCursor_) {
    SDL_SetCursor(previousCursor_);
    if (previousCursorVisible_)
      SDL_ShowCursor();
    else
      SDL_HideCursor();
  }
  for (SDL_Cursor* cursor : stockCursors_)
    if (cursor) SDL_DestroyCursor(cursor);
  SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_VIDEO);
}

bool PlatformInput::selectStockCursor(const std::filesystem::path& exe, int32_t index,
                                      bool visible, std::string& error) {
  // FUN_004b7bd0 indexes the handles FUN_004b7ff0 loads from cursor groups
  // 0x74..0x79, 0x7b..0x7e; an index outside 0..9 selects 0.
  if (index < 0 || index > 9) index = 0;
  static const uint16_t groups[10] = {0x74, 0x75, 0x76, 0x77, 0x78,
                                      0x79, 0x7b, 0x7c, 0x7d, 0x7e};
  SDL_Cursor*& selectedCursor_ = stockCursors_[static_cast<size_t>(index)];
  if (!selectedCursor_) {
    CursorPixels pixels;
    if (!loadStockCursor(exe, groups[index], pixels, error)) return false;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(pixels.width),
                                                 static_cast<int>(pixels.height), SDL_PIXELFORMAT_RGBA32, pixels.rgba.data(),
                                                 static_cast<int>(pixels.width * 4));
    if (!surface) {
      error = SDL_GetError();
      return false;
    }
    selectedCursor_ = SDL_CreateColorCursor(surface, static_cast<int>(pixels.hotspotX),
                                            static_cast<int>(pixels.hotspotY));
    SDL_DestroySurface(surface);
    if (!selectedCursor_) {
      error = SDL_GetError();
      return false;
    }
  }
  if (!changedCursor_) {
    previousCursor_ = SDL_GetCursor();
    previousCursorVisible_ = SDL_CursorVisible();
    changedCursor_ = true;
  }
  if (!SDL_SetCursor(selectedCursor_) || !(visible ? SDL_ShowCursor() : SDL_HideCursor())) {
    error = SDL_GetError();
    return false;
  }
  error.clear();
  return true;
}

void PlatformInput::captureMouse(float x, float y, float wheel) {
  std::lock_guard<std::mutex> lock(mouseMutex_);
  pendingMouse_[0] += x;
  pendingMouse_[1] += y;
  pendingMouse_[2] += wheel;
}

void PlatformInput::captureMouseButton(uint8_t button, bool down, float x, float y) {
  if (button != SDL_BUTTON_LEFT && button != SDL_BUTTON_RIGHT &&
      button != SDL_BUTTON_MIDDLE && button != SDL_BUTTON_X1) return;
  std::lock_guard<std::mutex> lock(mouseMutex_);
  mouseButtonTransitions_.push_back({button, down, x, y});
}

std::optional<std::array<float, 2>> PlatformInput::frameButtonPosition() const {
  std::lock_guard<std::mutex> lock(mouseMutex_);
  return frameButtonPosition_;
}

void PlatformInput::beginFrame(bool advanceMouseButtons) {
  SDL_PumpEvents();
  const uint64_t now = SDL_GetTicksNS();
  {
    std::lock_guard<std::mutex> lock(mouseMutex_);
    frameMilliseconds_ = static_cast<float>(static_cast<double>(now - lastFrame_) / 1000000.0);
    lastFrame_ = now;
    for (size_t i = 0; i < 3; ++i) {
      frameMouse_[i] = pendingMouse_[i];
      pendingMouse_[i] = 0.0f;
    }
    if (advanceMouseButtons) {
      frameButtonPosition_.reset();
      while (!mouseButtonTransitions_.empty()) {
        const auto event = mouseButtonTransitions_.front();
        mouseButtonTransitions_.pop_front();
        const auto mask = SDL_BUTTON_MASK(event.button);
        if (((frameButtons_ & mask) != 0) == event.down) continue;
        if (event.down)
          frameButtons_ |= mask;
        else
          frameButtons_ &= ~mask;
        frameButtonPosition_ = std::array<float, 2>{event.x, event.y};
        break;
      }
    }
  }
  {
    std::lock_guard<std::mutex> lock(keyMutex_);
    for (auto it = keyTransitions_.begin(); it != keyTransitions_.end();) {
      auto& transitions = it->second;
      while (!transitions.empty()) {
        const bool down = transitions.front();
        transitions.pop_front();
        const bool held = frameKeys_.count(it->first) != 0;
        if (held == down) continue;
        if (down)
          frameKeys_.insert(it->first);
        else
          frameKeys_.erase(it->first);
        break;
      }
      if (transitions.empty())
        it = keyTransitions_.erase(it);
      else
        ++it;
    }
  }
}

namespace {
std::optional<SDL_Scancode> keyboardScancode(int32_t axis) {
  if (axis >= 0x02 && axis <= 0x0a)
    return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + axis - 0x02);
  if (axis >= 0x3b && axis <= 0x44)
    return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + axis - 0x3b);
  switch (axis) {
  case 0x00:
    return SDL_SCANCODE_UNKNOWN;
  case 0x01:
    return SDL_SCANCODE_ESCAPE;
  case 0x0b:
    return SDL_SCANCODE_0;
  case 0x0c:
    return SDL_SCANCODE_MINUS;
  case 0x0d:
    return SDL_SCANCODE_EQUALS;
  case 0x0e:
    return SDL_SCANCODE_BACKSPACE;
  case 0x0f:
    return SDL_SCANCODE_TAB;
  case 0x10:
    return SDL_SCANCODE_Q;
  case 0x11:
    return SDL_SCANCODE_W;
  case 0x12:
    return SDL_SCANCODE_E;
  case 0x13:
    return SDL_SCANCODE_R;
  case 0x14:
    return SDL_SCANCODE_T;
  case 0x15:
    return SDL_SCANCODE_Y;
  case 0x16:
    return SDL_SCANCODE_U;
  case 0x17:
    return SDL_SCANCODE_I;
  case 0x18:
    return SDL_SCANCODE_O;
  case 0x19:
    return SDL_SCANCODE_P;
  case 0x1a:
    return SDL_SCANCODE_LEFTBRACKET;
  case 0x1b:
    return SDL_SCANCODE_RIGHTBRACKET;
  case 0x1c:
    return SDL_SCANCODE_RETURN;
  case 0x1d:
    return SDL_SCANCODE_LCTRL;
  case 0x1e:
    return SDL_SCANCODE_A;
  case 0x1f:
    return SDL_SCANCODE_S;
  case 0x20:
    return SDL_SCANCODE_D;
  case 0x21:
    return SDL_SCANCODE_F;
  case 0x22:
    return SDL_SCANCODE_G;
  case 0x23:
    return SDL_SCANCODE_H;
  case 0x24:
    return SDL_SCANCODE_J;
  case 0x25:
    return SDL_SCANCODE_K;
  case 0x26:
    return SDL_SCANCODE_L;
  case 0x27:
    return SDL_SCANCODE_SEMICOLON;
  case 0x28:
    return SDL_SCANCODE_APOSTROPHE;
  case 0x29:
    return SDL_SCANCODE_GRAVE;
  case 0x2a:
    return SDL_SCANCODE_LSHIFT;
  case 0x2b:
    return SDL_SCANCODE_BACKSLASH;
  case 0x2c:
    return SDL_SCANCODE_Z;
  case 0x2d:
    return SDL_SCANCODE_X;
  case 0x2e:
    return SDL_SCANCODE_C;
  case 0x2f:
    return SDL_SCANCODE_V;
  case 0x30:
    return SDL_SCANCODE_B;
  case 0x31:
    return SDL_SCANCODE_N;
  case 0x32:
    return SDL_SCANCODE_M;
  case 0x33:
    return SDL_SCANCODE_COMMA;
  case 0x34:
    return SDL_SCANCODE_PERIOD;
  case 0x35:
    return SDL_SCANCODE_SLASH;
  case 0x36:
    return SDL_SCANCODE_RSHIFT;
  case 0x38:
    return SDL_SCANCODE_LALT;
  case 0x37:
    return SDL_SCANCODE_KP_MULTIPLY;
  case 0x39:
    return SDL_SCANCODE_SPACE;
  case 0x3a:
    return SDL_SCANCODE_CAPSLOCK;
  case 0x45:
    return SDL_SCANCODE_NUMLOCKCLEAR;
  case 0x46:
    return SDL_SCANCODE_SCROLLLOCK;
  case 0x47:
    return SDL_SCANCODE_KP_7;
  case 0x48:
    return SDL_SCANCODE_KP_8;
  case 0x49:
    return SDL_SCANCODE_KP_9;
  case 0x4a:
    return SDL_SCANCODE_KP_MINUS;
  case 0x4b:
    return SDL_SCANCODE_KP_4;
  case 0x4c:
    return SDL_SCANCODE_KP_5;
  case 0x4d:
    return SDL_SCANCODE_KP_6;
  case 0x4e:
    return SDL_SCANCODE_KP_PLUS;
  case 0x4f:
    return SDL_SCANCODE_KP_1;
  case 0x50:
    return SDL_SCANCODE_KP_2;
  case 0x51:
    return SDL_SCANCODE_KP_3;
  case 0x52:
    return SDL_SCANCODE_KP_0;
  case 0x53:
    return SDL_SCANCODE_KP_PERIOD;
  case 0x57:
    return SDL_SCANCODE_F11;
  case 0x58:
    return SDL_SCANCODE_F12;
  case 0x9c:
    return SDL_SCANCODE_KP_ENTER;
  case 0x9d:
    return SDL_SCANCODE_RCTRL;
  case 0xb5:
    return SDL_SCANCODE_KP_DIVIDE;
  case 0xb8:
    return SDL_SCANCODE_RALT;
  case 0xc7:
    return SDL_SCANCODE_HOME;
  case 0xc8:
    return SDL_SCANCODE_UP;
  case 0xc9:
    return SDL_SCANCODE_PAGEUP;
  case 0xcb:
    return SDL_SCANCODE_LEFT;
  case 0xcd:
    return SDL_SCANCODE_RIGHT;
  case 0xcf:
    return SDL_SCANCODE_END;
  case 0xd0:
    return SDL_SCANCODE_DOWN;
  case 0xd1:
    return SDL_SCANCODE_PAGEDOWN;
  case 0xd2:
    return SDL_SCANCODE_INSERT;
  case 0xd3:
    return SDL_SCANCODE_DELETE;
  default:
    return std::nullopt;
  }
}
} // namespace

// Stock PTR_DAT_0061ac44 display names indexed by DirectInput scancode,
// extracted verbatim from the immutable exe; undefined slots read as "?".
const std::pair<int32_t, const char*> kKeyboardAxisNames[] = {
    {1, "Escape"},
    {2, "1"},
    {3, "2"},
    {4, "3"},
    {5, "4"},
    {6, "5"},
    {7, "6"},
    {8, "7"},
    {9, "8"},
    {10, "9"},
    {11, "0"},
    {12, "-"},
    {13, "="},
    {14, "Backspace"},
    {15, "Tab"},
    {16, "Q"},
    {17, "W"},
    {18, "E"},
    {19, "R"},
    {20, "T"},
    {21, "Y"},
    {22, "U"},
    {23, "I"},
    {24, "O"},
    {25, "P"},
    {26, "["},
    {27, "]"},
    {28, "Return"},
    {29, "LCtrl"},
    {30, "A"},
    {31, "S"},
    {32, "D"},
    {33, "F"},
    {34, "G"},
    {35, "H"},
    {36, "J"},
    {37, "K"},
    {38, "L"},
    {39, ";"},
    {40, "'"},
    {41, "`"},
    {42, "LShift"},
    {43, "\\"},
    {44, "Z"},
    {45, "X"},
    {46, "C"},
    {47, "V"},
    {48, "B"},
    {49, "N"},
    {50, "M"},
    {51, ","},
    {52, "."},
    {53, "/"},
    {54, "RShift"},
    {55, "Num*"},
    {56, "LAlt"},
    {57, "Space"},
    {58, "CapsLock"},
    {59, "F1"},
    {60, "F2"},
    {61, "F3"},
    {62, "F4"},
    {63, "F5"},
    {64, "F6"},
    {65, "F7"},
    {66, "F8"},
    {67, "F9"},
    {68, "F10"},
    {69, "NumLock"},
    {70, "ScrollLock"},
    {71, "Num7"},
    {72, "Num8"},
    {73, "Num9"},
    {74, "Num-"},
    {75, "Num4"},
    {76, "Num5"},
    {77, "Num6"},
    {78, "Num+"},
    {79, "Num1"},
    {80, "Num2"},
    {81, "Num3"},
    {82, "Num0"},
    {83, "Num."},
    {87, "F11"},
    {88, "F12"},
};

void PlatformInput::captureKey(uint32_t scancode, uint32_t keycode, uint16_t modifiers, bool down, bool repeat) {
  if (repeat) return;
  std::lock_guard<std::mutex> lock(keyMutex_);
  keyTransitions_[scancode].push_back(down);
  if (keyEvents_.size() >= 256) {
    keyOverflow_ = true;
    return;
  }
  keyEvents_.push_back({scancode, keycode, modifiers, down});
}

std::optional<uint32_t> PlatformInput::nextKey() {
  SDL_PumpEvents();
  std::lock_guard<std::mutex> lock(keyMutex_);
  if (keyOverflow_) return std::nullopt;
  if (keyEvents_.empty()) return uint32_t{0};
  const KeyEvent event = keyEvents_.front();
  keyEvents_.pop_front();
  if (!event.down) return uint32_t{0};
  int32_t scan = -1;
  for (int32_t i = 1; i < 256; ++i) {
    const auto mapped = keyboardScancode(i);
    if (mapped && static_cast<uint32_t>(*mapped) == event.scancode) {
      scan = i;
      break;
    }
  }
  if (scan < 0) return std::nullopt;
  uint32_t ch = event.keycode;
  if (ch >= 128 && (ch & SDLK_SCANCODE_MASK) == 0) return std::nullopt;
  if (ch & SDLK_SCANCODE_MASK) ch = 0;
  if (ch && (event.modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) return std::nullopt;
  const bool shift = (event.modifiers & SDL_KMOD_SHIFT) != 0;
  const bool caps = (event.modifiers & SDL_KMOD_CAPS) != 0;
  if (ch >= 'a' && ch <= 'z') {
    if (shift != caps) ch -= 'a' - 'A';
  } else if (shift) {
    const std::string plain = "1234567890-=[]\\;',./`";
    const std::string shifted = "!@#$%^&*()_+{}|:\"<>?~";
    const auto at = plain.find(static_cast<char>(ch));
    if (at != std::string::npos) ch = static_cast<uint32_t>(shifted[at]);
  }
  return static_cast<uint32_t>(scan) | (ch << 16);
}

std::optional<float> PlatformInput::sample(int32_t device, int32_t axis) const {
  if (device < 0 || static_cast<size_t>(device) >= kinds_.size()) return std::nullopt;
  const auto kind = kinds_[static_cast<size_t>(device)];
  if (kind == Kind::Mouse && axis >= 0 && axis <= 2) {
    std::lock_guard<std::mutex> lock(mouseMutex_);
    if (frameMilliseconds_ <= 0.0f) return 0.0f;
    return std::clamp(frameMouse_[axis] / frameMilliseconds_ * 0.005f, -1.0f, 1.0f);
  }
  if (kind == Kind::Mouse && axis >= 3 && axis <= 6) {
    const uint8_t buttons[] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE, SDL_BUTTON_X1};
    std::lock_guard<std::mutex> mouseLock(mouseMutex_);
    auto state = frameButtons_;
    {
      std::lock_guard<std::mutex> lock(syntheticMutex_);
      state |= syntheticButtons_;
    }
    return (state & SDL_BUTTON_MASK(buttons[axis - 3])) ? 1.0f : 0.0f;
  }
  if (kind != Kind::Keyboard || axis < 0) return std::nullopt;
  if (axis >= 256) return 0.0f;
  const auto scancode = keyboardScancode(axis);
  if (!scancode) return std::nullopt;
  bool held;
  {
    std::lock_guard<std::mutex> lock(keyMutex_);
    held = frameKeys_.count(static_cast<uint32_t>(*scancode)) != 0;
  }
  std::lock_guard<std::mutex> lock(syntheticMutex_);
  return (held || syntheticKeys_.count(*scancode)) ? 1.0f : 0.0f;
}

void PlatformInput::setSyntheticMouseButton(uint8_t sdlButton, bool down) {
  std::lock_guard<std::mutex> lock(syntheticMutex_);
  if (down)
    syntheticButtons_ |= SDL_BUTTON_MASK(sdlButton);
  else
    syntheticButtons_ &= ~SDL_BUTTON_MASK(sdlButton);
}

void PlatformInput::setSyntheticKey(uint32_t scancode, bool down) {
  std::lock_guard<std::mutex> lock(syntheticMutex_);
  if (down)
    syntheticKeys_.insert(scancode);
  else
    syntheticKeys_.erase(scancode);
}

bool PlatformInput::keyHeld(uint32_t scancode) const {
  {
    std::lock_guard<std::mutex> lock(keyMutex_);
    if (frameKeys_.count(scancode)) return true;
  }
  std::lock_guard<std::mutex> lock(syntheticMutex_);
  return syntheticKeys_.count(scancode) != 0;
}

std::optional<int32_t> PlatformInput::type(int32_t device, int32_t axis) const {
  if (device < 0 || static_cast<size_t>(device) >= kinds_.size() || axis < 0) return std::nullopt;
  if (kinds_[static_cast<size_t>(device)] == Kind::Keyboard) return 0;
  if (kinds_[static_cast<size_t>(device)] == Kind::Mouse && axis >= 0 && axis <= 2) return 1;
  if (kinds_[static_cast<size_t>(device)] == Kind::Mouse && axis >= 3 && axis <= 6) return 0;
  return std::nullopt;
}

std::optional<std::string> PlatformInput::axisName(int32_t device, int32_t axis) const {
  if (device < 0 || static_cast<size_t>(device) >= kinds_.size() || axis < 0)
    return std::nullopt;
  const auto kind = kinds_[static_cast<size_t>(device)];
  if (kind == Kind::Keyboard) {
    if (axis >= 256) return std::nullopt;
    for (const auto& entry : kKeyboardAxisNames)
      if (entry.first == axis) return std::string(entry.second);
    return std::string("?");
  }
  if (kind == Kind::Mouse) {
    static const char* const kMouseAxisNames[] = {
        "Mouse X", "Mouse Y", "Mouse Z",
        "M.Button 1", "M.Button 2", "M.Button 3", "M.Button 4"};
    if (axis >= 7) return std::nullopt;
    return std::string(kMouseAxisNames[axis]);
  }
  return std::nullopt;
}

int32_t PlatformInput::axisCount(int32_t device) const {
  if (device < 0 || static_cast<size_t>(device) >= kinds_.size()) return 0;
  switch (kinds_[static_cast<size_t>(device)]) {
  case Kind::Keyboard:
    return 256;
  case Kind::Mouse:
    return 7;
  case Kind::Joystick:
    return 0;
  }
  return 0;
}

std::unique_ptr<Platform> Platform::create(const char* title, int width, int height, std::string& error) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    error = SDL_GetError();
    return nullptr;
  }
  SDL_Window* window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) {
    error = SDL_GetError();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return nullptr;
  }
  error.clear();
  return std::unique_ptr<Platform>(new Platform(window));
}

Platform::~Platform() {
  SDL_DestroyWindow(window_);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

std::vector<PlatformEvent> Platform::poll() {
  std::vector<PlatformEvent> events;
  const SDL_WindowID id = SDL_GetWindowID(window_);
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
      events.emplace_back(PlatformQuit{});
      break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      if (event.window.windowID == id) events.emplace_back(PlatformQuit{});
      break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
      if (event.key.windowID == id) {
        events.emplace_back(PlatformKey{static_cast<uint32_t>(event.key.scancode),
                                        event.type == SDL_EVENT_KEY_DOWN, event.key.repeat});
      }
      break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      if (event.window.windowID == id) {
        events.emplace_back(PlatformPixelSize{event.window.data1, event.window.data2});
      }
      break;
    case SDL_EVENT_MOUSE_MOTION:
      if (event.motion.windowID == id) {
        events.emplace_back(PlatformMouseMotion{event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel});
      }
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (event.button.windowID == id) {
        events.emplace_back(PlatformMouseButton{event.button.button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                                                event.button.x, event.button.y});
      }
      break;
    default:
      break;
    }
  }
  return events;
}

bool Platform::pixelSize(PlatformPixelSize& size, std::string& error) const {
  if (!SDL_GetWindowSizeInPixels(window_, &size.width, &size.height)) {
    error = SDL_GetError();
    return false;
  }
  error.clear();
  return true;
}

std::vector<PlatformDisplayMode> uniqueDisplayModes(const std::vector<PlatformDisplayMode>& modes) {
  std::vector<PlatformDisplayMode> result;
  for (const auto& mode : modes)
    if (mode[0] > 0 && mode[1] > 0 && (mode[2] == 16 || mode[2] == 32) &&
        std::find(result.begin(), result.end(), mode) == result.end()) result.push_back(mode);
  return result;
}
std::optional<std::vector<PlatformDisplayMode>> platformDisplayModes(std::string& error) {
  error.clear();
  const auto display = SDL_GetPrimaryDisplay();
  if (!display) {
    error = SDL_GetError();
    return std::nullopt;
  }
  int count = 0;
  SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
  if (!modes) {
    error = SDL_GetError();
    return std::nullopt;
  }
  std::vector<PlatformDisplayMode> values;
  // The native renderer supports 32-bit color. Do not offer unimplemented
  // 16-bit output as a selectable mode in the stock options dialog.
  for (int i = 0; i < count; ++i)
    if (modes[i] && SDL_BITSPERPIXEL(modes[i]->format) == 32)
      values.push_back({modes[i]->w, modes[i]->h, static_cast<int32_t>(SDL_BITSPERPIXEL(modes[i]->format))});
  SDL_free(modes);
  return uniqueDisplayModes(values);
}

std::optional<std::array<float, 2>> normalizeCursorPosition(float x, float y, int width, int height) {
  if (width <= 0 || height <= 0 || !std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
  return std::array<float, 2>{std::clamp(2.0f * x / width - 1.0f, -1.0f, 1.0f),
                              std::clamp(2.0f * y / height - 1.0f, -1.0f, 1.0f)};
}

bool applyDisplayMode(SDL_Window* window, PlatformDisplayMode mode, bool windowed, std::string& error) {
  if (!window || mode[0] <= 0 || mode[1] <= 0 || mode[0] > 32767 || mode[1] > 32767 || mode[2] != 32) {
    error = "display request unsupported: dimensions must fit stock signed shorts and host renderer requires 32-bit color";
    return false;
  }
  const SDL_DisplayMode* selected = nullptr;
  SDL_DisplayMode selectedValue{};
  if (!windowed) {
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(window), &count);
    if (!modes) {
      error = SDL_GetError();
      return false;
    }
    for (int i = 0; i < count; ++i)
      if (modes[i] && modes[i]->w == mode[0] && modes[i]->h == mode[1] && SDL_BITSPERPIXEL(modes[i]->format) == mode[2]) {
        selectedValue = *modes[i];
        selected = &selectedValue;
        break;
      }
    SDL_free(modes);
    // macOS may expose only the desktop mode. The renderer retains the stock
    // content resolution and letterboxes it; nullptr selects desktop fullscreen.
  }
  int oldWidth = 0, oldHeight = 0;
  if (!SDL_GetWindowSize(window, &oldWidth, &oldHeight)) {
    error = SDL_GetError();
    return false;
  }
  const bool oldFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
  const SDL_DisplayMode* oldMode = SDL_GetWindowFullscreenMode(window);
  std::optional<SDL_DisplayMode> previous;
  if (oldMode) previous = *oldMode;
  if (!SDL_SetWindowFullscreen(window, false) || !SDL_SetWindowSize(window, mode[0], mode[1]) ||
      !SDL_SetWindowFullscreenMode(window, selected) || !SDL_SetWindowFullscreen(window, !windowed) || !SDL_SyncWindow(window)) {
    error = SDL_GetError();
    SDL_SetWindowFullscreen(window, false);
    SDL_SetWindowSize(window, oldWidth, oldHeight);
    SDL_SetWindowFullscreenMode(window, previous ? &*previous : nullptr);
    SDL_SetWindowFullscreen(window, oldFullscreen);
    SDL_SyncWindow(window);
    return false;
  }
  error.clear();
  return true;
}
