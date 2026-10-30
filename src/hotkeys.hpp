#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

struct VmObject;

// Stock Input's 512-entry table, owned by the host for the VM lifetime.
struct HotkeyState {
  struct Entry {
    int32_t flags = 0;
    int32_t key = 0;
    const VmObject *hotkey = nullptr;
    const VmObject *osd = nullptr;
    int32_t state = 0;
    const VmObject *handler = nullptr;
    int32_t eventFilter = 0;
    int32_t initialized = 0;
  };
  std::array<Entry, 512> entries{};
  std::function<std::optional<float>(int32_t)> physicalKey;
  std::function<std::optional<uint32_t>()> lastKey;
  std::array<uint8_t, 16> characterRing{};
  size_t characterWrite = 0;
  std::function<std::optional<float>(const VmObject *, int32_t)> virtualAxis;
};
